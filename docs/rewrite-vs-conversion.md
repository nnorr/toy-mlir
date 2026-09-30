# Rewrite and conversion

Files: `src/dialect/Folders.cpp`, `src/dialect/ToyCombine.{td,cpp}`,
`src/passes/ShapeInference.cpp`, `src/passes/LowerToAffine.cpp`,
`src/passes/LowerToLLVM.cpp`, `src/Pipeline.cpp`

## What this covers

Documents 06 to 09 follow one file each. This one cuts across them and puts the
four mechanisms toy-mlir uses to change IR side by side. Two questions tell them
apart: does the change cross a dialect boundary, and is there a condition the IR
must satisfy when it is done?

| | fold | canonicalization pattern | hand-written pass | dialect conversion |
| --- | --- | --- | --- | --- |
| Produces | an `Attribute` or an existing `Value` | new IR | anything | new IR, in another dialect |
| Driven by | the greedy driver | the greedy driver | the pass itself | the conversion driver |
| Goal state | none | none, runs to a fixpoint | defined by the pass | a `ConversionTarget` |
| Nothing applies | IR unchanged | IR unchanged | the pass decides | fails if an illegal op remains |
| Dialects | same | usually the same | same | A to B |
| In toy-mlir | `Folders.cpp` | `ToyCombine.{td,cpp}` | `ShapeInference.cpp` | `LowerToAffine.cpp`, `LowerToLLVM.cpp` |

The first three tidy the IR at one level of abstraction; the last one moves it
down a level. That is the line between Toy-level optimization and lowering.

## 1. Fold: simplification without building ops

A folder answers "what is this operation's result?" with a constant `Attribute`
or a `Value` that already exists. It builds no IR, which makes it the cheapest
mechanism, and the greedy driver calls folders constantly. `let hasFolder = 1` in
Ops.td generates the hook; the implementations are in `src/dialect/Folders.cpp:43-64`.

```cpp
OpFoldResult ConstantOp::fold(FoldAdaptor adaptor) { return getValue(); }
OpFoldResult StructAccessOp::fold(FoldAdaptor adaptor) {
  auto structAttr =
      llvm::dyn_cast_if_present<mlir::ArrayAttr>(adaptor.getInput());
```

- `FoldAdaptor` hands over what each operand folded to, not the operand itself. A
  null entry means "not a constant".
- When a folder returns an `Attribute`, something has to turn it back into an
  operation. The Toy dialect's `hasConstantMaterializer = 1`
  (`include/toy/Ops.td:57`) generates that hook, `materializeConstant`.
- Folding is also why Ch7's structs are gone before lowering starts. After
  inlining every field access is `struct_access(struct_constant)`, and the fold
  replaces it with the element's attribute. Adding the struct type changed
  nothing in the lowering passes.

## 2. Canonicalization: one form per meaning

Canonical means the standard form. When the same meaning can be written several
ways, collapsing them into one means later passes only have to match that one.
The chosen form is usually the simpler one, so canonicalization also optimizes as
a side effect.

`-canonicalize` is a generic pass that knows nothing about Toy. Each op registers
its own patterns, and the pass collects them and applies them until the IR stops
changing. `hasCanonicalizer = 1` generates the registration hook; the
implementations are in `src/dialect/ToyCombine.cpp:79-89`.

```cpp
void TransposeOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                              MLIRContext *context) {
  results.add<SimplifyRedundantTranspose>(context);
}
```

A pattern can be written two ways:

- C++: `SimplifyRedundantTranspose` (`src/dialect/ToyCombine.cpp:54`) derives from
  `OpRewritePattern<TransposeOp>`. If the operand was defined by another
  transpose, it replaces the result with the original input.
- DRR in TableGen: rules that are purely structural are written declaratively
  (`src/dialect/ToyCombine.td:42`).

```tablegen
def ReshapeReshapeOptPattern : Pat<(ReshapeOp(ReshapeOp $arg)),
                                   (ReshapeOp $arg)>;
```

Removing the inner op that is left over is the driver's dead-code elimination,
and it only happens for ops marked `Pure`. That is why `toy.print` is not `Pure`
(`include/toy/Ops.td:330` declares it with no traits at all): it has no results,
so a `Pure` `toy.print` would have no uses and would be trivially dead, and every
print in the program would be deleted.

Why not do this during lowering? `transpose(transpose(x))` is only recognizable
while the operation is still called transpose. Once it is a loop nest, the same
fact is a theorem about index arithmetic that nobody is going to prove. So it
happens at the highest level where the meaning is still visible.

## 3. A hand-written pass: shape inference

`src/passes/ShapeInference.cpp` uses no patterns at all. It walks the function,
puts every op with a dynamically shaped result on a worklist, and repeatedly
takes an op whose operands are all ranked and calls its `inferShapes()` through
the `ShapeInference` interface. If progress stops with the worklist non-empty,
the pass fails.

