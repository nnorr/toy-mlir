//===- Folders.cpp - Constant folding for Toy operations ------------------===//
//
// Adapted from mlir/examples/toy/Ch7/mlir/ToyCombine.cpp in the LLVM Project,
// under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Folding, as distinct from canonicalization.
//
// A folder answers "what constant is this operation, if any" and returns an
// Attribute; a canonicalization pattern rewrites the IR. Folders are cheaper,
// since no new operations are built, and the greedy driver runs them
// constantly. This is where the ConstantLike operations report their own value
// and where struct field access collapses.
//
// The last one is what makes Ch7's structs disappear: inlining turns every field
// access into struct_access(struct_constant), each fold replaces it with the
// element's attribute, and by the time lowering starts there is no struct left
// for the affine or LLVM conversions to handle. That is why adding a type to the
// language required no change to the lowering pipeline at all.
//
// These live in the dialect rather than with the passes because a folder is part
// of an operation's own definition (`let hasFolder = 1` in Ops.td), not a
// transformation someone chooses to run.
//
//===----------------------------------------------------------------------===//

#include "toy/Dialect.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/OpDefinition.h"

#include "llvm/Support/Casting.h"

#include <cstddef>

using namespace mlir;
using namespace mlir::toy;

/// A constant folds to the attribute it already carries.
OpFoldResult ConstantOp::fold(FoldAdaptor adaptor) { return getValue(); }

/// Likewise for a struct constant, whose value is an array of the elements'
/// attributes.
OpFoldResult StructConstantOp::fold(FoldAdaptor adaptor) { return getValue(); }

/// struct_access(struct_constant, i) -> the i-th element attribute.
///
/// The adaptor hands over what the operands folded to, not the operands
/// themselves: a null entry means "not a constant", which is the common case
/// before inlining has exposed the constant.
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
