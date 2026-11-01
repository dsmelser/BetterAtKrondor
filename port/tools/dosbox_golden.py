"""Golden screenshots: the original game in DOSBox Staging against the port.

The black-box check from the port plan. The original KRONDOR.EXE from the user's copy runs in
DOSBox Staging, the port runs on the same data, both get the same keys, and the pictures they
show are compared pixel by pixel. Captures go to work/golden/<scenario>/ (never committed: they
are the game's art).

    uv run python -I port/tools/dosbox_golden.py run main-menu --data "E:/GOG/Betrayal at Krondor"
    uv run python -I port/tools/dosbox_golden.py trace --data "E:/GOG/Betrayal at Krondor" --seconds 30

DOSBox Staging 0.84 or later runs headless (SDL's dummy video driver), with its own settings
only (--noprimaryconf), sound off and the game folder mounted under an overlay, so the
game's writes land in the capture folder. Its HTTP API (localhost) reads emulated memory
between CPU instructions, but reads VGA memory through the CPU's read path: one plane, the
one the graphics controller's Read Map Select names, loading the latches. So the capture hooks
IRQ 0 with a small routine (CAPTURE_CODE) that, inside the interrupt, saves the CRTC,
sequencer, graphics controller and DAC, then selects each plane in turn while the host reads
A000h through the API, and chains to the game's handler. The game is stopped meanwhile. Its
next latch copy may be off if the interrupt fell inside one; the capture itself is not.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.request
import zlib
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port" / "tests"))
from harness.mzexe import parse_map  # noqa: E402

DOSBOX = Path(r"C:\Program Files\DOSBox Staging\dosbox.exe")
PORT_EXE = ROOT / "build" / "win-x86-clangcl" / "betterkrondor.exe"
OUT_DIR = ROOT / "work" / "golden"

# KRONDOR.EXE's sha256 prefix -> the reference build's map (see port/tests/README.md).
MAPS = {"c943fd895a57": "OUT/KRONDOR.MAP", "e254770143e0": "OUT102/KRONDOR.MAP"}

# ---- the capture routine -----------------------------------------------------------------
#
# A block of BLOCK_SIZE bytes in emulated memory: data at the start, code at CODE_OFF. The
# routine addresses its data through CS, so the block's segment is all it needs.
OLD_VECTOR = 0x00   # dd: the IRQ 0 handler to chain to
STATE = 0x04        # db: 0 idle, 1 armed, 2 capturing (host drives the planes), 3 done
PLANE_REQ = 0x05    # db: host: the plane to select next; FFh ends the capture
PLANE_ACK = 0x06    # db: routine: the plane selected
CRTC_INDEX = 0x08   # db: CRTC index at the interrupt; then registers 00h-18h
CRTC = 0x09
GC_INDEX = 0x22     # db: graphics controller index; then registers 0-8
GC = 0x23
SEQ_INDEX = 0x2C    # db: sequencer index; then registers 0-4
SEQ = 0x2D
DAC_STATE = 0x32    # db: port 3C7h (3: read mode)
DAC_WINDEX = 0x33   # db: port 3C8h, the write index, restored afterwards
PEL_MASK = 0x34     # db: port 3C6h
ATTR_INDEX = 0x35   # db: attribute controller index (with the palette address source bit)
ATTR = 0x36         # 21 bytes: attribute registers 00h-14h
DAC = 0x100         # 768 bytes: the DAC's 256 colours, 6 bits a component
CODE_OFF = 0x400
BLOCK_SIZE = 0x540

# Assembled with clang (--target=i386-unknown-none-elf, .code16); offsets from CODE_OFF.
#
#   400 cmp byte cs:[4],1 / jne 526           armed? otherwise chain
#   40A push ax,bx,dx,cx,di,es / cld / es=cs
#   413 dx=3D4h: save the index; read registers 00h-18h to cs:[9]; restore the index
#   433 dx=3C4h: the same for sequencer registers 0-4 to cs:[2Dh]
#   453 dx=3CEh: save the index; graphics controller registers 0-8 to cs:[23h]
#   46E 3C0h: save the attribute index; for each of registers 00h-14h: in 3DAh (flip-flop
#       to index), out 3C0h,reg|20h (keeps the display on), in 3C1h to cs:[36h]; then
#       in 3DAh, out 3C0h,saved index, in 3DAh (flip-flop back to index)
#   4A1 3C7h (DAC state), 3C8h (write index) and 3C6h (pel mask) to cs:[32h..34h]
#   4B7 out 3C7h,0 / rep insb 768 bytes of 3C9h to cs:[100h] / out 3C8h,write index
#   4CC GC 5 = saved & F7h (read mode 0); byte cs:[4] = 2
#   4E1 spin: plane_req FFh -> done; differs from plane_ack -> GC 4 = plane_req,
#       plane_ack = plane_req
#   501 done: restore GC 4, GC 5 and the GC index; byte cs:[4] = 3; pop
#   526 jmp far cs:[0]
CAPTURE_CODE = bytes.fromhex(
    "2e803e0400010f851c01505352515706fc0e07bad403ec2ea20800bf0900b30088d8ee42ecaa4afec3"
    "80fb1972f22ea00800eebac403ec2ea22c00bf2d00b30088d8ee42ecaa4afec380fb0572f22ea02c00"
    "eebace03ec2ea22200bf2300b30088d8ee42ecaa4afec380fb0972f2bac003ec2ea23500bf3600b300"
    "bada03ecbac00388d80c20ee42ecaafec380fb1572eabada03ecbac0032ea03500eebada03ecbac703"
    "ec2ea2320042ec2ea23300bac603ec2ea2340042b000eebac903bf0001b90003f36c4a2ea03300eeba"
    "ce03b005ee422ea0280024f7ee4a2ec6060400022ea005003cff74182e3a06060074f188c4b004ee42"
    "88e0ee4a2e88260600ebe0b004ee422ea02700ee4ab005ee422ea02800ee4a2ea02200ee2ec6060400"
    "03075f595a5b582eff2e0000"
)
IRQ0_VECTOR = 0x08 * 4


# ---- DOSBox Staging and its HTTP API ------------------------------------------------------


class DosBox:
    """A headless DOSBox Staging running the game, driven through its HTTP API."""

    def __init__(self, exe: Path, port: int):
        self.exe = exe
        self.port = port
        self.base = f"http://127.0.0.1:{port}/api/v1"
        self.proc: subprocess.Popen | None = None
        self.log = None
        self.started = 0.0
        self.block = 0  # linear address of the capture block

    def start(self, data: Path, work: Path, keys: list[tuple[float, str]], pace: float) -> None:
        overlay = work / "overlay"
        if overlay.exists():
            shutil.rmtree(overlay)
        overlay.mkdir(parents=True)
        lines = [
            f'mount c "{data}"',
            f'mount c "{overlay}" -t overlay',
            "c:",
        ]
        if keys:
            lines.append(autotype_command(keys, pace))
        lines += ["KRONDOR.EXE", "exit"]
        conf = work / "dosbox.conf"
        conf.write_text(
            "\n".join([
                "[sdl]", "fullscreen = false",
                "[dosbox]", "machine = svga_s3", "memsize = 16",
                "[cpu]", "core = normal", "cpu_cycles = 20000",
                "[mixer]", "nosound = true",
                "[midi]", "mpu401 = intelligent", "mididevice = none",
                "[speaker]", "pcspeaker = off",
                "[webserver]", "webserver_enabled = on", f"webserver_port = {self.port}",
                "[autoexec]", *lines, "",
            ]),
            encoding="utf-8", newline="\n")
        env = dict(os.environ, SDL_VIDEO_DRIVER="dummy")
        self.log = open(work / "dosbox.log", "wb")
        self.proc = subprocess.Popen(
            [str(self.exe), "--noprimaryconf", "--nolocalconf", "--conf", str(conf)],
            env=env, stdout=self.log, stderr=subprocess.STDOUT, cwd=work)
        self.started = time.monotonic()
        deadline = self.started + 20
        while True:
            try:
                self.json("/dosbox/info")
                return
            except OSError:
                if self.proc.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError(f"DOSBox did not start its API (see {work / 'dosbox.log'})")
                time.sleep(0.2)

    def elapsed(self) -> float:
        return time.monotonic() - self.started

    def stop(self) -> None:
        if self.proc is None:
            return
        try:
            self.request("/dosbox/shutdown", method="POST", body=b"")
        except OSError:
            pass
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        self.proc = None
        self.log.close()

    def request(self, path: str, method: str = "GET", body: bytes | None = None,
                headers: dict[str, str] | None = None) -> bytes:
        req = urllib.request.Request(self.base + path, data=body, method=method,
                                     headers=headers or {})
        for attempt in range(5):
            try:
                with urllib.request.urlopen(req, timeout=30) as r:
                    return r.read()
            except urllib.error.HTTPError as e:
                # The API answers 500 "timeout" while the emulator does not take requests
                # (starting up, or inside a DOS call); ask again.
                if e.code != 500 or b"timeout" not in e.read() or attempt == 4:
                    raise
                time.sleep(0.2)
        raise AssertionError("unreachable")

    def json(self, path: str, method: str = "GET", body: dict | None = None) -> dict:
        data = None if body is None else json.dumps(body).encode()
        hdrs = {"Accept": "application/json"}
        if data is not None:
            hdrs["Content-Type"] = "application/json"
        return json.loads(self.request(path, method, data, hdrs))

    def read(self, addr: int, n: int) -> bytes:
        data = self.request(f"/memory/{addr:#x}/{n:#x}",
                            headers={"Accept": "application/octet-stream"})
        if len(data) != n:
            raise RuntimeError(f"read {n:#x} bytes at {addr:#x}: got {len(data):#x}")
        return data

    def write(self, addr: int, data: bytes, expect: bytes | None = None) -> None:
        hdrs = {"Content-Type": "application/octet-stream"}
        if expect is not None:
            hdrs["If-Match"] = base64.b64encode(expect).decode()
        self.request(f"/memory/{addr:#x}", method="PUT", body=data, headers=hdrs)

    def registers(self) -> dict[str, int]:
        return self.json("/cpu/state")["registers"]

    def current_psp(self) -> int:
        sda = self.json("/dos/internals")["dosSwappableArea"]
        return struct.unpack("<H", self.read(sda + 0x10, 2))[0]

    # -- capture --

    def capture(self) -> Capture:
        if not self.block:
            self.block = self.allocate(BLOCK_SIZE)
        seg = self.block >> 4
        old = self.read(IRQ0_VECTOR, 4)
        block = bytearray(CODE_OFF)
        block[OLD_VECTOR:OLD_VECTOR + 4] = old
        block[STATE] = 1
        block[PLANE_REQ] = block[PLANE_ACK] = 0xFE
        self.write(self.block, bytes(block) + CAPTURE_CODE)
        hook = struct.pack("<HH", CODE_OFF, seg)
        self.write(IRQ0_VECTOR, hook, expect=old)
        try:
            self.wait_byte(STATE, 2)
            planes = []
            for p in range(4):
                self.write(self.block + PLANE_REQ, bytes([p]))
                self.wait_byte(PLANE_ACK, p)
                planes.append(self.read(0xA0000, 0x10000))
            self.write(self.block + PLANE_REQ, b"\xff")
            self.wait_byte(STATE, 3)
        finally:
            self.write(IRQ0_VECTOR, old, expect=hook)
        regs = self.read(self.block, CODE_OFF)
        return Capture(planes=planes, crtc=regs[CRTC:CRTC + 0x19], gc=regs[GC:GC + 9],
                       seq=regs[SEQ:SEQ + 5], attr=regs[ATTR:ATTR + 0x15],
                       dac=regs[DAC:DAC + 0x300], pel_mask=regs[PEL_MASK])

    def allocate(self, size: int) -> int:
        last = None
        for area in ("UMA", "conv"):
            try:
                addr = self.json("/memory/allocate", "POST",
                                 {"size": size, "area": area})["addr"]
            except urllib.error.HTTPError as e:
                last = e
                continue
            if addr % 16:
                raise RuntimeError(f"allocation at {addr:#x} is not paragraph-aligned")
            return addr
        raise RuntimeError(f"no memory for the capture routine: {last}")

    def wait_byte(self, off: int, value: int, timeout: float = 10) -> None:
        deadline = time.monotonic() + timeout
        while self.read(self.block + off, 1)[0] != value:
            if time.monotonic() > deadline:
                raise RuntimeError(f"capture routine: byte {off:#x} never became {value:#x}")
            time.sleep(0.01)


def autotype_command(keys: list[tuple[float, str]], pace: float) -> str:
    """DOSBox's AUTOTYPE types at one pace after a first wait; a comma adds one pace of
    pause. Key times are rounded to the pace."""
    first = keys[0][0]
    words = [keys[0][1]]
    t = first
    for at, key in keys[1:]:
        gap = round((at - t) / pace)
        if gap < 1:
            raise ValueError(f"keys closer than the pace ({pace} s): {key} at {at}")
        words += [","] * (gap - 1) + [key]
        t += gap * pace
    return f"autotype -w {first:g} -p {pace:g} " + " ".join(words)


# ---- what the VGA shows -------------------------------------------------------------------


@dataclass
class Capture:
    planes: list[bytes]   # 4 x 64 KB
    crtc: bytes
    gc: bytes
    seq: bytes
    attr: bytes           # attribute registers 00h-14h
    dac: bytes            # 6-bit components
    pel_mask: int

    def chunky(self) -> bytes:
        """Display memory in the port's model (bak_video.h): planar byte (o, p) at 4o + p."""
        out = bytearray(0x40000)
        for p in range(4):
            out[p::4] = self.planes[p]
        return bytes(out)

    def start_address(self) -> int:
        return self.crtc[0x0C] << 8 | self.crtc[0x0D]

    def attribute_color(self, nibble: int) -> int:
        """A 16-colour pixel's DAC index: its attribute palette register, bits 4-7 from
        the colour select register as the paging mode (mode control bit 7) says."""
        a = self.attr
        v = a[nibble & a[0x12] & 0x0F]
        if a[0x10] & 0x80:
            return (v & 0x0F) | (a[0x14] & 0x0F) << 4
        return (v & 0x3F) | (a[0x14] & 0x0C) << 4

    def picture(self) -> tuple[int, int, list[int]]:
        """(width, height, DAC indices) of the picture the CRTC scans out: 256 colours,
        four pixels to a planar byte (Mode Y), or 16 colours, eight pixels to a byte in
        each plane (mode 10h)."""
        c = self.crtc
        colors256 = bool(self.gc[5] & 0x40)
        if self.seq[4] & 0x08:
            raise RuntimeError(f"chain-4 is on (seq 4 = {self.seq[4]:02X}): not decoded")
        if not c[0x17] & 0x40 or c[0x14] & 0x40:
            raise RuntimeError("CRTC not in byte mode")
        width = (c[0x01] + 1) * (4 if colors256 else 8)
        vde = c[0x12] | (c[0x07] & 0x02) << 7 | (c[0x07] & 0x40) << 3
        lines_per_row = ((c[0x09] & 0x1F) + 1) * (2 if c[0x09] & 0x80 else 1)
        height = (vde + 1) // lines_per_row
        line_compare = c[0x18] | (c[0x07] & 0x10) << 4 | (c[0x09] & 0x40) << 3
        if line_compare <= vde:
            raise RuntimeError(f"split screen at line {line_compare}: not decoded")
        start = self.start_address()
        pitch = c[0x13] * 2
        pix = []
        for y in range(height):
            row = start + y * pitch
            if colors256:
                for x in range(width):
                    pix.append(self.planes[x & 3][(row + (x >> 2)) & 0xFFFF] & self.pel_mask)
                continue
            for x in range(width):
                o = (row + (x >> 3)) & 0xFFFF
                bit = 7 - (x & 7)
                nibble = sum(((self.planes[p][o] >> bit) & 1) << p for p in range(4))
                pix.append(self.attribute_color(nibble) & self.pel_mask)
        return width, height, pix

    def rgb6(self) -> tuple[int, int, list[tuple[int, int, int]]]:
        w, h, pix = self.picture()
        pal = [tuple(self.dac[i * 3:i * 3 + 3]) for i in range(256)]
        return w, h, [pal[i] for i in pix]