The rule "once the operands are ranked, fix the result type" could run to a
fixpoint as a greedy pattern just as well. It is a pass for two other reasons:

- There is a global condition at the end. Every shape must be known, and if some
  are not, the pass has to report how many and fail. The greedy driver has no
  notion of "fail unless the IR is in this state".
- It updates result types in place rather than replacing ops. The per-op rule
  comes from the `ShapeInference` interface; the pass is only responsible for
  order and termination. This is the split from 07 of a generic algorithm from
  per-op rules through an interface.

## 4. Dialect conversion: moving down a level

### 4.1 The target says what may remain

A conversion starts from a specification of the end state, not a list of
rewrites. The framework uses the patterns it is given to make the IR satisfy that
specification, and fails if it cannot. The first lowering's target is in
`src/passes/LowerToAffine.cpp:358-373`.

```cpp
  ConversionTarget target(getContext());
  target.addLegalDialect<affine::AffineDialect, BuiltinDialect,
                         arith::ArithDialect, func::FuncDialect,
                         memref::MemRefDialect>();
  target.addIllegalDialect<toy::ToyDialect>();
  target.addDynamicallyLegalOp<toy::PrintOp>([](toy::PrintOp op) {
    return llvm::none_of(op->getOperandTypes(),
                         [](Type type) { return llvm::isa<TensorType>(type); });
  });
```

- There are three kinds of rule: legal, meaning it may remain; illegal, meaning
  it must be gone; and dynamically legal, decided per op instance by a callback.
- Per-op rules take precedence over per-dialect rules. So although the whole Toy
  dialect is illegal, `toy.print` survives as long as its operand is a memref.
- `toy.print` is dynamically legal rather than plainly legal because its operand
  must change while the op itself stays. All `PrintOpLowering`
  (`src/passes/LowerToAffine.cpp:271`) does is swap the operand.

```cpp
    rewriter.modifyOpInPlace(op,
                             [&] { op->setOperands(adaptor.getOperands()); });
```

### 4.2 Patterns and the adaptor

A conversion pattern derives from `OpConversionPattern<SourceOp>` and takes an
extra `OpAdaptor` argument (`src/passes/LowerToAffine.cpp:141-163`).

```cpp
struct BinaryOpLowering : public OpConversionPattern<BinaryOp> {
      auto loadedLhs =
          affine::AffineLoadOp::create(builder, loc, adaptor.getLhs(), loopIvs);
```

`op.getLhs()` returns the tensor the op was built with; `adaptor.getLhs()`
returns the already converted value, a memref. Match on the old types, build with
the new ones: that is the core contract of a conversion pattern. A
canonicalization `OpRewritePattern` has no adaptor, because within one dialect
the operands never change type.

### 4.3 Partial and full

| | `applyPartialConversion` | `applyFullConversion` |
| --- | --- | --- |
| Used for | toy to affine | everything else to LLVM |
| Rule | illegal ops must go; ops that are neither legal nor illegal may stay | nothing may remain except what the target declares legal |
| Consequence | two levels can coexist in one function | the module is guaranteed translatable to LLVM IR |

Because the first lowering is partial, the IR right after it holds `affine.for`
and `toy.print` side by side. The second has to be full because `Translate.cpp`
hands the module straight to `translateModuleToLLVMIR` (`src/Translate.cpp:40`),
which understands the LLVM dialect and nothing else.

### 4.4 Type conversion: absent in the first, present in the second

- toy to affine has no `TypeConverter`, and the string does not appear in
  `LowerToAffine.cpp` at all. Each pattern does the tensor to memref change
  itself: `lowerOpToLoops` allocates a buffer and calls
  `rewriter.replaceOp(op, alloc)` (`:125`), and the framework records that
  replacement and hands the memref to later patterns through their adaptors. The
  function signature needs no conversion because `main` has no arguments and no
  results, which is also why `FuncOpLowering` rejects any other function
  (`:244`).
- The LLVM lowering has an `LLVMTypeConverter` (`src/passes/LowerToLLVM.cpp:221`).
  This is where a memref becomes the descriptor struct of allocated pointer,
  aligned pointer, offset, sizes and strides, and everything the memref type
  carried implicitly becomes an explicit value. Converting the block arguments of
  loop regions needs a type converter too, which is a general property of the
  framework rather than something this repo exercises.

### 4.5 Several steps hidden in one pass, and transitive lowering

The second lowering merges pattern sets and runs them in one conversion
(`src/passes/LowerToLLVM.cpp:223-238`).

```cpp
  populateAffineToStdConversionPatterns(patterns);
  populateSCFToControlFlowConversionPatterns(patterns);
  mlir::arith::populateArithToLLVMConversionPatterns(typeConverter, patterns);
  populateFinalizeMemRefToLLVMConversionPatterns(typeConverter, patterns);
  cf::populateControlFlowToLLVMConversionPatterns(typeConverter, patterns);
  cf::populateAssertToLLVMConversionPattern(typeConverter, patterns);
  populateFuncToLLVMConversionPatterns(typeConverter, patterns);
  patterns.add<PrintOpLowering>(&getContext());
  if (failed(applyFullConversion(module, target, std::move(patterns))))
```

