# 11. The driver and the pipeline

Files: `src/main.cpp`, `src/Pipeline.cpp` (115 lines),
`include/toy/Pipeline.h`.

## What this is for

`main.cpp` parses arguments and calls one small function per stage. The
compilation itself lives elsewhere: the pipeline here, the exit from MLIR in
[Translate.cpp](12-debug-info-and-objects.md), the back ends in
`ObjectEmitter.cpp` and `Jit.cpp`.

Pulling the pipeline out of the driver is one of this repo's structural changes.
Upstream builds its passes inline in `toyc.cpp`, interleaved with argument parsing
and file loading, and each chapter has a slightly different copy. Here it is a
function of a value, which is what makes `--print-pipeline` possible.

## The command line

| Option | Meaning |
| --- | --- |
| `<input>` | a `.toy` or `.mlir` file, or `-` for stdin (default `-`) |
| `-x toy\|mlir` | input kind; MLIR is also implied by a `.mlir` extension |
| `-emit=ast\|mlir\|mlir-affine\|mlir-llvm\|llvm\|jit` | how far to compile |
| `-opt` | enable the optimizations that are not required for correctness |
| `-o <file>` | write output to a file instead of stderr |
| `-c` | emit a native object file (implies the LLVM stage) |
| `--emit-llvm` | with `-c`, also write the LLVM IR beside the object |
| `-g` | attach debug info, so the object carries a DWARF line table |
| `--dump-ast-style=toy\|kaleidoscope` | format for `-emit=ast` |
| `--target=<triple>` `-mattr=<features>` `-mabi=<abi>` | object emission knobs |
| `--print-pipeline` | print the pass pipeline that would run, then exit |

Plus whatever `mlir::registerAsmPrinterCLOptions`,
`registerMLIRContextCLOptions` and `registerPassManagerCLOptions` add, which is
where `--mlir-print-ir-after-all` and `--mlir-pass-statistics` come from.

### Output goes to stderr

Worth knowing before you redirect anything: with no `-o`, every textual stage
writes to stderr, including `-emit=llvm`. That is upstream's behavior
(`reference/Ch7/toyc.cpp` prints with `llvm::errs()`), kept because byte-identical
output is this repo's acceptance gate. Capture with `2>&1`, which is why every
`RUN:` line in `test/` does.

### Exit codes

`src/main.cpp:14-31` documents them. Upstream's first, kept so scripts written
against `toyc-ch7` behave the same:

| | |
| --- | --- |
| 0 | success |
| 1 | MLIR generation from the AST failed |
| 3 | the input `.mlir` could not be parsed |
| 4 | the pass manager rejected its options or a pass failed |
| 5 | `-emit=ast` asked for, but the input is MLIR |
| 6 | the Toy source could not be parsed |
| −1 | translation to LLVM IR failed, the JIT failed, or no action given |

and the ones this driver adds:

| | |
| --- | --- |
| 7 | contradictory options |
| 8 | a token was malformed (deviation D1) |
| 9 | object emission failed |
| 10 | the output file could not be opened |

`validateOptions` (`src/main.cpp:276-307`) is what produces code 7. It rejects
combinations that cannot mean anything, rather than letting one side be silently
ignored: `-c` with `-emit=mlir`, `--dump-ast-style` without `-emit=ast`,
`--emit-llvm` without `-c`, `-g` before the LLVM stage, `--target=` without `-c`.

## The ordered Stage enum

```c++
enum class Stage {
  AST,        ///< Parse only, print the tree. No MLIR involved.
  MLIR,       ///< Toy dialect. With -opt: canonicalize, inline, infer shapes.
  MLIRAffine, ///< Affine + arith + memref + the surviving toy.print.
  MLIRLLVM,   ///< The LLVM dialect, nothing else.
  LLVMIR,     ///< llvm::Module, out of MLIR entirely.
  Object,     ///< A native .o, via TargetMachine.
  JIT,        ///< Compiled and run in this process.
};
```

