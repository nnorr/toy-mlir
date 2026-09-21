# 06. Patterns and folding

Files: `src/dialect/ToyCombine.td` (68), `src/dialect/ToyCombine.cpp` (89), `src/dialect/Folders.cpp` (64), and the generated `build/src/ToyCombine.inc` (174)

## What these do

These files hold the rewrites that need Toy's own semantics. `transpose(transpose(x))` is only recognisable while the operation still says "transpose"; once the program is loop nests over memrefs, the same simplification is a theorem about index arithmetic that nobody is going to prove.

Nothing here runs a pass. Patterns are registered on the operations themselves, and MLIR's generic canonicalizer picks them up. That is how a dialect contributes to a pass that has never heard of it.

## Why they live in the dialect

Upstream keeps `ToyCombine.cpp` next to its passes. Here it is in `src/dialect/`, and the reason is mechanical rather than aesthetic.

`getCanonicalizationPatterns` is an operation hook declared by `let hasCanonicalizer = 1` in ODS. The generated `RegisteredOperationName::Model<ReshapeOp>` refers to it, so `libToyDialect.a` contains a reference to a symbol that a file in `libToyPasses.a` defines. Static archives are searched in link order, so that arrangement produced a real failure:

```
libToyDialect.a(ToyDialect.cpp.o): in function
  `mlir::RegisteredOperationName::Model<mlir::toy::ReshapeOp>::getCanonicalizationPatterns(...)':
  undefined reference to `mlir::toy::ReshapeOp::getCanonicalizationPatterns(...)'
```

Reordering the archives does not fix it, because `ToyPasses` also needs the operation classes from `ToyDialect`, so the two would be mutually dependent. Moving the patterns into the dialect removes the cycle: an operation's canonicalization hook is part of the operation's definition, the same way its verifier and its folder are. `ARCHITECTURE.md` records the decision; the consequence is that `ToyPasses` now holds only the three passes Toy actually writes.

## Canonicalization, folding, and the greedy driver

Three mechanisms are easy to confuse.

A **folder** answers "what constant is this operation, if any" and returns an `Attribute`. No IR is built, which makes it cheap, and the greedy driver runs folders constantly.

A **canonicalization pattern** rewrites IR. It matches a shape and replaces it with another, through a `PatternRewriter` so the driver can track what changed.

The **canonicalizer pass** is generic MLIR. It collects every registered pattern for the operations it meets, then runs the greedy rewrite driver: apply patterns, fold, delete dead operations, repeat until nothing changes or the iteration limit is hit.

Benefits order competing patterns. The C++ transpose pattern declares 1 explicitly (`src/dialect/ToyCombine.cpp:57`):

```c++
  SimplifyRedundantTranspose(mlir::MLIRContext *context)
      : OpRewritePattern<TransposeOp>(context, /*benefit=*/1) {}
```

DRR computes a benefit from the number of operations matched, which is why the generated reshape-of-reshape pattern arrives with 2:

```c++
struct ReshapeReshapeOptPattern : public ::mlir::RewritePattern {
  ReshapeReshapeOptPattern(::mlir::MLIRContext *context)
      : ::mlir::RewritePattern("toy.reshape", 2, context, {"toy.reshape"}) {}
```

## The hand-written pattern

`transpose(transpose(x))` cancels (`src/dialect/ToyCombine.cpp:54`):

```c++
struct SimplifyRedundantTranspose : public mlir::OpRewritePattern<TransposeOp> {
  ...
  llvm::LogicalResult
  matchAndRewrite(TransposeOp op,
                  mlir::PatternRewriter &rewriter) const override {
    mlir::Value transposeInput = op.getOperand();
    TransposeOp transposeInputOp = transposeInput.getDefiningOp<TransposeOp>();

    // The operand came from somewhere else (a block argument, a constant, any
    // other op): nothing to cancel.
    if (!transposeInputOp)
      return failure();

    // Every mutation goes through the rewriter so the driver can track what
    // changed and re-examine the affected operations.
    rewriter.replaceOp(op, {transposeInputOp.getOperand()});
    return success();
  }
};
```

