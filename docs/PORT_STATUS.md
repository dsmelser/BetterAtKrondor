# Port status

Where the native port stands. Update this file in the same commit as the work it describes.

## Milestone 1 — boot to the main menu (Windows x86)

| Step | Status |
|---|---|
| Fork docs and rules (`CLAUDE.md`, `README.md`, `CONTRIBUTING.md`) | ✅ |
| Explicit integer widths (`s16/u16/s32/u32` sweep, `port/tools/retype16.py`) | ✅ (byte-neutrality check pending in `reference.yml`) |
| Reference build in CI (`reference.yml`) | 🚧 |
| Native build skeleton (CMake presets, `bak_native.h`, Borland RTL shims, ASM stubs) | ⬜ |
| Inline asm on the boot path rewritten in C | ⬜ |
| SDL3 platform layer (pages + present, virtual PIT, input, memory, files) | ⬜ |
| Boot-path ASM in C (codecs, `FARPTR`, `RAND`, `DOSRW`, `PALDAC`, …) | ⬜ |
| Differential test harness (Unicorn) | ⬜ |
| Boots the user's data to the main menu | ⬜ (needs the GOG copy) |

## ASM modules

The 50 TASM modules linked into `KRONDOR.EXE` (1.02 link set). ⬜ = generated stub,
🚧 = partial, ✅ = C replacement verified, 🔌 = replaced by the platform layer.

_Filled in once the stubs are generated._

## Known divergences from the original

_None recorded yet._
