# 13. Testing, and equivalence against the reference

Files: `tests/FrontendTests.cpp`, `tests/CMakeLists.txt`, `tests/RunToy.cmake`,
`test/**` (24 lit tests), `tests/compat/**`.

## Equivalence against a golden reference

This repo is a reimplementation of something that already works. The upstream
tutorial's `toyc-ch7` is a golden reference, so the question worth answering is
not "do my tests pass" but "does my compiler produce the same output as the
reference, on every input, at every stage".

That is equivalence checking. The hardware analogue is formal equivalence between
RTL and the synthesized netlist: you are not re-verifying the design's intent, you
are proving the transformation preserved behavior. Unit tests and lit tests catch
the bugs you thought of; the equivalence sweep catches the ones you did not.

Three layers, weakest claim to strongest:

| Layer | What it proves | Size |
| --- | --- | --- |
| Unit tests | the front end behaves as specified in isolation | 63 checks |
| lit + FileCheck | each stage emits the IR we intend | 24 files, 48 RUN lines |
| Differential sweep | the whole compiler equals `toyc-ch7` | 18,480 comparisons |

```console
$ ctest --test-dir build
...
100% tests passed, 0 tests failed out of 7
Total Test time (real) =  12.16 sec
```

The seven: `frontend_unit`, `lit`, `jit_transpose`, `jit_transpose_opt`,
`error_redeclaration`, `compat`, `compat_fixtures`.

## Layer 1: unit tests over the front end alone

`tests/FrontendTests.cpp` is a plain `main()` with assert-style checks, no gtest.
It links `ToyFrontend` and nothing else, which is how one of this repo's design
claims gets checked mechanically:

```console
$ build/tests/FrontendTests | tail -1
PASSED: 63 checks, 0 failures

$ ldd build/tests/FrontendTests | grep -ci mlir
0
```

Zero MLIR libraries. `AST.h` really is independent of the IR, so the claim in
[02-ast.md](02-ast.md) is enforced at link time rather than by inspection.

Coverage: token kinds and keywords, identifiers containing `_`, `#` comments, token
line/column values, deviation D1's malformed-number diagnostic and `hadError()`,
shape declarations, tensor literal dimension recovery, struct definitions,
operator precedence, `.` member access, D2's operator location, D3's error
propagation, and the exact text of a representative parse error.

To add one, write a function in `tests/FrontendTests.cpp`, call it from `main`, and
use the existing check macros. No CMake change needed.

## Layer 2: lit and FileCheck

This is MLIR's own house style: `mlir/test/**` works exactly this way, and so do
the vendored `reference/tests/**`. A test is a source file carrying both its
command line and its expectations.

```toy
# RUN: %toyc %s -emit=mlir -opt 2>&1 | %filecheck %s

def transpose_transpose(x) {
  return transpose(transpose(x));
}

def main() {
  var a<2, 3> = [[1, 2, 3], [4, 5, 6]];
  var b = transpose_transpose(a);
  print(b);
}

# CHECK: module {
# CHECK-NEXT:   toy.func @main() {
# CHECK-NEXT:     %0 = toy.constant dense<{{\[\[}}1.000000e+00, ...]]> : tensor<2x3xf64>
# CHECK-NEXT:     toy.print %0 : tensor<2x3xf64>
# CHECK-NEXT:     toy.return
```

`test/opt/transpose-transpose.toy`. The mechanics:

- `RUN:` lines are shell commands lit executes, with substitutions expanded.
- `CHECK:` matches a line anywhere after the previous match. `CHECK-NEXT:` requires
  the immediately following line, and `CHECK-LABEL:` splits the file into
  independent blocks, so a mismatch is reported near where it happened rather than
  cascading.
- `{{...}}` is a regex escape, needed here because `[[` is FileCheck's variable
  syntax.
- Every `RUN` line pipes `2>&1`, because MLIR dumps go to stderr
  ([11](11-driver-and-pipeline.md)).

Substitutions come from `test/lit.cfg.py`: `%toyc`, `%toyc-upstream`,
`%filecheck`, `%clang`, `%readelf`, `%dwarfdump`. Two details in that file worth
knowing:

- `%toyc-upstream` is registered before `%toyc`, because substitutions apply in
  sequence and the shorter name would otherwise rewrite the longer one's prefix.
- `config.test_format = lit.formats.ShTest()` with no `execute_external=True`:
  LLVM 24 refuses that argument, so RUN lines must stay within lit's internal
  shell (pipelines, redirects and `&&` are fine; shell builtins are not).

Tests that need a linker or a DWARF reader are guarded with `available_features`,
so the suite degrades to "skipped" rather than "failed" on a machine with only
MLIR.

The 24 files by area: `frontend/` (6) AST dumps in both styles, the D1 and D3
diagnostics, `-emit=ast` refused for MLIR input; `dialect/` (5) IR generation,
verifier rejection, round-trip, D4; `opt/` (3) transpose folding, reshape DRR
rules, inline + shape inference; `lowering/` (5) affine with and without fusion,
LLVM dialect, LLVM IR, JIT output; `driver/` (5) `--print-pipeline`, object
emission, link-and-run, the DWARF line table, bad flag combinations.

To add one, drop a `.toy` or `.mlir` file into the right `test/` subdirectory
with its own `RUN:` and `CHECK:` lines. No CMake change, no rebuild of the suite.
Run one file with
`~/dev/08_mlir_toy/build/bin/llvm-lit -v build/test/opt/transpose-transpose.toy`.

## Layer 3: the differential sweep

`tests/compat/` is the layer that makes the equivalence claim.

### Generating a corpus

`reference/tests/**` has 40 programs. That is far too few, so
`tests/compat/gen-programs.py` produces valid ones on demand, reproducibly
(`--seed`, `--count`).

"Valid" is the hard part, and the generator's header is explicit about why: Toy has
no type checker worth the name. A program with mismatched shapes parses, survives
MLIRGen, and then fails in shape inference, on which both compilers would agree,
making it a useless differential input. So the generator tracks shapes as it
builds rather than generating freely and repairing afterwards:

- f64 tensors of rank 1 or 2 only;
- `+` and `*` need identical operand shapes;
- transpose reverses a rank-2 shape;
- a declaration with an explicit shape reshapes, so element counts must match;
- every call site of a user function must pass the same shape, since parameters
  are unranked;
- structs are initialized with a literal and accessed directly, because
  `struct_access(struct_constant)` folding is what makes their members concrete;
- `print()` takes a tensor; `main()` takes nothing and returns nothing.

Every candidate is then compiled by upstream before being kept, so a mistake in
those rules shows up as a high skip rate rather than as a corpus that quietly
tests nothing.

### Sweeping

`tests/compat/compare-upstream.sh` runs both compilers over the corpus at six
stages × two `-opt` settings, and fails on any difference it cannot attribute to a
documented deviation.

What it normalizes, and nothing more:

- the input path;
- the compiler's own name where it prefixes a diagnostic, since `argv[0]` is
  necessarily `toyc` for us and `toyc-ch7` upstream. It is matched only at the start
  of a line followed by `": "`;
- debug-metadata numbering at the LLVM stages, described below. The instruction
  stream must still match exactly.

No value, type, symbol, SSA number or diagnostic text is ever rewritten.

### The one real finding: D2 reaches `-emit=llvm`

Deviation D2 gives a binary operator the operator's own location instead of its
right-hand side's, and it was expected to show only in `-emit=ast`. It also shows
in LLVM IR, and the mechanism is worth presenting.

Upstream's binop location *equals* its right-hand side's, so both share one
`!DILocation` and LLVM emits a single node. With the operator's own column they
differ, so an extra node appears and every later `!dbg` renumbers. One
program's raw diff was 525 lines that collapsed to a single extra `!DILocation`
once numbering was normalized; the instruction stream was byte-identical.

So the gate normalizes metadata numbering and masks `column:` at
`-emit=llvm`/`mlir-llvm`, then requires both that the instruction stream match
exactly and that the set of distinct `!DILocation` nodes match. A changed line,
scope or instruction still fails.

