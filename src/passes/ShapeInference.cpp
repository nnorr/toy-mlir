//===- ShapeInference.cpp - Propagate shapes through a function -----------===//
//
// Adapted from mlir/examples/toy/Ch7/mlir/ShapeInferencePass.cpp in the LLVM
// Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Toy functions are generic: a parameter is `tensor<*xf64>` until a call site
// says otherwise. By the time this pass runs the inliner has replaced every
// call with the callee's body, so the only unranked types left are results
// waiting to be derived from operands that are already ranked.
//
// The pass itself knows nothing about add, mul or transpose. It asks each
// operation for its ShapeInference interface and lets the operation set its own
// result type, which is why a new shaped Toy op needs an inferShapes() and no
// edit here.
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/Types.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Support/TypeID.h"
#include "toy/Dialect.h"
#include "toy/Passes.h"
#include "toy/ShapeInferenceInterface.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/DebugLog.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>

#define DEBUG_TYPE "shape-inference"

using namespace mlir;
using namespace toy;

// The interface's Concept/Model definitions are compiled into the dialect
// library (dialect/Interfaces.cpp), not here: the interface is part of the Toy
// IR's vocabulary, and an op declaring it must not depend on this pass being
// linked in. Upstream places them in this file because a tutorial chapter is a
// single executable.

namespace {

/// Intra-procedural shape inference over one function.
///
/// Algorithm:
///   1) Collect every operation that returns a dynamically shaped tensor --
///      those are the ones needing inference.
///   2) Repeatedly take an operation from that set whose operands are all
///      ranked (so its shape rule has something to work from) and let it infer
///      its results. Stop when no such operation exists.
///   3) A non-empty worklist at that point means the shapes cannot be resolved.
///
/// Step 2 is a fixed-point loop rather than a single pass over the block
/// because operations are not necessarily in an order where one traversal would
/// see every operand already inferred.
struct ShapeInferencePass
    : public mlir::PassWrapper<ShapeInferencePass, OperationPass<toy::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ShapeInferencePass)

  StringRef getArgument() const override { return "toy-shape-inference"; }

  void runOnOperation() override {
    auto f = getOperation();

    llvm::SmallPtrSet<mlir::Operation *, 16> opWorklist;
    f.walk([&](mlir::Operation *op) {
      if (returnsDynamicShape(op))
        opWorklist.insert(op);
    });

    while (!opWorklist.empty()) {
      auto nextop = llvm::find_if(opWorklist, allOperandsInferred);
      if (nextop == opWorklist.end())
        break;

      Operation *op = *nextop;
      opWorklist.erase(op);

      LDBG() << "Inferring shape for: " << *op;
      if (auto shapeOp = dyn_cast<ShapeInference>(op)) {
        shapeOp.inferShapes();
      } else {
        // The contract is not optional: an op producing an unranked result with
        // no way to resolve it would stall the loop silently.
        op->emitError("unable to infer shape of operation without shape "
                      "inference interface");
        return signalPassFailure();
      }
    }

    if (!opWorklist.empty()) {
      f.emitError("Shape inference failed, ")
          << opWorklist.size() << " operations couldn't be inferred\n";
      signalPassFailure();
    }
  }

  /// True if every operand of `op` already has a known rank, which is the
  /// precondition for asking it to infer its results.
  static bool allOperandsInferred(Operation *op) {
    return llvm::all_of(op->getOperandTypes(), [](Type operandType) {
      return llvm::isa<RankedTensorType>(operandType);
    });
  }

  /// True if `op` produces a result whose shape is still unknown.
  static bool returnsDynamicShape(Operation *op) {
    return llvm::any_of(op->getResultTypes(), [](Type resultType) {
      return !llvm::isa<RankedTensorType>(resultType);
    });
  }
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::toy::createShapeInferencePass() {
  return std::make_unique<ShapeInferencePass>();
}
