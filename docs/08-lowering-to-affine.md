# 08. Lowering to Affine

File: `src/passes/LowerToAffine.cpp` (355 lines).

## What this is for

The first of two lowerings. It turns the computational Toy operations into affine
loop nests over memrefs, and deliberately leaves `toy.print` alone. After this
pass one function contains operations from five dialects at two levels of
abstraction, which the whole design depends on: MLIR does not make you pick one
level per module.

Two changes happen at once, and they are worth separating:

1. Operations change: `toy.mul` becomes a loop nest of `arith.mulf`.
2. Types change: `tensor<3x2xf64>` becomes `memref<3x2xf64>`.

## Tensor versus memref, and what the change costs

A tensor is an SSA value. It has no address, nothing aliases it, and its
definition is visible in the use-def chain. A memref is a reference to memory:
`affine.store` writes *through* it without producing a new value.

`src/passes/LowerToAffine.cpp:16-21` states the consequence: after this pass the
data-flow graph no longer describes the contents of those buffers, only the loads
and stores against them. Any optimization that has to reason about the data, such
as fusion or redundant-load elimination, must run at or above this level. This is
the concrete form of the rule *optimize at the highest level where the
information still exists*.

The type mapping itself is one line (`:68-70`):

```c++
static MemRefType convertTensorToMemRef(RankedTensorType type) {
  return MemRefType::get(type.getShape(), type.getElementType());
}
```

## DialectConversion: three pieces

The framework that performs a lowering takes a declarative view. You do not write
"rewrite this, then that"; you declare what is *allowed to remain* and supply
patterns, and the framework works out whether it can get there.

| Piece | Role | Here |
| --- | --- | --- |
| `ConversionTarget` | what is legal afterwards | `:358-373` |
| Rewrite patterns | how to legalize what is not | `:140-328` |
| `TypeConverter` | how to convert block and region argument types | not needed; `main` has no arguments |

### The target

```c++
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

`src/passes/LowerToAffine.cpp:358-373`. The last rule is the one to read twice:
`toy.print` is legal once its operands are no longer tensors. Marking it
*dynamically* legal rather than plainly legal forces its operand to be rewritten
while leaving the operation itself in place. Per-operation rules always beat
per-dialect rules, so the order of these three statements does not matter.

This is why `PrintOp`'s ODS accepts a memref as well as a tensor
(`include/toy/Ops.td:303`). The seam that makes the lowering partial lives in the
operation's definition as much as in the pass.

### Partial versus full

```c++
if (failed(applyPartialConversion(getOperation(), target, std::move(patterns))))
  signalPassFailure();
```

`:382-384`. Partial means: operations that are neither legal nor convertible are
an error, but legal operations from any dialect may stay. [The LLVM
lowering](09-lowering-to-llvm.md) uses `applyFullConversion`, where anything left
illegal fails.

## OpConversionPattern and its adaptor

Every pattern here is an `OpConversionPattern`, whose `matchAndRewrite` takes an
extra argument:

```c++
LogicalResult matchAndRewrite(BinaryOp op, OpAdaptor adaptor,
                              ConversionPatternRewriter &rewriter) const final {
  auto loc = op->getLoc();
  lowerOpToLoops(op, rewriter, [&](OpBuilder &builder, ValueRange loopIvs) {
    auto loadedLhs =
        affine::AffineLoadOp::create(builder, loc, adaptor.getLhs(), loopIvs);
    auto loadedRhs =
        affine::AffineLoadOp::create(builder, loc, adaptor.getRhs(), loopIvs);
    return LoweredBinaryOp::create(builder, loc, loadedLhs, loadedRhs);
  });
  return success();
}
```

`:145-158`. Note `adaptor.getLhs()` rather than `op.getLhs()`. The adaptor hands
back the converted operands, which are already memrefs, while `op` still reports
the tensors it was built with. Matching happens on the old types, building on the
new ones. Getting this wrong is the most common way to write a conversion pattern
that compiles and then fails legalization.

Two aliases give the two binary operations one implementation (`:160-161`):

```c++
using AddOpLowering = BinaryOpLowering<toy::AddOp, arith::AddFOp>;
using MulOpLowering = BinaryOpLowering<toy::MulOp, arith::MulFOp>;
```

## Building the loop nest

`lowerOpToLoops` (`:102-126`) is shared by every element-wise operation: it
allocates the result buffer, builds one loop per dimension, and calls back for
the body.

```c++
SmallVector<int64_t, 4> lowerBounds(tensorType.getRank(), /*Value=*/0);
SmallVector<int64_t, 4> steps(tensorType.getRank(), /*Value=*/1);
affine::buildAffineLoopNest(
    rewriter, loc, lowerBounds, tensorType.getShape(), steps,
    [&](OpBuilder &nestedBuilder, Location loc, ValueRange ivs) {
      Value valueToStore = processIteration(nestedBuilder, ivs);
      affine::AffineStoreOp::create(nestedBuilder, loc, valueToStore, alloc, ivs);
    });
