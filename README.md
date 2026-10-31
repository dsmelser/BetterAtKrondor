<h1 align="center">BetterAtKrondor</h1>

<p align="center">A native port of <em>Betrayal at Krondor</em> (1993) for modern systems,
built on a byte-perfect reconstruction of the original source.</p>

> **Status: early bring-up — nothing is playable yet.** Progress is tracked in
> [docs/PORT_STATUS.md](docs/PORT_STATUS.md).

## About

[canassa/betrayal-at-krondor](https://github.com/canassa/betrayal-at-krondor) reconstructed
the game's source from the DOS binaries until it rebuilt them byte for byte. That project is
about preservation and deliberately stops at DOS. BetterAtKrondor is a fork that takes the
reconstructed code to modern platforms:

- **Faithful first.** At default settings the port plays like the DOS release whose data it
  runs (1.00 floppy or 1.02 CD): same rules, timing and look.
- **Then better, optionally.** Rebindable controls and gamepad support, quality-of-life
  options, a high-resolution 3D view, upscaled art, widescreen. Every enhancement is a
  toggle; "Original" stays exact.
- **Native, no emulator.** Windows first, then Linux and macOS, using SDL3.

## You need your own copy of the game

This repository contains **no game data**. To run the port you need a legally obtained copy
of *Betrayal at Krondor* (for example the GOG release) and point the port at its files.

## Building

Windows, with Visual Studio 2022 or later and its *C++ Clang tools* component:

```
port\tools\win-build.cmd
build\win-x86-clangcl\betterkrondor.exe --data "C:\path\to\Betrayal at Krondor"
```

The script finds Visual Studio, loads its x86 environment and runs the `win-x86-clangcl` CMake
preset (SDL3 is downloaded and built automatically). The port is in early bring-up: it starts, reads
the game's configuration and stops at the first subsystem not ported yet. See
[docs/PORT_STATUS.md](docs/PORT_STATUS.md).

The original DOS build (Borland C++ in FreeDOS under QEMU) is preserved at the
`upstream-snapshot` tag and runs in CI (`.github/workflows/reference.yml`) to produce
reference binaries and objects for testing. Its instructions are in
[docs/BUILDING.md](docs/BUILDING.md).

## Asset viewer

Upstream's browser-based asset viewer is still here: drop your game files into
`viewer/data/`, run `uv run bak viewer`, and open the printed URL.

## Acknowledgements

- **canassa** and contributors, for the reconstruction this port is built on.
- **Guido de Jong** (xBaK) and **Emmanuel Jacyna** (BaKGL), whose earlier
  reverse-engineering the reconstruction built on.

## License & legal status

The original work in this repository is released under the MIT License (see
[`LICENSE`](LICENSE)).

**This project does not grant, and cannot grant, any rights to third-party material:**

- ***Betrayal at Krondor***, its code, data, and assets are copyright © 1993 Dynamix, Inc. /
  Sierra On-Line. The reconstructed C source under `bak/SRC/` is a reverse-engineered
  derivative of that copyrighted program, provided for research, preservation, and
  interoperability. This repository contains **no original game data files**; to run the
  port you must supply your own legally obtained copy of the game.

This is an independent effort and is not affiliated with or endorsed by Dynamix, Sierra, or
their successors.