- affine to scf, arith to cf, cf to llvm are not separate passes. They are all
  members of one pattern set, so `--mlir-print-ir-after-all` never shows the IR
  in an scf or cf state. Checked: at `-emit=mlir-llvm -opt` that dump contains no
  line beginning with `scf.` or `cf.`.
- Toy contributes one pattern, for `toy.print`, and it emits an `scf.for` nest
  rather than LLVM operations (`src/passes/LowerToLLVM.cpp:107`). scf is illegal
  for this target, but the scf to cf to llvm patterns in the same set finish the
  job. A pattern may produce illegal IR as long as some other pattern can
  legalize it, and that is transitive lowering.
- This pass is also where regions become a CFG. `affine.for` and `scf.for` are
  ops that hold their body in a region; `cf` is branches between blocks. That is
  why optimizations depending on loop structure, fusion and scalar replacement,
  have to run before this pass.

## 5. Where each mechanism runs

`src/Pipeline.cpp` fixes the order.

```text
inline                          (module)       generic pass, driven by interfaces
canonicalize                    (toy.func)     section 1 folds + section 2 patterns
shape-inference                 (toy.func)     section 3
canonicalize, cse               (toy.func)
toy-to-affine                   (module)       section 4, partial conversion
canonicalize, cse               (func.func)    toy.func no longer exists
[-opt] affine-loop-fusion, affine-scalrep
toy-to-llvm                     (module)       section 4, full conversion, region to CFG
DIScopeForLLVMFuncOp, or AttachDebugInfo with -g
```

- Canonicalize runs several times, each time over different dialects. The run
  after lowering applies affine and arith patterns, not Toy's.
- The last line is easy to miss: one debug-scope pass always runs after the LLVM
  lowering (`src/Pipeline.cpp:83-86`). Without `-g` it is upstream's
  `DIScopeForLLVMFuncOpPass`, which is what keeps `-emit=mlir-llvm`
  byte-identical to upstream; with `-g` our own pass takes its place.
- Canonicalizing before shape inference has a visible effect: the reshape folds
  remove ops whose shapes would otherwise have to be inferred, which
  [`ir-trace.md`](ir-trace.md) shows at its second pass boundary, where two
  constants and two reshapes collapse into one value. Whether the order was
  chosen for that reason or inherited from upstream, which uses the same order,
  is not recorded anywhere in this repo.

## 6. Adding a transformation: where it belongs

| Goal | Mechanism | Where |
| --- | --- | --- |
| Report that an op's result is a known constant | fold | the op definition, `hasFolder` |
| Collapse equivalent forms into one | canonicalization pattern | the op definition, `hasCanonicalizer` |
| An ordered analysis that then updates the IR | hand-written pass | `src/passes/` |
| Move to another dialect | conversion patterns plus a target | `src/passes/`, `Conversion/` upstream |
| A property several generic passes ask about | trait or interface | the op definition, or an external model |

The last row connects to 07 on interfaces. What an op is belongs with the op; how
to lower it belongs in conversion patterns. A lowering recipe is never made into
an interface.

## Trying it

```bash
# Before and after canonicalization (section 2)
build/bin/toyc reference/tests/Ch3/transpose_transpose.toy -emit=mlir
build/bin/toyc reference/tests/Ch3/transpose_transpose.toy -emit=mlir -opt

# IR after every pass (section 5). The dump after the affine lowering shows
# affine.for and toy.print in the same function (section 4.3).
build/bin/toyc reference/tests/Ch2/codegen.toy -emit=mlir-affine -opt \
  --mlir-print-ir-after-all --mlir-disable-threading

# Split the hidden affine -> scf -> cf steps apart with upstream passes (4.5)
build/bin/toyc reference/tests/Ch2/codegen.toy -emit=mlir-affine -opt \
  --mlir-print-op-generic -o build/affine.mlir
mlir-opt build/affine.mlir --allow-unregistered-dialect --lower-affine
mlir-opt build/affine.mlir --allow-unregistered-dialect --lower-affine \
  --convert-scf-to-cf
```

The last two commands go through the generic form so that `mlir-opt` accepts the
remaining `toy.print` as an unregistered op. They need an `mlir-opt` from the same
LLVM, which is not part of this repo.

These are `bash` blocks rather than `console` blocks, so `tests/check-docs.py`
does not run them, which makes them the one set of commands here that no test
keeps honest. They were all run by hand when this document was written; the
`mlir-opt` path produced two `scf.for` operations from the affine loops.

For the same material already captured as files, with the per-pass annotations,
see [`ir-trace.md`](ir-trace.md) and `docs/examples/dumps/`.