```

Bounds are literals rather than values (`:110-113`). That is the affine dialect's
bargain: it guarantees every loop bound and every access index is an affine
function of the enclosing indices, and fusion and tiling rely on that guarantee
to prove their transformations legal later.

### Allocation

```c++
static Value insertAllocAndDealloc(MemRefType type, Location loc,
                                   PatternRewriter &rewriter) {
  auto alloc = memref::AllocOp::create(rewriter, loc, type);
  auto *parentBlock = alloc->getBlock();
  alloc->moveBefore(&parentBlock->front());
  auto dealloc = memref::DeallocOp::create(rewriter, loc, alloc);
  dealloc->moveBefore(&parentBlock->back());
  return alloc;
}
```

`:80-90`. The alloc is hoisted to the top of the block and the dealloc sunk to
just before the terminator, so the buffer outlives every use wherever the pattern
happened to be inserting. `:76-79` is candid that this is only sound because Toy
functions are a single block with no control flow; a real bufferization pass has
to solve placement properly.

## The individual patterns

`ConstantOpLowering` (`:172-226`) is the longest, because an attribute holding a
whole array has to become one store per element. Two details worth noting: the
index constants are created once and reused (`:185-199`), rather than emitting an
`arith.constant` per element for CSE to clean up; and the recursion over the shape
walks in the same row-major order the attribute iterates, so a single forward
iterator keeps them in step (`:201-219`).

`TransposeOpLowering` (`:313-328`) is the prettiest of them. Nothing moves data in
a transposed order; the loop reads `[j, i]` while writing `[i, j]`:

```c++
lowerOpToLoops(op, rewriter, [&](OpBuilder &builder, ValueRange loopIvs) {
  Value input = adaptor.getInput();
  SmallVector<Value, 2> reverseIvs(llvm::reverse(loopIvs));
  return affine::AffineLoadOp::create(builder, loc, input, reverseIvs);
});
```

`FuncOpLowering` (`:238-260`) converts `toy.func` to `func.func`, and only for
`main`:

```c++
if (op.getName() != "main")
  return failure();

if (op.getNumArguments() || op.getFunctionType().getNumResults()) {
  return rewriter.notifyMatchFailure(op, [](Diagnostic &diag) {
    diag << "expected 'main' to have 0 inputs and 0 results";
  });
}
```

Every other function is expected to be gone: the inliner ran first, and a
surviving generic function would still have unranked parameters this pass could
not allocate buffers for. Returning `failure()` leaves it illegal and the partial
conversion reports it, which is the target doing the error reporting for you.

`PrintOpLowering` (`:271-281`) is three lines: update the operand in place, leave
the operation standing.

`ReturnOpLowering` (`:290-302`) handles only the operand-less form; a returned
value would mean a function other than `main` survived.

## Try it

The excerpts here are abridged to keep the interesting lines together. For the
whole level unabridged, `docs/examples/dumps/codegen.mlir-affine.txt` is this
stage in full for the same program, and `codegen.mlir-affine.opt.diff` is what
loop fusion and scalar replacement change.

The `// the transpose` and `// the multiply` comments below are added here to mark
which loop nest is which; `toyc` does not print them. Everything else is its real
output, abridged only where a line says `...`.

