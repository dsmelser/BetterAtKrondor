"""Golden screenshots: the original game in DOSBox Staging against the port.

The black-box check from the port plan. The original KRONDOR.EXE from the user's copy runs in
DOSBox Staging, the port runs on the same data, both get the same keys, teleports and clicks (the
original through its memory, the port through BAK_INPUT), and the pictures they show are compared
pixel by pixel, as are the saves they leave (TEMP.GAM, the working save, after every scenario; a
scenario may save, or start both sides from a save the port made). Captures go to
work/golden/<scenario>/ (never committed: they are the game's art).

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
from dataclasses import dataclass, field
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
        self.publics: dict[str, tuple[int, int]] | None = None  # the game's map (GameMap)

    def start(self, data: Path, work: Path, keys: list[tuple[float, str]], pace: float,
              seed: dict[str, Path] | None = None) -> None:
        """Starts the game in the folder, the files of seed (a save to load, by its path under
        the game's folder) laid in the overlay, so the game finds them from its start."""
        overlay = work / "overlay"
        if overlay.exists():
            shutil.rmtree(overlay)
        overlay.mkdir(parents=True)
        for rel, src in (seed or {}).items():
            (overlay / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, overlay / rel)
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

    def press(self, scan: int, ascii: int) -> None:
        """A key in the BIOS keyboard buffer (0040:001E-003D), where the game's keyboard interrupt
        queues scancode:ASCII and kbhit_read takes it (KEYBOARD.ASM): the keys after AUTOTYPE's
        line, at their moment. The key goes in first and the tail moves after it, only if the
        game did not move it meanwhile."""
        for _ in range(20):
            head, tail = struct.unpack("<HH", self.read(0x41A, 4))
            after = tail + 2 if tail + 2 < 0x3E else 0x1E
            if after == head:
                raise RuntimeError("the keyboard buffer is full")
            self.write(0x400 + tail, struct.pack("<H", scan << 8 | ascii))
            try:
                self.write(0x41C, struct.pack("<H", after), expect=struct.pack("<H", tail))
                return
            except urllib.error.HTTPError:
                continue
        raise RuntimeError("the keyboard buffer kept moving")

    def registers(self) -> dict[str, int]:
        return self.json("/cpu/state")["registers"]

    def current_psp(self) -> int:
        sda = self.json("/dos/internals")["dosSwappableArea"]
        return struct.unpack("<H", self.read(sda + 0x10, 2))[0]

    def symbol(self, name: str) -> int:
        """The linear address of one of the game's publics (the reference build's map, whose
        segments count from the image's start: the running game's PSP and 100h), while the game
        is the program running."""
        if self.publics is None:
            raise RuntimeError("no map for the game's publics")
        seg, off = self.publics[name.upper()]
        return (self.current_psp() + 0x10 + seg) * 16 + off

    def until_the_picture_changes(self, poke, what: str, tries: int = 3, within: float = 8) -> None:
        """Poke the game (a step written into its memory) until the picture changes within a few
        seconds: every step of the scenarios opens or pages something. A poke the game did not
        act on (seen once in the Blue Wheel Inn, cause unknown) is made again."""
        before = self.capture().rgb6()
        for _ in range(tries):
            poke()
            deadline = time.monotonic() + within
            while time.monotonic() < deadline:
                time.sleep(1)
                if self.capture().rgb6() != before:
                    return
            print(f"{what} changed nothing in {within:g} s at {self.elapsed():.0f} s: again")
        raise RuntimeError(f"{what} changed nothing, {tries} times")

    def wait_for_world(self, timeout: float = 480) -> float:
        """Seconds from start-up at which the world's menu page was loaded (g_pReqMainPage, a
        near pointer, set right before the zone loads and the world loop runs, cleared after
        it), polled every two seconds from 60 s on, when the game is the program running. The
        world comes near 175 s after a new game's intro, book and cutscene."""
        wait = 60 - self.elapsed()
        if wait > 0:
            time.sleep(wait)
        at = self.symbol("_G_PREQMAINPAGE")
        while self.elapsed() < timeout:
            if self.read(at, 2) != b"\0\0":
                return self.elapsed()
            time.sleep(2)
        raise RuntimeError(f"the world did not come within {timeout:g} s")

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
    """DOSBox's AUTOTYPE types after a first wait, a key taking a pace and a comma pausing two
    (measured on the BIOS keyboard buffer with a pace of 0.5 s: a key every half second, a
    comma a second; until 2026-11-02 the tool counted a comma as one pace, so every key but
    the first came late, the world scenario's 24 Spaces 7.5 s apart from 132 s to 306 s, and
    the last sixteen fell into the world and its screens). Each key gets the commas that bring
    it nearest its time."""
    first = keys[0][0]
    words = [keys[0][1]]
    t = first + pace
    for at, key in keys[1:]:
        if at < t + pace:
            raise ValueError(f"keys closer than the pace ({pace} s): {key} at {at}")
        commas = round((at - t - pace) / (2 * pace))
        words += [","] * commas + [key]
        t += commas * 2 * pace + pace
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
        self.publics = parse_map(ref / rel)
        self.symbols = sorted((seg * 16 + off, name) for name, (seg, off) in self.publics.items())
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


# ---- what the original is told once it runs -----------------------------------------------
#
# After AUTOTYPE stops, the original is driven through its memory: keys into the BIOS keyboard
# buffer, and the game's own records where the port's script writes them too (teleport.c,
# mouse.c), found by the reference build's map. A step's run may return seconds that the steps
# after it and the capture wait longer (the world came late).


@dataclass
class Key:
    scan: int
    ascii: int

    def run(self, box: DosBox) -> None:
        box.press(self.scan, self.ascii)


@dataclass
class Teleport:
    """The game's pending teleport (GameState.abTeleportRecord, 23h into g_gameState), as the
    port's scripted teleport fills it: the world loop carries it out at its next pass, entering
    the town scene named (its kind and index) first; zone 255 enters the scene and moves nobody,
    as TELEPORT.DAT's own records for the scenes do."""
    zone: int
    tile_x: int = 0
    tile_y: int = 0
    cell_x: int = 0
    cell_y: int = 0
    heading: int = 0
    scene_kind: int = 0
    scene_index: int = 0

    def run(self, box: DosBox) -> None:
        at = box.symbol("_G_GAMESTATE") + 0x23
        rec = struct.pack("<5BHHH", self.zone, self.tile_x, self.tile_y, self.cell_x, self.cell_y,
                          self.heading, self.scene_kind, self.scene_index)

        def poke() -> None:
            box.write(at + 1, rec[1:])
            box.write(at, rec[:1])  # the zone last: the world loop acts once it is set

        box.until_the_picture_changes(poke, f"the teleport {rec.hex()}")

    def port_step(self) -> str:
        scene = f",{self.scene_kind},{self.scene_index}" if self.scene_kind else ""
        return (f"teleport:{self.zone},{self.tile_x},{self.tile_y},{self.cell_x},{self.cell_y},"
                f"{self.heading:x}{scene}")


@dataclass
class Click:
    """A click of the left button at pixel (x, y), through the state the game's mouse callback
    latches (MOUSE.ASM: the position in mickeys, four a pixel, and the buttons), which the
    game's queries read: DOSBox's API moves no mouse, and headless nothing else does. The
    game sees the pointer there, then the button down for a while, then up: a click."""
    x: int
    y: int

    def run(self, box: DosBox) -> None:
        at = box.symbol("_G_MOUSE_X_MICKEYS")
        buttons = box.symbol("_G_MOUSE_BUTTONS")
        pos = struct.pack("<HH", self.x * 4, self.y * 4)

        def poke() -> None:
            box.write(at, pos)
            time.sleep(0.5)
            box.write(buttons, b"\x01")
            time.sleep(0.5)
            box.write(buttons, b"\x00")

        box.until_the_picture_changes(poke, f"the click at ({self.x}, {self.y})")

    def port_step(self) -> str:
        return f"click:{self.x},{self.y}"


@dataclass
class WaitForWorld:
    """Waits for the world (DosBox.wait_for_world), expected by `expected` seconds from
    start-up, and when it comes later (a slow host) says how much: the steps after this one
    and the capture wait as much longer."""
    expected: float

    def run(self, box: DosBox) -> float:
        ready = box.wait_for_world()
        late = max(0.0, ready - self.expected)
        print(f"the world at {ready:.0f} s"
              + (f": the steps wait {late:.0f} s longer" if late else ""))
        return late


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
    # What the original is told after AUTOTYPE (above): (seconds, Key, Teleport, Click or
    # WaitForWorld). The steps over the world come after a WaitForWorld; when the world comes
    # later than it expects (a slow host), the steps after it and the capture move with it.
    late: list[tuple[float, Key | Teleport | Click | WaitForWorld]] = field(default_factory=list)
    # Rectangles (x0, y0, x1, y1, inclusive) whose differences do not count, and why: a text
    # field's caret, which blinks on the game's timer, so each side shows its own phase.
    ignore: list[tuple[int, int, int, int]] = field(default_factory=list)
    ignore_why: str = ""
    # The picture holds a dialog box, whose parchment pattern starts where a random draw says
    # (DIALOG.C): the original's draw is read from its memory after the capture, and the port
    # draws from the same place (BAK_PARCHMENT).
    parchment: bool = False
    # A script the port runs first, on the virtual clock from a fresh user folder (a quicksave
    # made two steps from the start), and the files it leaves, by their path under the user
    # folder, that both sides then start with: laid in the port's user folder and in the
    # original's folder (the overlay).
    prelude: str = ""
    seed: list[str] = field(default_factory=list)
    # The saves both sides leave, by their path under the game's folder, compared byte for
    # byte after the pictures (the working save, TEMP.GAM, always is).
    saves: list[str] = field(default_factory=list)


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


# Scan codes and ASCII of the keys pressed over the world.
KEYS = {"M": (0x32, ord("m")), "1": (0x02, ord("1")), "E": (0x12, ord("e")), "O": (0x18, ord("o")),
        "S": (0x1F, ord("s")), "R": (0x13, ord("r")), "T": (0x14, ord("t")), "N": (0x31, ord("n")),
        "Tab": (0x0F, 9), "Return": (0x1C, 13)}


def from_the_world(description: str, *keys: str) -> Scenario:
    """The world scenario, then keys (KEYS' names) once the party stands in the world, 10 s
    apart: from 240 s in DOSBox (the Spaces past the book page through the cutscene, and the world
    comes near 175 s), put in the keyboard buffer; from 90 s on the port (41 s), 3 s apart."""
    world = SCENARIOS["world"]
    end = "90000:quit"
    assert world.port_input.endswith(end), world.port_input
    port = "".join(f"{90 + 3 * i}000:key:{k};" for i, k in enumerate(keys))
    return Scenario(description=description, keys=world.keys, settle=260.0 + 10 * len(keys),
                    port_input=world.port_input[: -len(end)] + port
                    + f"{90 + 3 * len(keys) + 7}000:quit",
                    late=[(60.0, WaitForWorld(220.0))]
                    + [(240.0 + 10 * i, Key(*KEYS[k])) for i, k in enumerate(keys)])


def in_lamut(description: str, scene: int, *clicks: tuple[int, int], parchment: bool = False
             ) -> Scenario:
    """The world, then a teleport into one of LaMut's scenes (town 1: GDS1A.DAT is the town,
    B the Blue Wheel Inn, C the Fletcher's Post), the party staying where it is, then clicks
    in it: in DOSBox the teleport at 240 s and the clicks from 255 s, 10 s apart; on the port
    the teleport at 90 s and the clicks from 95 s, 4 s apart (a dialog opens with a wipe that
    takes 3 s, and ignores a click for a moment after a page)."""
    world = SCENARIOS["world"]
    end = "90000:quit"
    assert world.port_input.endswith(end), world.port_input
    into = Teleport(255, scene_kind=1, scene_index=scene)
    steps = [(90, into)] + [(95 + 4 * i, Click(x, y)) for i, (x, y) in enumerate(clicks)]
    port = "".join(f"{at}000:{step.port_step()};" for at, step in steps)
    return Scenario(description=description, keys=world.keys, settle=260.0 + 10 * len(clicks),
                    port_input=world.port_input[: -len(end)] + port
                    + f"{95 + 4 * len(clicks) + 5}000:quit",
                    late=[(60.0, WaitForWorld(220.0)), (240.0, into)]
                    + [(255.0 + 10 * i, Click(x, y)) for i, (x, y) in enumerate(clicks)],
                    parchment=parchment)


# The 2-D screens over the world (M6's parity step): the map (M), the first party member's
# inventory (1: Locklear's), the camp (E), the Options menu (O) and its Save Game dialog (S).
# (Restore Game is off while there is no save.)
SCENARIOS["map"] = from_the_world("The world, then M: the zone's map", "M")
SCENARIOS["inventory"] = from_the_world("The world, then 1: Locklear's inventory", "1")
SCENARIOS["camp"] = from_the_world("The world, then E: the camp and its clock", "E")
SCENARIOS["options"] = from_the_world("The world, then O: the Options menu", "O")
SCENARIOS["save"] = from_the_world("The world, then O and S: the Save Game dialog", "O", "S")
SCENARIOS["save"].ignore = [(147, 129, 147, 139)]
SCENARIOS["save"].ignore_why = "the Games field's caret, which blinks"
# The town scenes and what opens from them: LaMut, its Fletcher's Post with the purchasing
# counter clicked (the merchant's screen, straight from the counter: its actor has no dialog).
SCENARIOS["town"] = in_lamut("The world, then LaMut's town scene (GDS1A.DAT)", 1)
SCENARIOS["shop"] = in_lamut("LaMut's Fletcher's Post (GDS1C.DAT), its counter clicked: the "
                             "merchant's screen", 3, (47, 33))
# The Blue Wheel Inn (GDS1B.DAT): its patrons are drawn stretched (VGA slot 37).
SCENARIOS["inn"] = in_lamut("LaMut's Blue Wheel Inn (GDS1B.DAT): its patrons drawn stretched", 2)
# A talk with a portrait: the Blue Wheel Inn's dwarf (its actor 6) clicked, and his tale paged by
# clicks on the parchment to his third page: Dubal An Loch drawn over the scene (BLITAA's
# anti-aliased edges, PALBLEND's colours), his name, his words.
SCENARIOS["talk"] = in_lamut("The Blue Wheel Inn (GDS1B.DAT), its dwarf clicked and paged: Dubal "
                             "An Loch's portrait over the scene, his name and his words", 2,
                             (118, 73), (160, 180), (160, 180), (160, 180), parchment=True)
# The saves. savegame: the world, then the Save Game dialog (O, S), which opens on its Games
# field with the first folder, SAVES, chosen: "test" typed as the save's name, Tab to the
# Directories field and "one" typed over the folder's, and Save clicked (REQ_SAVE's button at
# 242,158): the folder GAMES\ONE.G02 is made, the game saved there as SAVE01.GAM, and the world
# is back. Both sides' save files are compared, as the working saves are after every scenario.
# In DOSBox the keys are typed 2 s apart from 260 s; on the port 0.3 s apart from 96 s.
_WORLD = SCENARIOS["world"].port_input[: -len("90000:quit")]
_TYPED = ["T", "E", "S", "T", "Tab", "O", "N", "E"]
SCENARIOS["savegame"] = Scenario(
    description="The world, then the Save Game dialog: the save test in a new folder one, and "
                "the world again",
    keys=SCENARIOS["world"].keys, settle=300.0,
    port_input=_WORLD + "90000:key:O;93000:key:S;96000:key:T;96300:key:E;96600:key:S;"
    "96900:key:T;97500:key:Tab;98000:key:O;98300:key:N;98600:key:E;99500:click:242,158;"
    "106000:quit",
    late=[(60.0, WaitForWorld(220.0)), (240.0, Key(*KEYS["O"])), (250.0, Key(*KEYS["S"]))]
    + [(260.0 + 2 * i, Key(*KEYS[k])) for i, k in enumerate(_TYPED)]
    + [(280.0, Click(242, 158))],
    saves=["GAMES/ONE.G02/SAVE01.GAM"])
# load: a quicksave of the port loaded by the original, and by the port. The prelude makes it on
# the virtual clock (the world, two steps, a turn left, F5: GAMES\SAVES.G01\SAVE00.GAM, the
# Bookmark of the first folder, which F5 makes in a fresh user folder), and both sides start
# with it. From the main menu, Restore Game (R) lists its folder, chosen at the start, and the
# Bookmark: the Bookmark clicked (the Games list's first row), Return, the full map with the
# chapter's caption while the zone loads, then the world where the party was, which is captured.
SCENARIOS["load"] = Scenario(
    description="Restore Game from the main menu: the port's quicksave, two steps and a turn "
                "from the start, loaded",
    keys=[(18.0, "esc"), (28.0, "r")], settle=90.0,
    port_input="6000:key:Escape;9000:key:R;12000:click:170,57;16000:key:Return;40000:quit",
    late=[(40.0, Click(170, 57)), (45.0, Key(*KEYS["Return"])), (47.0, WaitForWorld(70.0))],
    prelude=_WORLD + "90000:key:Up;91000:key:Up;92000:key:Left;94000:key:F5;97000:quit",
    seed=["GAMES/SAVES.G01/SAVE00.GAM"])


# ---- the saves ----------------------------------------------------------------------------

# TEMP.GAM's regions: GSTATE.H's GAM_* offsets (the 1.02 write guard's bounds agree).
GAM_REGIONS = [(0x0, "the game state"), (0xAD7, "the encounter table"),
               (0x113F, "the objfixed directory"), (0x12F7, "the gold stashes"),
               (0x131F, "the encounter rosters"), (0x3967, "the encounters' fought times"),
               (0x4457, "the encounters' visited times"),
               (0x4F47, "the encounter object states"), (0x90E7, "the combatants"),
               (0x312E5, "the combatant states"), (0x3A791, "the actor pool")]
TEMP_GAM_SIZE = 0x51AA9
# A save file: the title (90 bytes), the chapter, the map place, the compass icon, 16h, then
# TEMP.GAM (SAVEGAME.C).
SAVE_HEADER = 100
# The pointers in TEMP.GAM's records (GSTATE.C), which the original writes as its addresses
# and the port as 0, and which no loader reads (each is set again after a read): in each
# Combatant (5Fh bytes: the game state's six party members, then the 1730 of the combatants'
# region) the name (a near pointer, at 0), the actor record (far, at 59h) and the inner state
# (near, at 5Dh); in each CombatantState (16h bytes) the target (near, at 0); and in the game
# state's tail the shared inventory and the ground pile (far, at 2B5h and 2B9h). Compared
# saves set them apart: the original's hold its DGROUP offsets (the party's names are its
# characterNames) and the segments of its far blocks (the actor records, 144 bytes each, lie
# in a row), with 0 in every far pointer's offset word and in the party's inner pointers.
COMBATANT_POINTERS = [(0x00, 2, "name"), (0x59, 4, "actor record"), (0x5D, 2, "inner state")]
STATE_POINTERS = [(0x00, 2, "target")]
GAMESTATE_POINTERS = [(0x2B5, 4, "shared inventory"), (0x2B9, 4, "ground pile")]


def pointer_fields() -> dict[int, str]:
    """TEMP.GAM's pointer bytes, by offset: the field's name."""
    fields = {}

    def mark(base: int, pointers: list[tuple[int, int, str]]) -> None:
        for at, size, name in pointers:
            for k in range(size):
                fields[base + at + k] = name

    for i in range(6):
        mark(0x77 + i * 0x5F, COMBATANT_POINTERS)
    mark(0, GAMESTATE_POINTERS)
    for i in range(1730):
        mark(0x90E7 + i * 0x5F, COMBATANT_POINTERS)
        mark(0x312E5 + i * 0x16, STATE_POINTERS)
    return fields


def region(off: int) -> str:
    """Where in TEMP.GAM an offset falls."""
    base, name = GAM_REGIONS[0]
    for b, n in GAM_REGIONS:
        if b > off:
            break
        base, name = b, n
    return f"{name} + {off - base:#x}"


def run_prelude(exe: str, data: Path, s: Scenario, folder: Path) -> dict[str, Path]:
    """The port on the virtual clock from a fresh user folder, headless and silent, with the
    scenario's prelude script: the files it leaves that both sides start with, by their path
    under the game's folder."""
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir()
    env = dict(os.environ, BAK_CLOCK="virtual", BAK_HEADLESS="1", SDL_VIDEO_DRIVER="dummy",
               SDL_AUDIO_DRIVER="dummy", BAK_INPUT=s.prelude)
    r = subprocess.run([exe, "--data", str(data), "--user", str(folder),
                        "--set", "saves.autosave=false"], env=env, check=False, timeout=600)
    if r.returncode != 0:
        raise RuntimeError(f"the prelude exited with {r.returncode:#x}")
    for rel in s.seed:
        if not (folder / rel).is_file():
            raise RuntimeError(f"the prelude left no {rel} in {folder}")
    print(f"the prelude left {', '.join(s.seed)}")
    return {rel: folder / rel for rel in s.seed}


def find_file(root: Path, rel: str) -> Path | None:
    """The file at rel under root, whatever the case of its name (DOS writes upper case)."""
    at = root
    for part in rel.replace("\\", "/").split("/"):
        if not at.is_dir():
            return None
        found = [p for p in at.iterdir() if p.name.upper() == part.upper()]
        if not found:
            return None
        at = found[0]
    return at if at.is_file() else None


def compare_saves(dos: bytes, port: bytes, what: str) -> int:
    """The bytes that differ between two saves (TEMP.GAM, or a save file: its header, then
    TEMP.GAM) outside the records' pointers, each run of them placed in its region; the
    pointers that differ are counted apart, by field."""
    if len(dos) != len(port):
        print(f"  {what}: {len(dos)} bytes from the original, {len(port)} from the port")
        return max(len(dos), len(port))
    head = SAVE_HEADER if len(dos) == TEMP_GAM_SIZE + SAVE_HEADER else 0
    pointers = pointer_fields() if len(dos) == TEMP_GAM_SIZE + head else {}
    runs = []
    i = 0
    while i < len(dos):
        if dos[i] == port[i]:
            i += 1
            continue
        j = i
        while j < len(dos) and dos[j] != port[j]:
            j += 1
        runs.append((i, j))
        i = j
    fields: dict[str, int] = {}  # the pointer fields that differ, by name
    bad = []
    for i, j in runs:
        names = {pointers.get(k - head) for k in range(i, j)}
        if None in names:
            bad.append((i, j))
        else:
            for name in names:
                fields[name] = fields.get(name, 0) + 1  # type: ignore[index]
    if fields:
        print(f"  {what}: the pointers differ as the port writes them 0, "
              + ", ".join(f"{n} {name}" for name, n in sorted(fields.items())))
    if not bad:
        print(f"  {what}: identical ({len(dos)} bytes)"
              + (" but for the pointers" if fields else ""))
        return 0
    total = sum(j - i for i, j in bad)
    print(f"  {what}: {total} bytes differ, in {len(bad)} runs")
    for i, j in bad[:20]:
        where = f"the header + {i:#x}" if i < head else region(i - head)
        print(f"    {i:#08x} ({j - i} bytes), {where}: original {dos[i:j][:12].hex(' ')}, "
              f"port {port[i:j][:12].hex(' ')}")
    return total


def compare_saves_left(overlay: Path, user: Path, saves: list[str]) -> int:
    """The saves both sides left, the working save and the scenario's: the bytes that differ,
    one for a file one side alone left."""
    bad = 0
    for rel in ["TEMP.GAM", *saves]:
        d, p = find_file(overlay, rel), find_file(user, rel)
        if d is None and p is None:
            if rel in saves:
                print(f"  {rel}: neither side left it")
                bad += 1
            continue
        if d is None or p is None:
            print(f"  {rel}: only {'the original' if d else 'the port'} left it")
            bad += 1
            continue
        bad += compare_saves(d.read_bytes(), p.read_bytes(), rel)
    return bad


# ---- commands -----------------------------------------------------------------------------


def capture_stable(box: DosBox, tries: int = 6, gap: float = 1.0) -> Capture:
    """Capture until two pictures in a row agree (a fade or a redraw may be in progress). Then
    again closer together: a text field's caret blinks, so one second apart they never agree."""
    for gap_now in (gap, gap / 5):
        prev = box.capture()
        for _ in range(tries if gap_now == gap else 5 * tries):
            time.sleep(gap_now)
            cur = box.capture()
            if cur.rgb6() == prev.rgb6():
                return cur
            prev = cur
    raise RuntimeError("the picture did not settle")


def compare(dos: tuple[int, int, list], port: tuple[int, int, list], diff_png: Path,
            ignore: list[tuple[int, int, int, int]] = ()) -> int:
    """The pixels that differ outside the ignored rectangles (the diff shows them all)."""
    (w, h, a), (w2, h2, b) = dos, port
    if (w, h) != (w2, h2):
        print(f"size differs: DOSBox {w}x{h}, port {w2}x{h2}")
        return w * h
    every = [i for i in range(w * h) if a[i] != b[i]]
    bad = [i for i in every if not any(x0 <= i % w <= x1 and y0 <= i // w <= y1
                                       for x0, y0, x1, y1 in ignore)]
    if len(bad) != len(every):
        print(f"{len(every) - len(bad)} pixels differ where they may")
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

    # What both sides start with: what the port's prelude leaves.
    seed = run_prelude(args.exe, data, s, out / "prelude") if s.prelude else {}

    # The original.
    box = DosBox(Path(args.dosbox), args.api_port)
    if s.late:
        box.publics = GameMap(data).publics
    try:
        box.start(data, out, s.keys, args.pace, seed)
        shift = 0.0  # how much longer the steps wait: the world came late
        for at, step in s.late:
            wait = at + shift - box.elapsed()
            if wait > 0:
                time.sleep(wait)
            shift += step.run(box) or 0.0
        wait = s.settle + shift - box.elapsed()
        if wait > 0:
            time.sleep(wait)
        cap = capture_stable(box)
        parchment = None
        if s.parchment:  # where the last dialog box started its parchment (DIALOG.C)
            (parchment,) = struct.unpack("<H", box.read(box.symbol("_G_VGABLIT_SI_BASE"), 2))
    finally:
        box.stop()
    (out / "dosbox.vram").write_bytes(cap.chunky())
    (out / "dosbox.dac").write_bytes(cap.dac)
    dos = cap.rgb6()
    write_png(out / "dosbox.png", dos[0], dos[1], [to8(c) for c in dos[2]])
    print(f"original: {dos[0]}x{dos[1]} from display memory {cap.start_address():04X}h")
    if parchment is not None:
        if 0x3D40 <= parchment <= 0x3DA3:
            print(f"the original's parchment starts at {parchment:#06x}")
        else:
            print(f"the original's parchment base reads {parchment:#06x}: not a dialog box's "
                  f"draw, so the port's is not pinned")
            parchment = None
    if args.skip_port:
        return 0

    # The port, from a fresh user folder with the prelude's files.
    bmp = out / "port.bmp"
    if bmp.exists():
        bmp.unlink()
    user = out / "user"
    if user.exists():
        shutil.rmtree(user)
    user.mkdir()
    for rel, src in seed.items():
        (user / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, user / rel)
    # The port runs on the host's clock, and silent: its sound would otherwise play.
    env = dict(os.environ, BAK_HEADLESS="1", SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy",
               BAK_INPUT=s.port_input, BAK_DUMP_FRAME=str(bmp))
    if parchment is not None:
        env["BAK_PARCHMENT"] = str(parchment)
    # The original draws its cursor into the picture; so does the port's game cursor. It has no
    # autosaves, whose folder the save dialogs would list.
    r = subprocess.run([args.exe, "--data", str(data), "--user", str(user),
                        "--set", "video.cursor=game", "--set", "saves.autosave=false"],
                       env=env, check=False, timeout=120)
    if r.returncode != 0 or not bmp.exists():
        print(f"the port exited with {r.returncode:#x}")
        return 2
    port = read_bmp_rgb6(bmp)
    write_png(out / "port.png", port[0], port[1], [to8(c) for c in port[2]])

    n = compare(dos, port, out / "diff.png", s.ignore)
    but = f" but for {s.ignore_why}" if n == 0 and s.ignore else ""
    print(f"{args.scenario}: the picture is {'identical' + but if n == 0 else 'DIFFERENT'} "
          f"({out})")
    bad = compare_saves_left(out / "overlay", user, s.saves)
    print(f"{args.scenario}: the saves are {'identical' if bad == 0 else 'DIFFERENT'}")
    return 0 if n == 0 and bad == 0 else 1


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
