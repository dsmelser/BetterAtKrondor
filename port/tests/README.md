# Differential tests

Every C translation of an original assembly routine is checked against the original
before it replaces its stub. The original runs in [Unicorn](https://www.unicorn-engine.org/)
(16-bit real mode), loaded from the reference build's executable and link map. The
translation runs in `bak_difftest`, a 32-bit build of the port's code. pytest compares the
two.

```
port/tests/
  harness/mzexe.py    MZ executables and TLINK maps
  harness/machine.py  real-mode PC: far/near calls, port and interrupt hooks, test heap
  harness/native.py   client for bak_difftest
  difftest/           bak_difftest: main.c (protocol) and one t_<module>.c per module
  test_<module>.py    the tests
```

## Running

1. Fetch the reference artifacts (the original binaries, objects and maps, built and
   sha256-verified by `.github/workflows/reference.yml`) into `work/ref`:

   ```
   gh run list --workflow reference.yml --status success --limit 1
   gh run download <run id> -n reference-<run id> -D work/ref
   ```

   If no run is listed, or its artifact has expired (they are kept 90 days), start one
   with `gh workflow run reference.yml -f ref=upstream-snapshot`.

2. Build (`port\tools\win-build.cmd`), then run either

   ```
   ctest --preset win-x86-clangcl
   uv run --group difftest pytest -q port/tests
   ```

Environment: `BAK_REF_DIR` (default `work/ref`), `BAK_DIFFTEST_EXE` (default
`build/win-x86-clangcl/bak_difftest.exe`), `BAK_VERSION` (`100` or `102`, default `102`).
Without the reference artifacts or the executable, the tests skip.

## Writing a test

- On the C side, add `t_<module>.c` with a `dt_<module>_entries[]` table: commands that
  call the translation, and `DT_GLOBAL` entries for the state the test copies or compares.
  List the table in `s_modules` in `difftest/main.c` and declare it in `difftest.h`.
- On the Python side, call the original with `Machine.call_far` / `call_near` (stack
  arguments as words, register arguments by name), the translation with `Native.call`,
  and move state with `Machine.read`/`write` and `Native.peek`/`poke`.
- Set the state a test depends on explicitly. The native process lives for the whole
  session; `fresh_native` gives a test its own process when it needs initial values.
- Compare whole outputs and the module's state afterwards, over fixed inputs and over
  seeded random ones.
- Near pointer arguments point into DS. To give a routine data in a segment of its own,
  pass `ds=` to `call_far`; a frameless routine (`MOV BX,SP`, then `[BX+4]`) also reads
  its stack arguments through DS, so pass the same segment as `ss=`, as the game has DS
  equal to SS.
- A routine that calls game C code (`res_fread`, say) can be served from Python: write
  `INT F1h` + `RETF` over the callee's entry and install a handler for 0F1h that reads the
  arguments at SS:SP+4 and sets AX. Natively, give the translation a function pointer the
  C side points at a twin of the same model (`dosrw.c`, `decomp.c`).
- Keep word accesses away from offset FFFFh of a segment: a 286 and later fault there, an
  8086 wraps, and Unicorn reads on past the segment, so no answer is the original's.