```console
$ build/bin/toyc reference/tests/Ch5/codegen.toy -emit=mlir-affine 2>&1
module {
  func.func @main() {
    %cst = arith.constant 6.000000e+00 : f64
    ... six constants ...
    %alloc = memref.alloc() : memref<3x2xf64>
    %alloc_5 = memref.alloc() : memref<3x2xf64>
    %alloc_6 = memref.alloc() : memref<2x3xf64>
    affine.store %cst_4, %alloc_6[0, 0] : memref<2x3xf64>
    ... six stores, one per element ...
    affine.for %arg0 = 0 to 3 {                       // the transpose
      affine.for %arg1 = 0 to 2 {
        %0 = affine.load %alloc_6[%arg1, %arg0] : memref<2x3xf64>
        affine.store %0, %alloc_5[%arg0, %arg1] : memref<3x2xf64>
      }
    }
    affine.for %arg0 = 0 to 3 {                       // the multiply
      affine.for %arg1 = 0 to 2 {
        %0 = affine.load %alloc_5[%arg0, %arg1] : memref<3x2xf64>
        %1 = arith.mulf %0, %0 : f64
        affine.store %1, %alloc[%arg0, %arg1] : memref<3x2xf64>
      }
    }
    toy.print %alloc : memref<3x2xf64>
    memref.dealloc %alloc_6 : memref<2x3xf64>
    memref.dealloc %alloc_5 : memref<3x2xf64>
    memref.dealloc %alloc : memref<3x2xf64>
    return
  }
}
```

Points to make from this output: `func.func` replaced `toy.func`; three dialects
(`affine`, `arith`, `memref`) coexist; `toy.print` is still there, now taking a
memref; and there are two separate loop nests over the same 3×2 space.

Now with `-opt`, which adds `affine-loop-fusion` and `affine-scalrep`:

```console
$ build/bin/toyc reference/tests/Ch5/codegen.toy -emit=mlir-affine -opt 2>&1
    %alloc = memref.alloc() : memref<3x2xf64>
    %alloc_5 = memref.alloc() : memref<2x3xf64>
    ... six stores ...
    affine.for %arg0 = 0 to 3 {
      affine.for %arg1 = 0 to 2 {
        %0 = affine.load %alloc_5[%arg1, %arg0] : memref<2x3xf64>
        %1 = arith.mulf %0, %0 : f64
        affine.store %1, %alloc[%arg0, %arg1] : memref<3x2xf64>
      }
    }
    toy.print %alloc : memref<3x2xf64>
```

Two loop nests became one, and one of the three buffers is gone: the transpose's
intermediate `memref<3x2xf64>` disappeared, because the load it produced is now
read directly in the fused body with reversed indices. This is the payoff for
lowering *through* affine instead of straight to loops. Fusion has to prove the
two nests access memory compatibly, and that proof needs the affine guarantee.

One correction, since the claim circulates: no accumulator collapses to
`memref<1x1xf64>` in Toy. That effect comes from the beginner-friendly tutorial's
matmul, where fusion sinks a *reduction* into the consumer loop and the
accumulator becomes scalar. Toy has no reduction, so fusion here removes a buffer
instead. Verified: `grep -c '1x1xf64'` over every `-emit=mlir-affine -opt` output
in `reference/tests/Ch5/` returns 0 for all nine inputs.

## Pitfalls

- Take operands from the adaptor, not the op. `op.getLhs()` gives you a tensor in
  a world where only memrefs are legal.
- `toy.print` legality is dynamic for a reason. Making it plainly legal would
  leave its tensor operand unconverted; making it illegal with no pattern would
  fail the conversion.
- Allocation placement here is sound only for single-block functions
  (`:76-79`). Do not copy it into anything with control flow.
- `getDependentDialects` (`:345-348`) must list every dialect the pass builds
  operations from, or the pass manager will not have loaded them.