### What it admits, by name

Read `tests/compat/EXPECTED-DIFFS.md` for the measured evidence. In summary:

| Class | Where | Why |
| --- | --- | --- |
| D2 | `-emit=ast` columns; `!DILocation` at the LLVM stages | the operator-location fix |
| D8 | `-emit=jit` on a module with no `main` | upstream aborts on an unconsumed `llvm::Error`; we report the cause and exit |
| D1, D3, D4 | fixtures only | diagnostics a valid program never triggers |

At `mlir` and `mlir-affine` the requirement is byte-identical output.

### Nothing is skipped

An early version excluded programs upstream itself cannot compile. That hid real
parity, so it was removed. `reference/tests/**/scalar.toy` is the interesting case:
it reshapes a rank-0 tensor to `2x2`, tripping an assertion inside MLIR itself.

Both compilers abort at the same file and line in `DenseElementsAttr::reshape`,
with the same text and exit code 134, at all nine stage/opt combinations that reach
it. Once `argv[0]` is normalized the outputs are identical, so these count as 54
passes. Reproducing an upstream crash faithfully is an equivalence result, not a
reason to look away. The summary prints `NOT COMPARED: 0`.

### Measured results

Default (what ctest runs, ~12 s):

```
compared 240 programs (40 from reference/tests, 200 generated with --seed 20260921)
  stages:        ast mlir mlir-affine mlir-llvm llvm jit
  opt settings:  none, -opt
  comparisons:   2880
    identical:   2324
    D2-only:     542
    D8-only:     14
    unexpected:  0
    NOT COMPARED: 0
  fixtures:      6 checked, 0 unexpected
RESULT: PASS -- output is equivalent to upstream except deviation D2
```

Deep run:

```console
$ time bash tests/compat/compare-upstream.sh --count 1500 --seed 424242
  comparisons:   18480
    identical:   14734
    D2-only:     3732
    D8-only:     14
    unexpected:  0
    NOT COMPARED: 0
RESULT: PASS
real	1m21.804s
```

### Validating the gate itself

A test suite that cannot fail proves nothing. The exemptions above are broad
enough to be worth attacking, so: stand in a mutant for our compiler that renames
`toy.mul` in its output, and check the gate notices.

Build the mutant under `build/`, as a wrapper that filters our compiler's output:

```console
$ printf '%s\n' '#!/bin/bash' 'exec > >(sed "s/toy[.]mul/toy.BOGUS/g") 2>&1' 'exec "$PWD/build/bin/toyc" "$@"' > build/mutant.sh && chmod +x build/mutant.sh
```

Then point the sweep at it:

```console
$ TOYC=build/mutant.sh bash tests/compat/compare-upstream.sh --no-random
  comparisons:   480
    identical:   351
    D2-only:     38
    D8-only:     14
    unexpected:  77
    NOT COMPARED: 0
RESULT: FAIL
```

77 unexpected differences and a `FAIL`. The D2 and D8 exemptions did not blunt it.
Worth re-running after any change to the normalization rules.

To add a deviation, it needs an entry in `EXPECTED-DIFFS.md` explaining the
mechanism, a classification rule in `compare-upstream.sh` narrow enough that
nothing else hides behind it, and a re-run of the mutation check to prove the gate
still bites.

To re-record an error-path fixture after deliberately changing a diagnostic, run
`tests/compat/compare-upstream.sh --fixtures-only --update`, then read the diff
before committing it. That flow caught the doubled `error: error:` prefix during
development, because the fixture flagged the change before it was recorded.

## Pitfalls

- `ctest` reports `frontend_unit (Not Run)` if you built only `toyc`. Build
  everything with `ninja -C build`.
- A lit test with no `2>&1` will appear to match nothing, because the output went to
  stderr.
- `llvm-lit` needs `PYTHONPATH` pointing at `llvm/utils/lit`; `tests/CMakeLists.txt`
  sets it, but a hand-run invocation may need it too.
- Do not add a normalization to make a diff go away. Each one narrows the claim
  the sweep makes; the mutation check is what tells you whether it went too far.
