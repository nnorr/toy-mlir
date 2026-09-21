//===- Interfaces.cpp - Dialect and op interfaces for Toy -----------------===//
//
// Adapted from mlir/examples/toy/Ch7/mlir/Dialect.cpp in the LLVM Project,
// under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The contracts that let code which has never heard of Toy transform Toy IR.
//
// Two kinds appear here, and the difference matters:
//
//   A *dialect* interface answers questions about the dialect as a whole.
//   ToyInlinerInterface is one: MLIR's inliner asks "may I inline across this
//   dialect's calls, and how do I fix up a type mismatch", and the dialect
//   answers once for every operation it owns.
//
//   An *op* interface is implemented per operation. ShapeInference is Toy's own
//   (ShapeInferenceInterface.td), and CallOpInterface/CastOpInterface/
//   FunctionOpInterface come from MLIR; their per-operation implementations are
//   in Ops.cpp. Only the generated dispatch thunk lives here.
//
// Neither is a pass. The inliner pass, the canonicalizer and CSE are all generic
// MLIR passes; what is written in this file is the reason they work on a dialect
// that did not exist when they were written.
//
//===----------------------------------------------------------------------===//

#include "toy/Dialect.h"

#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/Value.h"
#include "mlir/IR/ValueRange.h"
#include "mlir/Transforms/InliningUtils.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Casting.h"

#include <cassert>

using namespace mlir;
using namespace mlir::toy;

/// The out-of-line dispatch for Toy's own op interface.
///
/// Placed with the dialect rather than with the shape inference pass (where
/// upstream puts it) because the interface is part of what the dialect promises:
/// every operation that declares ShapeInferenceOpInterface in Ops.td needs this
/// symbol, so ToyDialect would otherwise fail to link without ToyPasses.
#include "toy/ShapeInferenceOpInterfaces.cpp.inc"

//===----------------------------------------------------------------------===//
// ToyInlinerInterface
//===----------------------------------------------------------------------===//

namespace {

/// Toy's answers to the generic inliner.
///
/// Inlining is not an optimization here, it is a prerequisite: Toy functions are
/// shape-polymorphic, so shapes can only be inferred once each call has been
/// replaced by the callee's body with the caller's concrete types in place.
struct ToyInlinerInterface : public DialectInlinerInterface {
  using DialectInlinerInterface::DialectInlinerInterface;

  //===--------------------------------------------------------------------===//
  // Analysis Hooks
  //===--------------------------------------------------------------------===//

  /// Toy has no recursion, no indirect calls and no cost model worth consulting,
  /// so every call, operation and region may always be inlined.
  bool isLegalToInline(Operation *call, Operation *callable,
                       bool wouldBeCloned) const final {
    return true;
  }

  bool isLegalToInline(Operation *, Region *, bool, IRMapping &) const final {
    return true;
  }

  bool isLegalToInline(Region *, Region *, bool, IRMapping &) const final {
    return true;
  }

  //===--------------------------------------------------------------------===//
  // Transformation Hooks
  //===--------------------------------------------------------------------===//

  /// Rewires an inlined `toy.return`.
  ///
  /// The terminator cannot survive inlining: control no longer leaves a
  /// function here. Whatever used the call's results must use the returned
  /// values directly instead, and the inliner leaves that substitution to the
  /// dialect because only the dialect knows which operands of its terminator
  /// correspond to which results.
  void handleTerminator(Operation *op, ValueRange valuesToRepl) const final {
    // toy.return is the only terminator Toy has.
    auto returnOp = cast<ReturnOp>(op);

    assert(returnOp.getNumOperands() == valuesToRepl.size());
    for (const auto &it : llvm::enumerate(returnOp.getOperands()))
      valuesToRepl[it.index()].replaceAllUsesWith(it.value());
  }

  /// Bridges the type mismatch at a call boundary.
  ///
  /// A call passes `tensor<2x3xf64>` to a parameter typed `tensor<*xf64>`. The
  /// inliner refuses to paper over that itself. It asks the dialect for an
  /// operation converting one to the other, and without this hook inlining
  /// silently does nothing at all.
  Operation *materializeCallConversion(OpBuilder &builder, Value input,
                                       Type resultType,
                                       Location conversionLoc) const final {
    return CastOp::create(builder, conversionLoc, resultType, input);
  }
};

} // namespace

//===----------------------------------------------------------------------===//
// Registration
//===----------------------------------------------------------------------===//

/// Declared in Ops.td's extraClassDeclaration, called from
/// ToyDialect::initialize(). Living here is what keeps ToyInlinerInterface in
/// the anonymous namespace above: nothing outside this file can name it.
void ToyDialect::registerInterfaces() {
  addInterfaces<ToyInlinerInterface>();
}
