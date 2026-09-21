# 12. Debug info, object files and the JIT

Files: `src/DebugInfo.cpp` (277 lines), `src/Translate.cpp`,
`src/ObjectEmitter.cpp`, `src/Jit.cpp`.

Two subjects: where debug information comes from in an MLIR pipeline, and the
three ways out of MLIR.

---

# Part A: debug info

## The locations are already there

In a hand-written LLVM front end you build debug info as you emit IR: a
`DIBuilder`, a `DICompileUnit`, a `DISubprogram` per function, a
`DILocalVariable` per variable, and an explicit `SetCurrentDebugLocation` before
each expression.

None of that happens here. `src/DebugInfo.cpp:10-17` explains why: MLIRGen
attached a `FileLineColLoc` to every operation it built, and those locations
survived inlining, shape inference, the affine lowering and the LLVM lowering.
Translation to LLVM IR already turns a location into a `DILocation`.

What is missing at the end of the pipeline is only the scope tree, since a
`DILocation` is emitted only when it has a scope to belong to. So the entire job
of this pass is one compile unit per module and one subprogram per function.

The architectural difference is worth presenting. Locations ride along for free
because they are part of every operation, while scopes have to be attached once,
at the end.

## What the pass does

`AttachDebugInfoPass` (`src/DebugInfo.cpp:195-266`) walks the module:

```c++
module.walk<WalkOrder::PreOrder>([&](Operation *op) {
  if (auto funcOp = dyn_cast<LLVM::LLVMFuncOp>(op))
    addScopeToFunction(funcOp, compileUnitAttr);
  else
    setLexicalBlockFileAttr(op);
});
```

`addScopeToFunction` (`:77-121`) builds a `DISubprogramAttr` and attaches it by
fusing it onto the function's location:

```c++
llvmFunc->setLoc(FusedLoc::get(context, {loc}, subprogramAttr));
```

`:120`. This is the MLIR idiom worth learning: debug metadata is carried as
attributes on locations, not as a separate structure. "This function has a
subprogram" is spelled "this function's location is a `FusedLoc` whose metadata is
a `DISubprogramAttr`". `:73-76` says so in the code.

`extractFileLoc` (`:48-63`) digs a file location out of whatever wrappers a
location has picked up along the way: `NameLoc`, `OpaqueLoc`, `FusedLoc`,
`CallSiteLoc`. A location that has been through inlining is a nest of these.

`setLexicalBlockFileAttr` (`:148-191`) gives operations that came from a call site,
or from a different file than their enclosing function, a scope of their own.
Without it their line numbers would be attributed to the wrong file, which
matters as soon as inlining has moved code.

## Where it differs from upstream

Upstream's `DIScopeForLLVMFuncOpPass`, which is what runs without `-g` to keep
output byte-identical, names the compile unit `"MLIR"` and takes its file from the
module's own location. For a Toy compilation that location is unknown,
because MLIRGen builds the module with an unknown loc, so the compile unit ends up
file-less.

`findSourceFile` (`:249-263`) recovers the `.toy` path from the functions inside
the module instead:

```c++
LLVM::DIFileAttr findSourceFile(ModuleOp module) {
  MLIRContext *context = module->getContext();
  if (FileLineColLoc moduleLoc = extractFileLoc(module.getLoc()))
    return makeFileAttr(context, moduleLoc.getFilename().getValue());

  LLVM::DIFileAttr found;
  module.walk([&](LLVM::LLVMFuncOp funcOp) {
    if (found)
      return WalkResult::interrupt();
    if (FileLineColLoc funcLoc = extractFileLoc(funcOp.getLoc()))
      found = makeFileAttr(context, funcLoc.getFilename().getValue());
    return WalkResult::advance();
  });
  return found ? found : LLVM::DIFileAttr::get(context, "<unknown>", "");
}
```

The pass also respects a compile unit someone else already attached (`:221-232`),
so running it after upstream's pass cannot produce two.

## The honest limit

Emission kind is `LineTablesOnly` (`:231`), and `:25-28` is candid about why:
there is no `DILocalVariable` anywhere, because Toy variables do not survive as
variables. They are SSA values, then memref slots. A debugger can step by Toy line
and cannot print a Toy variable.

