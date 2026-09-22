# toy-mlir architecture

A rebuild of the [MLIR Toy tutorial](https://mlir.llvm.org/docs/Tutorials/Toy/)
as one out-of-tree compiler, organized by concern rather than by chapter.

The tutorial ships seven executables, `toyc-ch1` through `toyc-ch7`, each a copy
of the previous chapter's source with the next feature added. That works for
reading the chapters in order, and it means the code exists in seven versions:
21,637 lines of C++ and ODS under `reference/`, in which `Lexer.h` has only three
distinct contents across the seven copies and `parser/AST.cpp` only two. A change
to the lexer is a change to seven files.

Here the same language is one binary, 6,776 lines of code and ODS. The pipeline
depth is a flag rather than a build target, so `-emit=mlir` and `-emit=jit` run
the same front end and the same dialect.

Because the language is unchanged, the rebuild is checkable: `toyc` is required
to produce byte-identical output to `toyc-ch7` at every stage, and the
differences it is allowed to have are enumerated below and enforced by
`tests/compat`.

## What each chapter contributed

Toy's chapters are cumulative, so Ch7 already has everything the earlier ones do.
This differs from the Kaleidoscope tutorial, where later chapters drop the JIT
and the optimizer, and a single binary supporting all of it is new. The value
here is in the organization and in what sits past Ch7.

| Capability | Upstream chapter | Here |
| --- | --- | --- |
| Lexer, parser, AST dump | Ch1 | `-emit=ast` |
| Toy dialect, ODS, MLIR emission | Ch2 | `-emit=mlir` |
| Canonicalization, C++ and DRR patterns | Ch3 | `-opt` |
| Inliner, shape inference, interfaces | Ch4 | `-opt` |
| Partial lowering to affine | Ch5 | `-emit=mlir-affine` |
| LLVM lowering, translation, JIT | Ch6 | `-emit=mlir-llvm`, `-emit=llvm`, `-emit=jit` |
| `!toy.struct` | Ch7 | all stages |
| Object emission | none | `-c` |
| DWARF naming the Toy source | none | `-g` |
| Pipeline introspection | none | `--print-pipeline` |
| Cross-target code generation | none | `--target=`, `-mattr=`, `-mabi=` |

## Module map

Four static libraries, layered so that the dependencies are visible in
`src/CMakeLists.txt` rather than implied by a single link line.

```
  toyc          main.cpp
                   │
                   ▼
  ToyCodegen    MLIRGen.h   Pipeline.h   Translate.h   ObjectEmitter.h
                   │            │         DebugInfo.h      Jit.h
                   │            ▼
  ToyPasses        │        Passes.h
                   │            │
                   ▼            ▼
  ToyDialect    Dialect.h ◄── Ops.td, ShapeInferenceInterface.{h,td}
                   │
                   ▼
  ToyFrontend   Parser.h   ASTDumper.h
                     ╲       ╱
                      AST.h ◄──── ASTVisitor.h
                        │
                     Lexer.h
```

Nothing points upward. `ToyFrontend` has no idea MLIR exists, which lets
`tests/FrontendTests` link it on its own; `ToyDialect` knows nothing about
lowering; `ToyPasses` knows nothing about the driver.

The front end's independence is checked rather than asserted. `ldd` on the unit
test binary lists `libstdc++`, `libgcc_s`, `libz`, `libzstd` and libc, with no
MLIR library, and `nm -C build/src/libToyFrontend.a | grep -c 'mlir::'` returns
0. `AST.h` includes `llvm/Support/Casting.h` for `isa<>`/`dyn_cast<>` over its
own `Kind` discriminator, and nothing else from LLVM.

| Library | Files | Responsibility |
| --- | --- | --- |
| `ToyFrontend` | `Lexer.cpp` 130, `Parser.cpp` 672, `ASTDumper.cpp` 427 | Source text to AST, plus both dump formats |
| `ToyDialect` | `ToyDialect.cpp` 79, `StructType.cpp` 183, `Ops.cpp` 501, `Folders.cpp` 64, `Interfaces.cpp` 131, `ToyCombine.cpp` 89 | The Toy IR: operations, `!toy.struct`, verifiers, folders, interfaces, canonicalization patterns |
| `ToyPasses` | `ShapeInference.cpp` 128, `LowerToAffine.cpp` 389, `LowerToLLVM.cpp` 244 | The three passes Toy writes for itself |
| `ToyCodegen` | `MLIRGen.cpp` 694, `Pipeline.cpp` 92, `Translate.cpp` 94, `DebugInfo.cpp` 276, `ObjectEmitter.cpp` 109, `Jit.cpp` 75 | AST to IR, the pass pipeline, and every exit from MLIR |
| `toyc` | `main.cpp` 533 | Options, input loading, and the stage dispatch |

`Ops.td` (498 lines) plus `ShapeInferenceInterface.td` (38) and `ToyCombine.td`
(72) generate 6,714 lines of C++ through mlir-tblgen, which is why the dialect
costs about a thousand hand-written lines.

## Data flow

```
 .toy ──► Lexer ──► Parser ──► AST ──► MLIRGen ──► Toy dialect
                                │                       │
                          ASTDumper                      │  inline, canonicalize,
                          (-emit=ast)                    │  shape inference, CSE
                                                         ▼
 .mlir ─────────────────────────────────────────► Toy dialect (-emit=mlir)
                                                         │  toy-to-affine
                                                         ▼
                              affine + arith + memref + toy.print (-emit=mlir-affine)
                                                         │  toy-to-llvm
                                                         ▼
                                            LLVM dialect (-emit=mlir-llvm)
                                                         │  Translate
                                                         ▼
                                              llvm::Module (-emit=llvm)
                                                    ╱         ╲
                                        ObjectEmitter          Jit
                                            (-c)             (-emit=jit)
```

`-x mlir` enters at the second arrow, so any stage's output can be fed back in.

## Design decisions

### One tree walk, shared by both consumers

Upstream writes the same nine-way switch over `ExprAST` kinds twice: once in
`parser/AST.cpp` to print the tree, once in `mlir/MLIRGen.cpp` to emit IR. Adding
a node kind means editing both.

The switch lives once in `ASTVisitor::visit` (`include/toy/ASTVisitor.h`), a CRTP
template whose return type is a parameter, and the two consumers instantiate it
differently:

```cpp
class ASTDumper : public ASTVisitor<ASTDumper>                        // void
class MLIRGenImpl : public ASTVisitor<MLIRGenImpl,
                                     mlir::FailureOr<mlir::Value>>    // value or error
```

Dispatch is not virtual, so `MLIRGen` returns a value-or-error while the dumper
returns nothing, and the AST nodes carry no `accept()` and no knowledge of either
one. The switch has no `default:`, so a new `ExprASTKind` makes `-Wswitch` fire in
one file, at the `llvm_unreachable` that closes `ASTVisitor::visit`, and every
visitor then fails to compile until it handles the case.

The cost is the Expression Problem, from the other side. The node set is closed
and operations over it are open: adding `ASTDumper` was free, adding a node kind
touches every visitor. MLIR itself makes the opposite trade, which is worth
noticing while reading both halves of this repo. A dialect can add an operation
without touching the passes that traverse it, and a pass asks each operation what
it can do through an interface (`ShapeInferenceInterface.td`) instead of switching
on a kind. The front end is a closed language; the IR is an open one.

### The pipeline is a value the driver configures

Upstream builds the pass pipeline inline in `toyc.cpp`, interleaved with argument
parsing and file loading, and each chapter's copy differs slightly. Here it is
`buildPipeline(PassManager &, const PipelineOptions &)` in `src/Pipeline.cpp`.

The `Stage` enum in `include/toy/Pipeline.h` is ordered, and each stage is a
superset of the one before, so the gating is comparisons rather than a switch:

```cpp
const bool isLoweringToAffine = opts.stage >= Stage::MLIRAffine;
const bool isLoweringToLLVM = opts.stage >= Stage::MLIRLLVM;
```

That is the same shape as upstream's comparisons on its `Action` enum, and it is
why `Stage::Object` and `Stage::JIT` need no separate handling: both sort above
`MLIRLLVM` and so get the whole pipeline. Pulling it out of the driver also makes
it inspectable, and `--print-pipeline` prints the result:

```
$ toyc prog.toy -emit=mlir --print-pipeline
builtin.module()
$ toyc prog.toy -emit=mlir-affine -opt --print-pipeline
builtin.module(inline{...},toy.func(canonicalize{...},toy-shape-inference,
canonicalize{...},cse),toy-to-affine,func.func(canonicalize{...},cse,
affine-loop-fusion{...},affine-scalrep))
```

### The dialect owns its canonicalization patterns

`getCanonicalizationPatterns` is an operation hook: `hasCanonicalizer = 1` in
`Ops.td` declares it, and ODS generates a call to it from the operation's
registration. Putting the definitions in `ToyPasses`, where upstream's
`ToyCombine.cpp` sits next to the passes, made `libToyDialect.a` and
`libToyPasses.a` mutually dependent, and the link failed:

```
libToyDialect.a(ToyDialect.cpp.o): undefined reference to
  `mlir::toy::ReshapeOp::getCanonicalizationPatterns(...)'
```

Static archives are searched in order, so a cycle between two of them has no
correct order. The patterns moved to `src/dialect/ToyCombine.cpp`, which is where
they belong anyway: the patterns are the dialect's own semantics, and the pass
that applies them is MLIR's generic canonicalizer, which this repo does not write.
`ToyPasses` then depends on `ToyDialect` in one direction only.

### Registration hooks for types and interfaces

The dialect's `extraClassDeclaration` in `include/toy/Ops.td` adds two methods to
the generated dialect class:

```cpp
void registerTypes();
void registerInterfaces();
```

`ToyDialect::initialize()` (`src/dialect/ToyDialect.cpp`) registers the
operations itself and calls these two. `registerTypes()` is defined in
`src/dialect/StructType.cpp`, because `addTypes<StructType>()` instantiates
`StorageUniquer` machinery that needs the complete `StructTypeStorage`, and that
storage class is private to that file. `registerInterfaces()` is defined in
`src/dialect/Interfaces.cpp` for the same reason, keeping
`ToyInlinerInterface` in an anonymous namespace.

Upstream never meets either problem, because its single `Dialect.cpp` has both
definitions in scope. `addInterfaces()` is public where `addTypes()` is
protected, so the interface case could have used a free function; one hook each
is better than a declaration duplicated across two files.

### The ShapeInference interface belongs to the dialect

Upstream compiles `ShapeInferenceOpInterfaces.cpp.inc` into
`mlir/ShapeInferencePass.cpp`. The interface is part of the dialect's contract,
so here it is included from `src/dialect/Interfaces.cpp` and
`ShapeInference::inferShapes()` lives in `libToyDialect.a`. The dialect links
without the passes, which makes the layering above real rather than nominal.

### Textual output goes to stderr

Upstream writes every textual stage to stderr, including `-emit=llvm`. `toyc`
does the same, because the acceptance gate is byte-level equality of what the two
compilers print; `-o` redirects to a file, through the `TextOutput` class in
`src/main.cpp`. A reader who expects stdout should know this before piping.

## Deviations from upstream

These thirteen are the complete set. Everything else is required to match
`toyc-ch7` byte for byte, and `tests/compat` fails on anything not listed here.

| | Deviation | Upstream behavior | Why |
| --- | --- | --- | --- |
| D1 | The lexer reports a malformed number such as `1.23.45` and the driver exits 8 | Lexes it as one token, `strtod` keeps the `1.23` that parsed, discards the rest, exits 0 | Silent truncation of a numeric literal is the kind of bug that costs an afternoon |
| D2 | A binary expression's location is the operator's | Reads the location after consuming the operator, so it records where the right-hand side starts | The location of `*` should be the column of `*` |
| D3 | A `var` whose initializer failed to parse propagates the failure, naming the initializer | Stores a null initializer and exits 0; the error surfaces two stages later, and `toyc-ch1`'s dumper aborts on it | A parse error should be reported where it happens |
| D4 | A redeclared variable gets a diagnostic at the second declaration | `MLIRGen::declare` returns failure with no diagnostic, so the function silently vanishes from the output | A compile that fails should say why |
| D5 | The structural split described above | Seven binaries, each a copy of the last | |
| D6 | `-c`, `-g`, `--dump-ast-style`, `--target=`, `-mattr=`, `-mabi=`, `--print-pipeline` | Stops at the JIT | An object file can be linked, inspected, and run outside the compiler |
| D7 | `var a;` and `def f(1)` are parse errors | Calls `consume(Token('='))` or `getId()` without checking the token, and the assertion inside aborts the process, exit 134 | A one-token typo should not crash the compiler |
| D8 | A module with no `main` reports `JIT invocation failed: Symbols not found: [ _mlir_main ]` and exits 255 | Prints `JIT invocation failed`, then aborts on an unconsumed `llvm::Error`, exit 134 | See below |
| D9 | A byte at or above 0x80 is an ordinary character | Reads it as a signed `char`; 0xFF equals `EOF`, so the rest of the file is dropped and the compile exits 0 | Input past a stray byte should not vanish |
| D10 | Verifiers reject a reshape that changes the element count, element-wise operands of different shapes, and a call with the wrong number of arguments | No such verifiers: the reshape aborts under `-opt`, `[1,2,3] + [1,2]` reads past the end of the shorter operand, and a bad call surfaces as a shape inference failure | Invalid IR should fail where it is built |
| D11 | A one-element constant reshaped to a larger shape is broadcast (`var a<2, 2> = 5.5;`) | Aborts inside `DenseElementsAttr::reshape` under `-opt` and at every lowered stage | The tutorial's own `scalar.toy` should compile |
| D12 | A `print` that fails to generate fails the compile | Prints the diagnostic and exits 0 | A compile that reports an error should not succeed |
| D13 | Calling a function that returns nothing is a diagnostic | Indexes the callee's empty result list and aborts, exit 134 | Same as D7 |

D2 reaches further than it looks. Upstream's binop location equals its right-hand
side's, so the two share one `!DILocation` and LLVM emits a single metadata node.
With the operator's own column they are distinct, one extra node appears, and
every later `!dbg` renumbers, which turns a two-column difference into a diff
hundreds of lines long whose instruction stream is identical.
`tests/compat/EXPECTED-DIFFS.md` has the measured evidence.

D8 comes from a C++ lifetime rule. `llvm::Error` aborts the process
when it is destroyed having been tested but never consumed. Upstream binds it to
a named variable, so its diagnostic prints before the abort fires at scope exit.
Writing the same test on the temporary, `if (engine->invokePacked("main"))`,
destroys the `Error` at the end of the condition and the abort happens before the
diagnostic is reached. `src/Jit.cpp` binds it and consumes it with
`llvm::toString`, which reports the cause and exits normally.

D4 has a related defect this repo reproduces on purpose. Upstream's own
diagnostics repeat the prefix that `emitError` already prints, so `toyc-ch7`
says:

```
loc("prog.toy":2:9): error: error: unknown variable 'undefined_var'
```

Those strings are compared byte for byte, so they keep the doubling. D4's message
is this repo's own and does not double it (`MLIRGenImpl::declare` in
`src/MLIRGen.cpp`).

## Equivalence as the acceptance gate

This repo claims its output matches the reference compiler's, which is a stronger
and more specific claim than a passing test suite. The check runs the way an
equivalence check runs against a golden model: over a generated corpus, at every
stage, in both optimization settings.

`tests/compat/gen-programs.py` generates valid Toy programs, tracking shapes as
it goes so that reshapes match element counts and operands agree, and rejecting
anything upstream will not compile. Its rejection rate is 0% at 25, 200 and 1500
programs. `tests/compat/compare-upstream.sh` then runs both compilers over that
corpus and over `reference/tests/**` at all six stages with and without `-opt`,
comparing merged stdout and stderr, plus stdout alone for `-emit=jit`.

Two things are normalized, and nothing else:

* `argv[0]` where it prefixes a diagnostic, since it is necessarily `toyc` for us
  and `toyc-ch7` upstream. Matched only at the start of a line followed by `": "`.
* At `-emit=llvm` and `-emit=mlir-llvm`, debug-metadata numbering and the
  `column:` field of `!DILocation`, for the D2 cascade described above. The
  instruction stream must still match exactly, and so must the set of distinct
  `!DILocation` nodes, so a changed line number, a changed scope, or any
  instruction difference still fails.

Nothing is skipped. A program upstream itself cannot compile is compared anyway,
because reproducing an upstream failure faithfully is an equivalence result. The
one upstream abort not reproduced is D11: `reference/tests/**/scalar.toy`
reshapes a rank-0 tensor to `2x2`, upstream aborts inside
`DenseElementsAttr::reshape` at the nine stage and opt combinations that reach
it, and this compiler broadcasts the value instead. The sweep admits that
difference only when upstream's output is that exact assertion. The summary
prints `NOT COMPARED: 0`.

Measured, on the build in `build/`:

| Corpus | Comparisons | Identical | D2 | D8 | D11 | Unexpected | Skipped |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 240 programs (ctest default) | 2880 | 2270 | 542 | 14 | 54 | 0 | 0 |
| 1540 programs, seed 424242 | 18480 | 14680 | 3732 | 14 | 54 | 0 | 0 |

A gate that never fails proves nothing, so it was checked against deliberate
breakage. A mutant that renames `toy.mul` to `toy.BOGUS` fails the sweep, while
upstream compared against itself produces no differences at all. The exact count
depends on which corpus you run, so `docs/13-testing-and-equivalence.md` gives a
figure together with the command that reproduces it rather than a bare number.

The error paths D1, D3, D4, D7, D9, D10, D12 and D13 are diagnostics a valid
program never reaches, so they live in `tests/compat/fixtures/` with their diffs
recorded under `tests/compat/expected/`, which makes a change in our own
diagnostics a test failure rather than a silent edit.

## Reading order

`docs/` walks the code in the order the upstream tutorial introduces it, starting
at `docs/01-lexer.md`. For the IR itself, `include/toy/Ops.td` is the shortest
path to understanding what the dialect is, and `src/Pipeline.cpp` to how the
stages fit together.
