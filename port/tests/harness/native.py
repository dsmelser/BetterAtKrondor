"""Client for bak_difftest, the executable that runs the C translations.

The protocol is described in port/tests/difftest/difftest.h. One process serves a whole
test session; the C side keeps its globals between requests, so tests set the state they
depend on (poke) instead of assuming a fresh process.
"""

from __future__ import annotations

import os
import struct
import subprocess
import threading
from pathlib import Path


class NativeError(Exception):
    """The C side rejected a request (dt_fail) or died."""


class Native:
    def __init__(self, exe: str | Path):
        env = dict(os.environ, BAK_HEADLESS="1", SDL_VIDEO_DRIVER="dummy")
        self.exe = Path(exe)
        self.proc = subprocess.Popen([str(self.exe)], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        self._stderr: list[bytes] = []
        # Drain stderr continuously so a chatty process can never block on a full pipe.
        self._drain = threading.Thread(target=self._read_stderr, daemon=True)
        self._drain.start()

    def _read_stderr(self) -> None:
        for chunk in iter(lambda: self.proc.stderr.read1(4096), b""):
            self._stderr.append(chunk)

    def _died(self) -> NativeError:
        self.proc.wait(timeout=10)
        self._drain.join(timeout=5)
        err = b"".join(self._stderr).decode(errors="replace").strip()
        return NativeError(f"{self.exe.name} exited with code {self.proc.returncode:#x}"
                           + (f":\n{err}" if err else ""))

    def _read_exact(self, n: int) -> bytes:
        data = self.proc.stdout.read(n)
        if data is None or len(data) < n:
            raise self._died()
        return data

    def call(self, command: str, payload: bytes = b"") -> bytes:
        frame = command.encode() + b"\0" + bytes(payload)
        try:
            self.proc.stdin.write(struct.pack("<I", len(frame)) + frame)
            self.proc.stdin.flush()
        except (BrokenPipeError, OSError):
            raise self._died() from None
        status, size = struct.unpack("<II", self._read_exact(8))
        body = self._read_exact(size) if size else b""
        if status != 0:
            raise NativeError(f"{command}: {body.decode(errors='replace')}")
        return body

    def peek(self, name: str) -> bytes:
        return self.call("peek", name.encode() + b"\0")

    def poke(self, name: str, data: bytes) -> None:
        self.call("poke", name.encode() + b"\0" + bytes(data))

    def close(self) -> None:
        if self.proc.poll() is None:
            self.proc.stdin.close()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