Producing variable-level debug info would mean deciding, for each Toy variable,
which memref slot represents it at which point, and emitting `llvm.dbg.declare`
accordingly. The information exists in MLIRGen's symbol table and is thrown away.
That is the gap between this and a front end that builds DWARF by hand.

## Try it

The input is `docs/examples/ex.toy`, the running example these documents share, and
the objects go under `build/`. Run from the repo root.

```console
$ build/bin/toyc docs/examples/ex.toy -c -g -o build/ex-g.o
$ readelf --debug-dump=info build/ex-g.o | grep -E 'DW_AT_producer|DW_AT_name|DW_AT_comp_dir' | head -4
    <c>   DW_AT_producer    : (indirect string, offset: 0x0): toyc (toy-mlir)
    <12>   DW_AT_name        : (indirect string, offset: 0x10): ex.toy
    <1a>   DW_AT_comp_dir    : (indirect string, offset: 0x17): docs/examples
    <2b>   DW_AT_name        : (indirect string, offset: 0x25): main
```

The compile unit names the Toy file, and `DW_AT_comp_dir` is the directory part of
the path as it was given on the command line. `makeFileAttr` splits the path it was
handed (`:66-69`) and does not absolutize it, so a relative input produces a
relative `comp_dir`.

The same object built without `-g`, i.e. through upstream's pass:

```console
$ build/bin/toyc docs/examples/ex.toy -c -o build/ex.o
$ readelf --debug-dump=info build/ex.o | grep -E 'DW_AT_producer|DW_AT_name|DW_AT_comp_dir' | head -4
    <c>   DW_AT_producer    : (indirect string, offset: 0x0): MLIR
    <12>   DW_AT_name        : (indirect string, offset: 0x5): <unknown>
    <27>   DW_AT_name        : (indirect string, offset: 0xf): main
```

`"MLIR"` and `"<unknown>"` versus `"toyc (toy-mlir)"` and `"ex.toy"`, and no
`DW_AT_comp_dir` at all. That is the whole difference `-g` makes, and it is the
reason the flag exists.

The line table points back at Toy source either way:

```console
$ readelf --debug-dump=decodedline build/ex-g.o | head -10
Contents of the .debug_line section:

CU: ex.toy:
File name                            Line number    Starting address    View
ex.toy                                         6                   0
ex.toy                                         3                 0xe
ex.toy                                         3                0x1e
ex.toy                                         0                0x91
ex.toy                                         3                0xa0
ex.toy                                         0                0xb1
```

In `docs/examples/ex.toy`, line 6 is `main` and line 3 is the body of
`multiply_transpose`, inlined into it. The line table is showing inlined Toy code
attributed to the line it was written on, which is `setLexicalBlockFileAttr` doing
its job.

> A line table is present with or without `-g`, because the pipeline keeps
> upstream's pass for byte-identity. `-g` changes the producer and the file name,
> not the existence of the table. This tripped up the test suite during
> development.

---

# Part B: leaving MLIR

Three exits, all from the same LLVM-dialect module.

## Translation

```c++
mlir::registerBuiltinDialectTranslation(*module->getContext());
mlir::registerLLVMDialectTranslation(*module->getContext());

std::unique_ptr<llvm::Module> llvmModule =
    mlir::translateModuleToLLVMIR(module, llvmContext);
```

`src/Translate.cpp:32-40`. Translation is opt-in per dialect: each dialect that
can become LLVM IR registers an interface saying how. Without those two
registrations the translation fails on the first operation it meets, even though
the module is already entirely LLVM dialect (`:32-35`).

Then the module is tagged with a triple and data layout, taken from the caller's
`TargetMachine` if there is one and otherwise by detecting the host, and optionally
run through LLVM's `-O3` pipeline via `mlir::makeOptimizingTransformer`.

The distinction worth drawing: `mlir-opt`-style passes are MLIR in, MLIR out.
This is a *translation*: the result is a different IR, and MLIR tooling can no
longer inspect it.

## Object emission

`src/ObjectEmitter.cpp` was written against the LLVM API rather than ported; its
header comment lists the headers it works from. Three steps: look up the target,
build a `TargetMachine`, run it.