# ---- images -------------------------------------------------------------------------------


def write_png(path: Path, w: int, h: int, rgb8: list[tuple[int, int, int]]) -> None:
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for r, g, b in rgb8[y * w:(y + 1) * w]:
            raw += bytes((r, g, b))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
                     + chunk(b"IEND", b""))


def to8(c6: tuple[int, int, int]) -> tuple[int, int, int]:
    """6-bit DAC components to 8 bits, as the port presents them."""
    return tuple((v << 2 | v >> 4) for v in c6)  # type: ignore[return-value]


def read_bmp_rgb6(path: Path) -> tuple[int, int, list[tuple[int, int, int]]]:
    """The port's BAK_DUMP_FRAME (SDL_SaveBMP, 24 or 32 bits a pixel), back to the DAC's
    6-bit components (the port widens them as v << 2 | v >> 4)."""
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise ValueError(f"{path}: not a BMP")
    off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp not in (24, 32):
        raise ValueError(f"{path}: {bpp} bits a pixel")
    step = bpp // 8
    stride = (w * step + 3) & ~3
    rows = range(h - 1, -1, -1) if h > 0 else range(-h)
    h = abs(h)
    out = []
    for y in rows:
        base = off + y * stride
        for x in range(w):
            b, g, r = data[base + x * step:base + x * step + 3]
            out.append((r >> 2, g >> 2, b >> 2))
    return w, h, out