`getDefiningOp<TransposeOp>()` is the step that makes this C++ rather than DRR. It walks backwards along a use-def edge and asks what produced the value, returning null for a block argument. The pattern is written on the outer transpose because the rewrite replaces the outer one's result.

Registration is the ODS-generated hook (`src/dialect/ToyCombine.cpp:79`):

```c++
void TransposeOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                              MLIRContext *context) {
  results.add<SimplifyRedundantTranspose>(context);
}
```

The effect, on upstream's own test input:

```console
$ build/bin/toyc reference/tests/Ch3/transpose_transpose.toy -emit=mlir 2>&1 | head -6
module {
  toy.func private @transpose_transpose(%arg0: tensor<*xf64>) -> tensor<*xf64> {
    %0 = toy.transpose(%arg0 : tensor<*xf64>) to tensor<*xf64>
    %1 = toy.transpose(%0 : tensor<*xf64>) to tensor<*xf64>
    toy.return %1 : tensor<*xf64>
  }

$ build/bin/toyc reference/tests/Ch3/transpose_transpose.toy -emit=mlir -opt 2>&1
module {
  toy.func @main() {
    %0 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
    toy.print %0 : tensor<2x3xf64>
    toy.return
  }
}
```

Both transposes are gone, and so is the whole function, because after inlining nothing calls it and it is private.

## Pure permits the deletion

The pattern replaces the outer transpose's uses, which leaves both transposes with no users. They are deleted because `TransposeOp` has the `Pure` trait. Without it MLIR must assume an operation might have a side effect and keeps it, leaving a dead transpose in the IR whose result nobody reads.

Traits are permissions the rest of MLIR acts on. When an operation refuses to disappear from the output, a missing `Pure` is the first thing to check.

## The three declarative rules

`ToyCombine.td` holds rewrites expressible as "this tree becomes that tree" (`src/dialect/ToyCombine.td:41`):

```tablegen
// Reshape(Reshape(x)) = Reshape(x). The inner reshape is dead afterwards, and
// Pure on ReshapeOp is what allows the driver to delete it.
def ReshapeReshapeOptPattern : Pat<(ReshapeOp(ReshapeOp $arg)),
                                   (ReshapeOp $arg)>;
```

`NativeCodeCall` escapes into C++ when the result has to be computed rather than matched (`src/dialect/ToyCombine.td:52`):

```tablegen
def ReshapeConstant :
  NativeCodeCall<"$0.reshape(::llvm::cast<ShapedType>($1.getType()))">;
def FoldConstantReshapeOptPattern : Pat<
  (ReshapeOp:$res (ConstantOp $arg)),
  (ConstantOp (ReshapeConstant $arg, $res))>;
```

`Constraint` adds a predicate checked after the structural match (`src/dialect/ToyCombine.td:64`):

```tablegen
def TypesAreIdentical : Constraint<CPred<"$0.getType() == $1.getType()">>;
def RedundantReshapeOptPattern : Pat<
  (ReshapeOp:$res $arg), (replaceWithValue $arg),
  [(TypesAreIdentical $res, $arg)]>;
```

That last rule removes the no-op reshape `MLIRGen` emits for every declaration with an explicit shape (see [04-mlirgen.md](04-mlirgen.md)).

Registration puts all three on `ReshapeOp` (`src/dialect/ToyCombine.cpp:85`):

```c++
void ReshapeOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                            MLIRContext *context) {
  results.add<ReshapeReshapeOptPattern, RedundantReshapeOptPattern,
              FoldConstantReshapeOptPattern>(context);
}
```

Together they fold a chain of three reshapes into one constant of the final shape:

