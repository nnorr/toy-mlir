//===- LowerToAffine.cpp - Partial lowering of Toy to Affine + MemRef -----===//
//
// Adapted from mlir/examples/toy/Ch7/mlir/LowerToAffineLoops.cpp in the LLVM
// Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The first lowering, and the one that shows what "partial" buys you: the
// computational operations become affine loop nests over memrefs, while
// toy.print stays a Toy operation until the LLVM lowering deals with it. Two
// abstraction levels coexist in one function, which is the property the whole
// design rests on.
//
// This is also where values become buffers. A tensor is an SSA value with no
// memory behind it; a memref is a reference to memory. After this pass the
// data-flow graph no longer describes the contents of those buffers, only the
// loads and stores against them, which is why optimisations that need to
// reason about the data (fusion, redundant-load removal) must run at or above
// the affine level, never later.
//
// Preconditions: every call is inlined and every shape is known. The
// allocation strategy assumes it, too: one alloc per result, freed at the end
// of the block, valid only because Toy functions have no control flow.
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/ValueRange.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Support/TypeID.h"
#include "toy/Dialect.h"
#include "toy/Passes.h"

#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/Support/Casting.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

using namespace mlir;

//===----------------------------------------------------------------------===//
// Helpers shared by the conversion patterns
//===----------------------------------------------------------------------===//

/// The tensor -> memref type mapping: same shape, same element type, now with
/// memory behind it.
static MemRefType convertTensorToMemRef(RankedTensorType type) {
  return MemRefType::get(type.getShape(), type.getElementType());
}

/// Allocates a buffer for one operation's result and frees it at the end of the
/// block.
///
/// The alloc is hoisted to the top of the block and the dealloc sunk to just
/// before the terminator, so the buffer outlives every use no matter where the
/// pattern happened to be inserting. This is only sound because Toy functions
/// are a single block with no control flow; a real bufferization pass has to
/// solve placement properly.
static Value insertAllocAndDealloc(MemRefType type, Location loc,
                                   PatternRewriter &rewriter) {
  auto alloc = memref::AllocOp::create(rewriter, loc, type);

  auto *parentBlock = alloc->getBlock();
  alloc->moveBefore(&parentBlock->front());

  auto dealloc = memref::DeallocOp::create(rewriter, loc, alloc);
  dealloc->moveBefore(&parentBlock->back());
  return alloc;
}

/// Builds the body of the innermost loop: given the induction variables, return
/// the value to store at that index.
using LoopIterationFn =
    function_ref<Value(OpBuilder &rewriter, ValueRange loopIvs)>;

/// Replaces `op` with a loop nest, one loop per dimension of its result, whose
/// body is `processIteration`.
///
/// Every element-wise Toy operation lowers this way, so the nest construction
/// is written once and the patterns supply only what happens per element.
static void lowerOpToLoops(Operation *op, PatternRewriter &rewriter,
                           LoopIterationFn processIteration) {
  auto tensorType = llvm::cast<RankedTensorType>((*op->result_type_begin()));
  auto loc = op->getLoc();

  auto memRefType = convertTensorToMemRef(tensorType);
  auto alloc = insertAllocAndDealloc(memRefType, loc, rewriter);

  // Bounds are literals rather than values: the affine dialect's guarantee is
  // that loop bounds and access indices are affine functions, and that
  // guarantee is exactly what lets the later fusion and tiling passes prove
  // their transformations legal.
  SmallVector<int64_t, 4> lowerBounds(tensorType.getRank(), /*Value=*/0);
  SmallVector<int64_t, 4> steps(tensorType.getRank(), /*Value=*/1);
  affine::buildAffineLoopNest(
      rewriter, loc, lowerBounds, tensorType.getShape(), steps,
      [&](OpBuilder &nestedBuilder, Location loc, ValueRange ivs) {
        Value valueToStore = processIteration(nestedBuilder, ivs);
        affine::AffineStoreOp::create(nestedBuilder, loc, valueToStore, alloc,
                                      ivs);
      });

  // Uses of the operation's result now read the buffer instead.
  rewriter.replaceOp(op, alloc);
}

