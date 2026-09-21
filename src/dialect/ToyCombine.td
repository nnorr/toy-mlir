//===- ToyCombine.td - Pattern Match Optimizations for Toy -*- tablegen -*-===//
//
// Adapted from mlir/examples/toy/Ch7 in the LLVM Project, under the Apache
// License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declarative Rewrite Rules (DRR): source pattern, result pattern, optional
// constraints. mlir-tblgen compiles each Pat<> into a RewritePattern subclass
// with matchAndRewrite() written for you; see build/src/ToyCombine.inc for the
// result, and dialect/ToyCombine.cpp for the C++ pattern that could not be
// expressed this way.
//
// The trade-off: DRR handles "this tree of ops becomes that tree of ops"
// concisely, and stops being usable as soon as the rewrite needs real logic.
//
//===----------------------------------------------------------------------===//

#ifndef TOY_COMBINE
#define TOY_COMBINE

include "mlir/IR/PatternBase.td"
include "toy/Ops.td"

/// For reference, the DRR base class:
///
/// class Pattern<
///    dag sourcePattern, list<dag> resultPatterns,
///    list<dag> additionalConstraints = [],
///    list<dag> supplementalPatterns = [],
///    dag benefitsAdded = (addBenefit 0)
/// >;

//===----------------------------------------------------------------------===//
// Plain structural rewrite
//===----------------------------------------------------------------------===//

// Reshape(Reshape(x)) = Reshape(x). The inner reshape is dead afterwards, and
// Pure on ReshapeOp is what allows the driver to delete it.
def ReshapeReshapeOptPattern : Pat<(ReshapeOp(ReshapeOp $arg)),
                                   (ReshapeOp $arg)>;

//===----------------------------------------------------------------------===//
// Rewrite calling into C++
//===----------------------------------------------------------------------===//

// Reshape(Constant(x)) = Constant(x reshaped). The new attribute has to be
// computed, not pattern-matched, so DRR delegates to a C++ expression.
def ReshapeConstant :
  NativeCodeCall<"$0.reshape(::llvm::cast<ShapedType>($1.getType()))">;
def FoldConstantReshapeOptPattern : Pat<
  (ReshapeOp:$res (ConstantOp $arg)),
  (ConstantOp (ReshapeConstant $arg, $res))>;

//===----------------------------------------------------------------------===//
// Rewrite guarded by a constraint
//===----------------------------------------------------------------------===//

// Reshape(x) = x when the reshape changes nothing. The predicate is a C++
// expression over the matched values, checked after the structural match.
def TypesAreIdentical : Constraint<CPred<"$0.getType() == $1.getType()">>;
def RedundantReshapeOptPattern : Pat<
  (ReshapeOp:$res $arg), (replaceWithValue $arg),
  [(TypesAreIdentical $res, $arg)]>;

#endif // TOY_COMBINE