```console
$ cat reference/tests/Ch3/trivial_reshape.toy | sed -n '3,8p'
def main() {
  var a<2,1> = [1, 2];
  var b<2,1> = a;
  var c<2,1> = b;
  print(c);
}

$ build/bin/toyc reference/tests/Ch3/trivial_reshape.toy -emit=mlir 2>&1 | sed -n '3,7p'
    %0 = toy.constant dense<[1.000000e+00, 2.000000e+00]> : tensor<2xf64>
    %1 = toy.reshape(%0 : tensor<2xf64>) to tensor<2x1xf64>
    %2 = toy.reshape(%1 : tensor<2x1xf64>) to tensor<2x1xf64>
    %3 = toy.reshape(%2 : tensor<2x1xf64>) to tensor<2x1xf64>
    toy.print %3 : tensor<2x1xf64>

$ build/bin/toyc reference/tests/Ch3/trivial_reshape.toy -emit=mlir -opt 2>&1 | sed -n '3,4p'
    %0 = toy.constant dense<[[1.000000e+00], [2.000000e+00]]> : tensor<2x1xf64>
    toy.print %0 : tensor<2x1xf64>
```

The constant arrives with the reshaped data already applied, which is `FoldConstantReshapeOptPattern` doing the work at compile time.

## What the generator writes

`build/src/ToyCombine.inc` holds the three patterns as `RewritePattern` subclasses. Mapping the generated matcher back to its one-line `Pat<>` is the clearest way to see what DRR saves you. For `Pat<(ReshapeOp(ReshapeOp $arg)), (ReshapeOp $arg)>`:

```c++
    // Match
    tblgen_ops.push_back(op0);
    auto castedOp0 = ::llvm::dyn_cast<::mlir::toy::ReshapeOp>(op0); (void)castedOp0;
    {
      auto *op1 = (*castedOp0.getODSOperands(0).begin()).getDefiningOp();
      if (!(op1)){
        return rewriter.notifyMatchFailure(castedOp0, [&](::mlir::Diagnostic &diag) {
          diag << "There's no operation that defines operand 0 of castedOp0";
        });
      }
      auto castedOp1 = ::llvm::dyn_cast<::mlir::toy::ReshapeOp>(op1); (void)castedOp1;
      if (!(castedOp1)){
        return rewriter.notifyMatchFailure(op1, ...);
      }
      arg = castedOp1.getODSOperands(0);
      tblgen_ops.push_back(op1);
    }
```

The outer `ReshapeOp` becomes `castedOp0`, the nested one becomes `castedOp1` found through `getDefiningOp()`, and `$arg` becomes the captured operand range. The rewrite half builds a fresh `ReshapeOp` and gives it a fused location built from both matched operations:

```c++
    auto odsLoc = rewriter.getFusedLoc({tblgen_ops[0]->getLoc(), tblgen_ops[1]->getLoc()});
```

The fused location explains why a canonicalized program's locations sometimes name two places at once. The `notifyMatchFailure` calls supply the text that `--debug-only=dialect-conversion` style tracing prints when a pattern does not fire.

## Folders

Three folders, all short (`src/dialect/Folders.cpp:43`):

```c++
/// A constant folds to the attribute it already carries.
OpFoldResult ConstantOp::fold(FoldAdaptor adaptor) { return getValue(); }

/// Likewise for a struct constant, whose value is an array of the elements'
/// attributes.
OpFoldResult StructConstantOp::fold(FoldAdaptor adaptor) { return getValue(); }

OpFoldResult StructAccessOp::fold(FoldAdaptor adaptor) {
  auto structAttr =
      llvm::dyn_cast_if_present<mlir::ArrayAttr>(adaptor.getInput());
  if (!structAttr)
    return nullptr;

  // The index is in range because the verifier already checked it against the
  // struct type.
  size_t elementIndex = getIndex();
  return structAttr[elementIndex];
}
```

The `FoldAdaptor` hands over what the operands *folded to*, not the operands themselves. A null entry means "not a constant", which is the common case before inlining has exposed the constant. That is why the first line of `StructAccessOp::fold` is a null check rather than an error.

