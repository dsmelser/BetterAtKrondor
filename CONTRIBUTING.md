# Contributing

## Where the project is

The game's source has been reconstructed (upstream: a byte-perfect rebuild of the 1993/1994
DOS binaries) and is now being brought up natively. Start with
[docs/PORT_STATUS.md](docs/PORT_STATUS.md): it lists what runs, what is stubbed, and what is
known to diverge from the original.

The ground rules are in [CLAUDE.md](CLAUDE.md). In short: faithful by default, every
enhancement a toggle, explicit integer widths, verified ASM replacements, no game data in the
repo.

## Kinds of work

### Replacing an ASM module

The 50 TASM modules linked into `KRONDOR.EXE` start as generated stubs in `port/asm/` that
abort with the routine's name. To replace one:

1. Read the original in `bak/SRC/**/<MODULE>.ASM`. Most carry a banner describing the
   routine, and many give a C equivalent.
2. Write the C version in `port/asm/<MODULE>.c` with the same public names and prototypes as
   the module's `.H`.
3. Add a differential test in `port/tests/` that runs the original routine (from the
   reference build's `.OBJ`) and the C version on the same inputs and compares the results.
   Hardware-facing modules (keyboard, mouse, timer, VGA ports) are replaced by the platform
   layer instead of being translated.

### Touching game code under `bak/SRC`

- Keep the original control flow unless the change is the point of the commit.
- Mind 16-bit semantics. `s16`/`u16` operands promote to a 32-bit `int` natively, so
  `a - b` on two `u16` can go negative where the original wrapped, and `~x` or `x >> n` can
  differ. Cast back to the 16-bit type when the original relied on wraparound.
- Structs that are read from game files or written to saves need fixed layouts. Use
  explicit widths, and serialize explicitly anything that contains a pointer.
- Names and documentation come from evidence: read the code and the data before naming
  something. A plausible wrong name is worse than a placeholder.

### Enhancements

Every enhancement is a runtime option, with "Original" as the default that stays exact. Keep
enhancement code in `port/` where possible, and hook into game code at a single, named point.

## Verification

- Native build and tests in CI on every push.
- `reference.yml` (manual) builds any ref with the period Borland toolchain and verifies the
  binaries by sha256. Use it to prove a change to `bak/` is byte-neutral when it should be.
  Its uploaded objects feed the differential tests.
- When behaviour is in doubt, compare with the original EXE from your own copy of the game
  in DOSBox Staging.

## Commits

Small commits with one concern each. Mechanical sweeps never share a commit with behaviour
changes. The message is an emoji, a short imperative subject, and a body that says *why*:

| | | | |
|---|---|---|---|
| 🏗️ | structural, build, toolchain | 🐛 | bug fix |
| 🖥️ | video / rendering | 🔊 | sound / music |
| 🎮 | input, controls | ✅ | tests |
| 🚚 | ASM module ported to C | 🔍 | naming / typing / RE findings |
| 📝 | documentation | 🎨 | style, formatting, no behaviour change |
| 🧹 | cleanup, dead code removal | 📦 | dependencies |