# ---- symbols ------------------------------------------------------------------------------


class GameMap:
    """The reference build's map for the user's KRONDOR.EXE, to name CS:IP."""

    def __init__(self, data: Path):
        exe = (data / "KRONDOR.EXE").read_bytes()
        digest = hashlib.sha256(exe).hexdigest()
        ref = Path(os.environ.get("BAK_REF_DIR", ROOT / "work" / "ref"))
        rel = MAPS.get(digest[:12])
        if rel is None:
            raise RuntimeError(f"unknown KRONDOR.EXE (sha256 {digest[:12]})")
        self.symbols = sorted((seg * 16 + off, name)
                              for name, (seg, off) in parse_map(ref / rel).items())
        cblp, cp, _crlc, cparhdr = struct.unpack_from("<4H", exe, 2)
        self.size = cp * 512 - ((512 - cblp) if cblp else 0) - cparhdr * 16

    def name(self, load_seg: int, cs: int, ip: int) -> str:
        """The public at or before CS:IP, or CS:IP outside the EXE's image (the driver
        chunks and DOS)."""
        lin = (cs - load_seg) * 16 + ip
        best = None
        for addr, name in self.symbols:
            if addr > lin:
                break
            best = (addr, name)
        if best is None or not 0 <= lin < self.size:
            return f"{cs:04X}:{ip:04X}"
        return f"{best[1]}+{lin - best[0]:X}"