Two API facts shaped the code, and both differ from what older examples show:

- In this LLVM, `TargetRegistry::lookupTarget` and `Module::setTargetTriple` take
  a `Triple` object rather than a string.
- Object emission still requires the legacy pass manager:
  `addPassesToEmitFile` takes a `legacy::PassManagerBase` and no new-PM equivalent
  is exposed (`src/ObjectEmitter.cpp:19-20`, `:97`).

The module's triple and data layout are set from the `TargetMachine` before
emission (`:87`), because a module tagged for one target cannot be emitted for
another.

```console
$ nm build/ex.o | grep -E ' T | U '
                 U free
0000000000000000 T main
                 U malloc
                 U printf
```

One defined symbol and three undefined ones, exactly what the lowering promised.
So it links and runs. Any C compiler will do the link; `gcc` is used here only
because it is on the path more often than `clang` is:

```console
$ gcc build/ex.o -o build/ex && build/ex
1.000000 16.000000 
4.000000 25.000000 
9.000000 36.000000 
```

```console
$ diff <(build/ex) <(build/bin/toyc docs/examples/ex.toy -emit=jit 2>/dev/null) \
  && echo "NATIVE OUTPUT == JIT OUTPUT"
NATIVE OUTPUT == JIT OUTPUT
```

That equality is the end-to-end check worth having: the JIT and the native
executable agree, so the lowering is not an artifact of the JIT's environment.

## The JIT

`src/Jit.cpp`. `mlir::ExecutionEngine` takes an MLIR module directly: it
translates, compiles, and resolves `printf` against the running process. Note how
little separates this from the object path: both translate the same module; only
what happens to the result differs.

```c++
auto maybeEngine = mlir::ExecutionEngine::create(module, engineOptions);
```

Creation is where compilation happens; the engine is eager, so a failure here is a
compile error rather than a run-time one.

### Deviation D8, and a C++ lifetime trap

```c++
if (llvm::Error err = engine->invokePacked("main")) {
  llvm::errs() << "JIT invocation failed: " << llvm::toString(std::move(err))
               << "\n";
  return -1;
}
```

`src/Jit.cpp`, with the reasoning in the comment above it. The subtlety is worth
presenting on its own slide:

- `llvm::Error` must be checked and then consumed. An `Error` that was checked but
  never consumed aborts the process when it is destroyed.
- Writing `if (engine->invokePacked("main"))` tests a temporary, which is destroyed
  at the end of the condition, before the body runs. The diagnostic inside the `if`
  is therefore unreachable, because the process aborts first.
- Upstream binds it to a named variable, so its message does print; it then still
  aborts at scope exit, because it never consumes the error.
- Binding it *and* consuming it with `toString` gives a clean diagnostic and a
  normal exit.

Measured, on a program with no `main`:

```console
$ build/bin/toyc reference/tests/Ch7/empty.toy -emit=jit ; echo "exit=$?"
JIT invocation failed: Symbols not found: [ _mlir_main ]
exit=255

$ ~/dev/08_mlir_toy/build/bin/toyc-ch7 reference/tests/Ch7/empty.toy -emit=jit ; echo "exit=$?"
JIT invocation failed
Program aborted due to an unhandled Error:
Symbols not found: [ _mlir_main ]
...
exit=134
```

(The elided line is the shell's own `Aborted (core dumped)` notice, which carries a
process id.)

Ours also reports *why*, which upstream's message omits, because the cause was in
the `Error` it discarded.

`src/Jit.cpp` makes the same change for engine construction, where upstream
`assert`s instead.

## Pitfalls

- Forgetting the translation registrations gives a failure that looks like a
  lowering bug, on a module that is already fully lowered.
- `-g` does not control whether a line table exists, only whose compile unit it
  belongs to. Do not test for the table's presence to test `-g`.
- `addPassesToEmitFile` needs the legacy pass manager. Reaching for `PassBuilder`
  here does not work.
- An `llvm::Error` in an `if` condition is a trap in any LLVM codebase, not just
  this one. Bind it, then consume it.
- The JIT resolves `printf` against the host process; a program that called
  something absent would fail at engine creation, not at the call.