namespace {

//===----------------------------------------------------------------------===//
// Binary operations
//===----------------------------------------------------------------------===//

/// toy.add / toy.mul -> a loop nest doing one arith op per element.
///
/// Note `adaptor.getLhs()` rather than `op.getLhs()`: the adaptor hands back the
/// *converted* operands, which are memrefs, while the op still reports the
/// tensors it was built with. Matching happens on the old types, building on the
/// new ones.
template <typename BinaryOp, typename LoweredBinaryOp>
struct BinaryOpLowering : public OpConversionPattern<BinaryOp> {
  using OpConversionPattern<BinaryOp>::OpConversionPattern;
  using OpAdaptor = typename OpConversionPattern<BinaryOp>::OpAdaptor;

  LogicalResult
  matchAndRewrite(BinaryOp op, OpAdaptor adaptor,
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
};
using AddOpLowering = BinaryOpLowering<toy::AddOp, arith::AddFOp>;
using MulOpLowering = BinaryOpLowering<toy::MulOp, arith::MulFOp>;

//===----------------------------------------------------------------------===//
// Constant operations
//===----------------------------------------------------------------------===//

/// toy.constant -> a buffer plus one store per element.
///
/// The attribute held the whole array; memory has to be filled element by
/// element, so a rank-2 constant becomes rows x cols stores. Nothing here folds
/// them; that is LLVM's job at -O3, and watching it happen is instructive.
struct ConstantOpLowering : public OpConversionPattern<toy::ConstantOp> {
  using OpConversionPattern<toy::ConstantOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(toy::ConstantOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    DenseElementsAttr constantValue = op.getValue();
    Location loc = op.getLoc();

    auto tensorType = llvm::cast<RankedTensorType>(op.getType());
    auto memRefType = convertTensorToMemRef(tensorType);
    auto alloc = insertAllocAndDealloc(memRefType, loc, rewriter);

    // One index constant per distinct value up to the largest dimension,
    // created once and reused: emitting them inside the store recursion would
    // produce an arith.constant per element for CSE to clean up afterwards.
    auto valueShape = memRefType.getShape();
    SmallVector<Value, 8> constantIndices;

    if (!valueShape.empty()) {
      for (auto i : llvm::seq<int64_t>(0, *llvm::max_element(valueShape)))
        constantIndices.push_back(
            arith::ConstantIndexOp::create(rewriter, loc, i));
    } else {
      // A rank-0 tensor still needs index 0 to store its single element.
      constantIndices.push_back(
          arith::ConstantIndexOp::create(rewriter, loc, 0));
    }

    // Walk the shape depth-first, emitting a store when the recursion bottoms
    // out. The attribute's elements are visited in the same row-major order the
    // indices are generated, so a single forward iterator keeps them in step.
    SmallVector<Value, 2> indices;
    auto valueIt = constantValue.value_begin<FloatAttr>();
    std::function<void(uint64_t)> storeElements = [&](uint64_t dimension) {
      if (dimension == valueShape.size()) {
        affine::AffineStoreOp::create(
            rewriter, loc, arith::ConstantOp::create(rewriter, loc, *valueIt++),
            alloc, llvm::ArrayRef(indices));
        return;
      }

      for (uint64_t i = 0, e = valueShape[dimension]; i != e; ++i) {
        indices.push_back(constantIndices[i]);
        storeElements(dimension + 1);
        indices.pop_back();
      }
    };

    storeElements(/*dimension=*/0);

    rewriter.replaceOp(op, alloc);
    return success();
  }
};

//===----------------------------------------------------------------------===//
// Func operations
//===----------------------------------------------------------------------===//

/// toy.func -> func.func, for `main` only.
///
/// Every other function is expected to be gone: the inliner ran first, and a
/// surviving generic function would still have unranked parameters that this
/// pass could not allocate buffers for. Returning failure on a non-main
/// function leaves it illegal, and the partial conversion then reports it.
struct FuncOpLowering : public OpConversionPattern<toy::FuncOp> {
  using OpConversionPattern<toy::FuncOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(toy::FuncOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    if (op.getName() != "main")
      return failure();

    if (op.getNumArguments() || op.getFunctionType().getNumResults()) {
      return rewriter.notifyMatchFailure(op, [](Diagnostic &diag) {
        diag << "expected 'main' to have 0 inputs and 0 results";
      });
    }

    // The body moves across untouched; only the operation holding it changes.
    auto func = mlir::func::FuncOp::create(rewriter, op.getLoc(), op.getName(),
                                           op.getFunctionType());
    rewriter.inlineRegionBefore(op.getRegion(), func.getBody(), func.end());
    rewriter.eraseOp(op);
    return success();
  }
};

//===----------------------------------------------------------------------===//
// Print operations
//===----------------------------------------------------------------------===//

/// toy.print survives this pass; only its operand changes from a tensor to the
/// memref the operand was converted to.
///
/// This is the seam that makes the lowering partial, and the reason PrintOp's
/// ODS accepts a memref as well as a tensor.
struct PrintOpLowering : public OpConversionPattern<toy::PrintOp> {
  using OpConversionPattern<toy::PrintOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(toy::PrintOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    rewriter.modifyOpInPlace(op,
                             [&] { op->setOperands(adaptor.getOperands()); });
    return success();
  }
};

//===----------------------------------------------------------------------===//
// Return operations
//===----------------------------------------------------------------------===//

/// toy.return -> func.return, for the operand-less form only.
///
/// A returned value would mean a function other than main survived inlining.
struct ReturnOpLowering : public OpConversionPattern<toy::ReturnOp> {
  using OpConversionPattern<toy::ReturnOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(toy::ReturnOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    if (op.hasOperand())
      return failure();

    rewriter.replaceOpWithNewOp<func::ReturnOp>(op);
    return success();
  }
};

//===----------------------------------------------------------------------===//
// Transpose operations
//===----------------------------------------------------------------------===//

/// toy.transpose -> a loop nest that reads with the indices reversed.
///
/// No data moves in a transposed order; the loop simply reads [j, i] while
/// writing [i, j]. Expressing the operation this way is what makes the affine
/// analysis able to reason about it later.
struct TransposeOpLowering : public OpConversionPattern<toy::TransposeOp> {
  using OpConversionPattern<toy::TransposeOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(toy::TransposeOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    auto loc = op->getLoc();
    lowerOpToLoops(op, rewriter, [&](OpBuilder &builder, ValueRange loopIvs) {
      Value input = adaptor.getInput();

      SmallVector<Value, 2> reverseIvs(llvm::reverse(loopIvs));
      return affine::AffineLoadOp::create(builder, loc, input, reverseIvs);
    });
    return success();
  }
};

} // namespace

//===----------------------------------------------------------------------===//
// ToyToAffineLoweringPass
//===----------------------------------------------------------------------===//

namespace {
struct ToyToAffineLoweringPass
    : public PassWrapper<ToyToAffineLoweringPass, OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ToyToAffineLoweringPass)