# ---- scenarios ----------------------------------------------------------------------------


@dataclass
class Scenario:
    description: str
    # The original's keys, in seconds from start-up. Its timeline is not the port's: in
    # DOSBox (20000 cycles) the MT-32 driver's set-up alone takes about 5 s and the intro
    # starts near 13 s.
    keys: list[tuple[float, str]]   # (seconds, AUTOTYPE key name)
    settle: float                   # seconds from start-up before the first capture
    port_input: str                 # BAK_INPUT for the port; its last frame is compared


SCENARIOS = {
    "main-menu": Scenario(
        description="Escape during the intro skips it and the credits: the main menu",
        keys=[(18.0, "esc")], settle=30.0,
        port_input="6000:key:Escape;20000:quit"),
    # N is Start New Game's key (its action id in REQ_OPT0.DAT is scancode 31h).
    "book": Scenario(
        description="Start New Game: the first page of chapter 1's book (640x350)",
        keys=[(18.0, "esc"), (28.0, "n")], settle=90.0,
        port_input="6000:key:Escape;9000:key:N;35000:quit"),
    # Space pages through the book (in DOSBox it ends near 160 s); the cutscene after it plays
    # out by itself (past 230 s), and the party then stands in the world.
    "world": Scenario(
        description="Start New Game, the book paged through, the cutscene: the world's first view",
        keys=[(18.0, "esc"), (28.0, "n")] + [(75.0 + 4 * k, "space") for k in range(24)],
        settle=420.0,
        port_input="6000:key:Escape;9000:key:N;" + "".join(
            f"{t}000:key:Space;" for t in (16, 19, 22, 25, 28, 31, 34, 37, 40, 45, 50, 55, 60, 70, 80))
        + "90000:quit"),
}


