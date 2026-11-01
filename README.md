<h1 align="center">BetterAtKrondor</h1>

<p align="center">A native port of <em>Betrayal at Krondor</em> (1993) for modern systems,
built on a byte-perfect reconstruction of the original source.</p>

> **Status: in development, not playable through yet.** A new game reaches the 3-D world;
> much of the game beyond it is not ported. Progress is tracked in
> [docs/PORT_STATUS.md](docs/PORT_STATUS.md).

## About

[canassa/betrayal-at-krondor](https://github.com/canassa/betrayal-at-krondor) reconstructed
the game's source from the DOS binaries until it rebuilt them byte for byte. That project is
about preservation and deliberately stops at DOS. BetterAtKrondor is a fork that takes the
reconstructed code to modern platforms:

- **The same game.** The rules are those of the DOS release whose data it runs (1.00 floppy
  or 1.02 CD), with its bugs fixed, and saves stay compatible with it both ways.
- **Modern to play.** Rebindable controls and gamepad support, quality-of-life features, a
  high-resolution 3D view, upscaled art, widescreen.
- **Native, no emulator.** Windows first, then the web browser, using SDL3.

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
preset (SDL3 is downloaded and built automatically). A new game plays the intro, the first book
and the chapter's cutscene and enters the 3-D world; anything not ported yet stops with its
name. See [docs/PORT_STATUS.md](docs/PORT_STATUS.md).

The game folder is remembered, so later runs need no `--data`; without one the port asks for
it. Saves and settings (`betterkrondor.ini`) go to `%APPDATA%\BetterAtKrondor`, or the folder
`--user` names, and the game folder is never written to. `betterkrondor --help` lists the
options and settings.

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