`ConstantLike` on the two constant operations tells the generic machinery how constants are spelled here, and `ToyDialect::materializeConstant` turns an attribute back into an operation ([05-dialect.md](05-dialect.md)).

## Folding makes structs disappear

Those seven lines remove any need for struct-aware lowering. After inlining, every field access is `struct_access(struct_constant, i)`; each fold replaces it with the element's attribute; and by the time lowering begins there is no struct left for the affine or LLVM conversions to handle.

```console
$ build/bin/toyc reference/tests/Ch7/struct-codegen.toy -emit=mlir 2>&1 | sed -n '2,8p'
  toy.func private @multiply_transpose(%arg0: !toy.struct<tensor<*xf64>, tensor<*xf64>>) -> tensor<*xf64> {
    %0 = toy.struct_access %arg0[0] : !toy.struct<tensor<*xf64>, tensor<*xf64>> -> tensor<*xf64>
    %1 = toy.transpose(%0 : tensor<*xf64>) to tensor<*xf64>
    %2 = toy.struct_access %arg0[1] : !toy.struct<tensor<*xf64>, tensor<*xf64>> -> tensor<*xf64>
    %3 = toy.transpose(%2 : tensor<*xf64>) to tensor<*xf64>
    %4 = toy.mul %1, %3 : tensor<*xf64>
    toy.return %4 : tensor<*xf64>

$ build/bin/toyc reference/tests/Ch7/struct-codegen.toy -emit=mlir -opt 2>&1
module {
  toy.func @main() {
    %0 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
    %1 = toy.transpose(%0 : tensor<2x3xf64>) to tensor<3x2xf64>
    %2 = toy.mul %1, %1 : tensor<3x2xf64>
    toy.print %2 : tensor<3x2xf64>
    toy.return
  }
}
```

No struct type, no struct operation, and the result is the same IR the non-struct program produces. This is the reason upstream's Ch7 needed no changes to the Ch6 lowering pipeline when it added a composite type: the type never reaches it.

## Try it

```console
$ build/bin/toyc <file>.toy -emit=mlir          # before
$ build/bin/toyc <file>.toy -emit=mlir -opt     # after
```

To see the patterns fire one pass at a time:

```console
$ build/bin/toyc <file>.toy -emit=mlir -opt --mlir-print-ir-after-all 2>&1 | less
```

`--mlir-pass-statistics` prints the pipeline with the counters each pass keeps. The canonicalizer keeps none, so what you learn from it is the pipeline's shape and what CSE did:

```console
$ build/bin/toyc docs/examples/ex.toy -emit=mlir -opt --mlir-pass-statistics 2>&1 | head -11
===-------------------------------------------------------------------------===
                         ... Pass statistics report ...
===-------------------------------------------------------------------------===
InlinerPass
'toy.func' Pipeline
  CanonicalizerPass
  (anonymous namespace)::ShapeInferencePass
  CanonicalizerPass
  CSEPass
    (S) 1 num-cse'd - Number of operations CSE'd
    (S) 0 num-dce'd - Number of operations DCE'd
```

That single CSE hit is the second `toy.transpose` in the running example. The two transposes become identical once inlining substitutes the same argument for both parameters, and CSE rather than a canonicalization pattern is what merges them.

## Pitfalls

A pattern must report failure, not assert, when it does not match. The greedy driver calls patterns speculatively on every candidate operation, so `getDefiningOp()` returning null is an ordinary outcome.

Mutating IR outside the `PatternRewriter` breaks the driver's bookkeeping. It tracks which operations changed in order to re-examine their users, and an unreported edit is invisible to it.

A canonicalization that does not terminate hangs the pass rather than failing it. Two patterns that undo each other are the usual cause; the driver stops after a fixed number of iterations, but a pair of patterns that keeps producing new operations will exhaust the limit and leave the IR in whatever state it reached.

Folding runs before and between pattern applications, so a fold and a pattern that target the same operation can race in a way that makes the final IR depend on benefits. If the output surprises you, `--mlir-print-ir-after-all` shows which mechanism acted.