# ---- commands -----------------------------------------------------------------------------


def capture_stable(box: DosBox, tries: int = 6, gap: float = 1.0) -> Capture:
    """Capture until two pictures in a row agree (a fade or a redraw may be in progress)."""
    prev = box.capture()
    for _ in range(tries):
        time.sleep(gap)
        cur = box.capture()
        if cur.rgb6() == prev.rgb6():
            return cur
        prev = cur
    raise RuntimeError("the picture did not settle")


def compare(dos: tuple[int, int, list], port: tuple[int, int, list], diff_png: Path) -> int:
    (w, h, a), (w2, h2, b) = dos, port
    if (w, h) != (w2, h2):
        print(f"size differs: DOSBox {w}x{h}, port {w2}x{h2}")
        return w * h
    bad = [i for i in range(w * h) if a[i] != b[i]]
    img = []
    for i in range(w * h):
        if a[i] != b[i]:
            img.append((255, 0, 255))
        else:
            v = sum(to8(a[i])) // 9
            img.append((v, v, v))
    write_png(diff_png, w, h, img)
    if bad:
        xs = [i % w for i in bad]
        ys = [i // w for i in bad]
        print(f"{len(bad)} pixels differ, within x {min(xs)}-{max(xs)}, y {min(ys)}-{max(ys)}")
    return len(bad)


def cmd_run(args: argparse.Namespace) -> int:
    s = SCENARIOS[args.scenario]
    out = OUT_DIR / args.scenario
    out.mkdir(parents=True, exist_ok=True)
    data = Path(args.data)

    # The original.
    box = DosBox(Path(args.dosbox), args.api_port)
    try:
        box.start(data, out, s.keys, args.pace)
        wait = s.settle - box.elapsed()
        if wait > 0:
            time.sleep(wait)
        cap = capture_stable(box)
    finally:
        box.stop()
    (out / "dosbox.vram").write_bytes(cap.chunky())
    (out / "dosbox.dac").write_bytes(cap.dac)
    dos = cap.rgb6()
    write_png(out / "dosbox.png", dos[0], dos[1], [to8(c) for c in dos[2]])
    print(f"original: {dos[0]}x{dos[1]} from display memory {cap.start_address():04X}h")
    if args.skip_port:
        return 0

    # The port.
    bmp = out / "port.bmp"
    if bmp.exists():
        bmp.unlink()
    user = out / "user"
    user.mkdir(exist_ok=True)
    env = dict(os.environ, BAK_HEADLESS="1", SDL_VIDEO_DRIVER="dummy",
               BAK_INPUT=s.port_input, BAK_DUMP_FRAME=str(bmp))
    # The original draws its cursor into the picture; so does the port's game cursor.
    r = subprocess.run([args.exe, "--data", str(data), "--user", str(user),
                        "--set", "video.cursor=game"], env=env, check=False, timeout=120)
    if r.returncode != 0 or not bmp.exists():
        print(f"the port exited with {r.returncode:#x}")
        return 2
    port = read_bmp_rgb6(bmp)
    write_png(out / "port.png", port[0], port[1], [to8(c) for c in port[2]])

    n = compare(dos, port, out / "diff.png")
    print(f"{args.scenario}: {'identical' if n == 0 else 'DIFFERENT'} ({out})")
    return 0 if n == 0 else 1


def cmd_trace(args: argparse.Namespace) -> int:
    data = Path(args.data)
    gmap = GameMap(data)
    out = OUT_DIR / "trace"
    out.mkdir(parents=True, exist_ok=True)
    keys = [(float(t), k) for t, k in (kv.split(":") for kv in args.keys)] if args.keys else []
    box = DosBox(Path(args.dosbox), args.api_port)
    try:
        box.start(data, out, keys, args.pace)
        while box.elapsed() < args.seconds:
            r = box.registers()
            psp = box.current_psp()
            print(f"{box.elapsed():6.1f}s  {r['cs']:04X}:{r['eip']:04X}  "
                  f"{gmap.name(psp + 0x10, r['cs'], r['eip'] & 0xFFFF)}", flush=True)
            time.sleep(args.interval)
    finally:
        box.stop()
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--dosbox", default=str(DOSBOX), help="DOSBox Staging's dosbox.exe")
    ap.add_argument("--api-port", type=int, default=18086, help="port for DOSBox's HTTP API")
    ap.add_argument("--pace", type=float, default=0.5, help="AUTOTYPE's pace, in seconds")
    sub = ap.add_subparsers(dest="cmd", required=True)
    run = sub.add_parser("run", help="capture a scenario in both and compare")
    run.add_argument("scenario", choices=sorted(SCENARIOS))
    run.add_argument("--data", required=True, help="the game folder")
    run.add_argument("--exe", default=str(PORT_EXE), help="the port's executable")
    run.add_argument("--skip-port", action="store_true", help="capture the original only")
    tr = sub.add_parser("trace", help="print where the original is (CS:IP) over time")
    tr.add_argument("--data", required=True, help="the game folder")
    tr.add_argument("--seconds", type=float, default=30)
    tr.add_argument("--interval", type=float, default=0.5)
    tr.add_argument("--keys", nargs="*", help="keys as SECONDS:NAME (AUTOTYPE names)")
    args = ap.parse_args()
    return cmd_run(args) if args.cmd == "run" else cmd_trace(args)


if __name__ == "__main__":
    sys.exit(main())