  StringRef getArgument() const override { return "toy-to-affine"; }

  /// Declared so the pass manager loads these dialects before running: a pass
  /// may only build operations from dialects it asked for.
  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<affine::AffineDialect, func::FuncDialect,
                    memref::MemRefDialect>();
  }

  void runOnOperation() final;
};
} // namespace

void ToyToAffineLoweringPass::runOnOperation() {
  // A conversion target is a statement about what may remain, not a list of
  // rewrites. The framework's job is to make the IR satisfy it using the
  // patterns provided, and to fail if it cannot.
  ConversionTarget target(getContext());

  target.addLegalDialect<affine::AffineDialect, BuiltinDialect,
                         arith::ArithDialect, func::FuncDialect,
                         memref::MemRefDialect>();

  // Toy as a whole is illegal, with one exception carved out: toy.print is
  // legal once its operands are no longer tensors. Marking it dynamically legal
  // rather than plainly legal is what forces its operand to be rewritten while
  // leaving the operation itself alone. Per-operation rules always win over
  // per-dialect ones, so the order of these three statements is irrelevant.
  target.addIllegalDialect<toy::ToyDialect>();
  target.addDynamicallyLegalOp<toy::PrintOp>([](toy::PrintOp op) {
    return llvm::none_of(op->getOperandTypes(),
                         [](Type type) { return llvm::isa<TensorType>(type); });
  });

  RewritePatternSet patterns(&getContext());
  patterns.add<AddOpLowering, ConstantOpLowering, FuncOpLowering, MulOpLowering,
               PrintOpLowering, ReturnOpLowering, TransposeOpLowering>(
      &getContext());

  // Partial, not full: operations that are neither legal nor convertible are an
  // error, but legal operations from any dialect may stay.
  if (failed(
          applyPartialConversion(getOperation(), target, std::move(patterns))))
    signalPassFailure();
}

std::unique_ptr<Pass> mlir::toy::createLowerToAffinePass() {
  return std::make_unique<ToyToAffineLoweringPass>();
}