`include/toy/Pipeline.h`. The order is load-bearing. Each stage is a superset of
the previous one, so the pipeline is built with comparisons rather than a switch:

```c++
const bool isLoweringToAffine = opts.stage >= Stage::MLIRAffine;
const bool isLoweringToLLVM = opts.stage >= Stage::MLIRLLVM;
```

`src/Pipeline.cpp:52-53`. Asking for an object file or the JIT runs the affine
lowering too, because `Object` and `JIT` sort above `MLIRLLVM`. Upstream does the
same thing with its `Action` enum; naming the enum `Stage` and giving it a
`getStageName` (`:27-45`) is the only difference.

## The pass order

```c++
if (opts.enableOpt || isLoweringToAffine) {
  pm.addPass(mlir::createInlinerPass());

  mlir::OpPassManager &optPM = pm.nest<mlir::toy::FuncOp>();
  optPM.addPass(mlir::createCanonicalizerPass());
  optPM.addPass(mlir::toy::createShapeInferencePass());
  optPM.addPass(mlir::createCanonicalizerPass());
  optPM.addPass(mlir::createCSEPass());
}

if (isLoweringToAffine) {
  pm.addPass(mlir::toy::createLowerToAffinePass());

  mlir::OpPassManager &optPM = pm.nest<mlir::func::FuncOp>();
  optPM.addPass(mlir::createCanonicalizerPass());
  optPM.addPass(mlir::createCSEPass());

  if (opts.enableOpt) {
    optPM.addPass(mlir::affine::createLoopFusionPass());
    optPM.addPass(mlir::affine::createAffineScalarReplacementPass());
  }
}

if (isLoweringToLLVM) {
  pm.addPass(mlir::toy::createLowerToLLVMPass());
  if (opts.debugInfo)
    pm.addPass(createAttachDebugInfoPass());
  else
    pm.addPass(mlir::LLVM::createDIScopeForLLVMFuncOpPass());
}
```

`src/Pipeline.cpp:58-109`. Five things to notice.

Lowering implies the Toy-level work (`:58`). The condition is
`enableOpt || isLoweringToAffine` rather than `enableOpt` alone, because the
lowering patterns need static shapes, and shapes only become static after inlining
and inference. You cannot lower an unoptimized Toy program.

Inlining is first, and at module scope (`:62`). It is what makes everything after
it intraprocedural: one function with known argument types instead of N generic
ones.

Canonicalize runs before inference (`:70`), so reshape folding has already removed
operations whose shapes would otherwise need inferring.

CSE at `:75` collapses the tutorial's two identical transposes into the single
`mul %1, %1` the documentation shows. Without it the IR is correct but does not
match the published output.

Anchors change after the affine lowering (`:83`). The first nest is on
`toy::FuncOp`, the second on `func::FuncOp`, because `toy.func` no longer exists by
then.

### Why nesting matters

`pm.nest<toy::FuncOp>()` says *run these passes once per `toy.func`*. Two
consequences:

- The pass never walks the module itself; it gets one function and runs on it.
- Because `toy.func` is `IsolatedFromAbove` (`include/toy/Ops.td:167-168`), the
  pass manager knows the functions cannot reference each other's values and can
  schedule them in parallel.

That trait is the enabling condition. A region that could reference values from
outside would have to be processed serially.

### Debug info, and byte-identity

`:99-108` is the one place the pipeline deviates. Upstream always appends
`DIScopeForLLVMFuncOpPass`, so that is what runs without `-g`, which keeps
`-emit=mlir-llvm` byte-identical to upstream's. With `-g`, our own pass takes its
place. See [12](12-debug-info-and-objects.md).

## Try it

`--print-pipeline` compiles nothing, so it is the cheapest check that the pipeline
is what you think:

```console
$ build/bin/toyc reference/tests/Ch5/codegen.toy -emit=mlir --print-pipeline
builtin.module()
```

Nothing at all: `-emit=mlir` without `-opt` runs no passes. With `-opt`:

```console
$ build/bin/toyc reference/tests/Ch5/codegen.toy -emit=mlir -opt --print-pipeline
builtin.module(inline{...},toy.func(canonicalize{...},toy-shape-inference,canonicalize{...},cse))
```

The nesting is visible in the syntax: `toy.func(...)` holds the passes that run
per function. Going further down:

```console
$ build/bin/toyc reference/tests/Ch5/codegen.toy -emit=mlir-affine -opt --print-pipeline
builtin.module(inline{...},toy.func(canonicalize{...},toy-shape-inference,canonicalize{...},cse),
               toy-to-affine,
               func.func(canonicalize{...},cse,affine-loop-fusion{...},affine-scalrep))
```

Two nests with different anchors, exactly as the source says. And the full
pipeline, which also shows what runs without `-g`:

```console
$ build/bin/toyc reference/tests/Ch5/codegen.toy -emit=jit --print-pipeline
builtin.module(inline{...},toy.func(canonicalize{...},toy-shape-inference,canonicalize{...},cse),
               toy-to-affine,func.func(canonicalize{...},cse),
               toy-to-llvm,ensure-debug-info-scope-on-llvm-func{emission-kind=LineTablesOnly})
```

(Pass options abridged as `{...}`; the real output prints them in full.)

### Watching it run

```console
$ build/bin/toyc reference/tests/Ch5/codegen.toy -emit=mlir-affine -opt --mlir-print-ir-after-all 2>&1
```

The pass sequence, extracted from the dump headers. The `<-` note on the first
line is added here, not printed by the compiler:

```
IR Dump After CanonicalizerPass: canonicalize          <- on the callee
IR Dump After CanonicalizerPass: canonicalize
IR Dump After CanonicalizerPass: canonicalize
IR Dump After InlinerPass: inline
IR Dump After CanonicalizerPass: canonicalize
IR Dump After (anonymous namespace)::ShapeInferencePass: toy-shape-inference
IR Dump After CanonicalizerPass: canonicalize
IR Dump After CSEPass: cse
IR Dump After (anonymous namespace)::ToyToAffineLoweringPass: toy-to-affine
IR Dump After CanonicalizerPass: canonicalize
IR Dump After CSEPass: cse
IR Dump After AffineLoopFusion: affine-loop-fusion
IR Dump After AffineScalarReplacement: affine-scalrep
```

The three canonicalizers before the inliner are worth explaining: the inliner runs
`canonicalize` on each callee first (its `default-pipeline=canonicalize` option,
visible in `--print-pipeline`), so the reshapes fold before any call is inlined.

`--mlir-pass-statistics` gives the counts instead: how many patterns fired, how
many operations CSE eliminated.

## PassManager mechanics worth knowing

- The verifier runs between passes, so a pass that corrupts the IR is caught at
  its own boundary instead of three passes later. This is on by default and is the
  main reason MLIR pipelines are debuggable.
- `applyPassManagerCLOptions(pm)` is called before the pipeline is built
  (`src/main.cpp`), which is what lets the `--mlir-*` flags take effect; it is also
  the source of exit code 4 when they are malformed.
- A pass must declare the dialects it creates operations from, via
  `getDependentDialects`; see `src/passes/LowerToAffine.cpp:345-348`.

## Pitfalls

- `-emit=llvm` writes to stderr. `toyc x.toy -emit=llvm > out.ll` produces an empty
  file. Use `-o out.ll`, or `2>`.
- `-opt` stops being optional below the affine stage. The pipeline turns the Toy
  passes on regardless once you ask for lowering; `-opt` adds only fusion, scalar
  replacement and LLVM's `-O3`.
- Nesting on the wrong anchor fails at pipeline construction rather than at run
  time. `pm.nest<toy::FuncOp>()` after the affine lowering would never match
  anything.
- `--print-pipeline` needs no input file to be valid, but does need the other
  options, since the pipeline depends on the stage.
