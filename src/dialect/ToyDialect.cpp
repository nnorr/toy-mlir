//===- ToyDialect.cpp - Registration of the Toy dialect -------------------===//
//
// Adapted from mlir/examples/toy/Ch7/mlir/Dialect.cpp in the LLVM Project,
// under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// What the MLIRContext learns when it loads the Toy dialect.
//
// initialize() is the whole registration surface: after it returns, the context
// can parse `toy.` operations, round-trip `!toy.struct<...>`, and hand generic
// transformations the dialect hooks they ask for. Nothing else in the project
// calls it. The generated constructor does, the first time anyone asks the
// context for this dialect.
//
//===----------------------------------------------------------------------===//

#include "toy/Dialect.h"

#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/Operation.h"

#include "llvm/Support/Casting.h"

using namespace mlir;
using namespace mlir::toy;

/// The generated constructor and destructor, plus the dialect's TypeID.
#include "toy/Dialect.cpp.inc"

//===----------------------------------------------------------------------===//
// ToyDialect
//===----------------------------------------------------------------------===//

void ToyDialect::initialize() {
  // The operation list generated from Ops.td. Registering an operation is what
  // gives it a verifier, a parser and a printer; an unregistered `toy.foo`
  // would still parse in generic form, but nothing would ever check it.
  addOperations<
#define GET_OP_LIST
#include "toy/Ops.cpp.inc"
      >();

  // Hooks for transformations that know nothing about Toy. Today that is the
  // inliner, which needs to be told that Toy calls may be inlined and how to
  // bridge a type mismatch at a call boundary. Defined in
  // dialect/Interfaces.cpp, which keeps ToyInlinerInterface private to that
  // file.
  registerInterfaces();

  // Without this the parser rejects `!toy.struct<...>`, and StructType::get
  // would build instances the printer cannot spell. Defined in
  // dialect/StructType.cpp: registering a type needs its storage class to be
  // complete, and that stays private to the file implementing the type.
  registerTypes();
}

/// Turns an Attribute produced by a folder back into an operation.
///
/// Folding replaces an operation with an Attribute, but the IR can only hold
/// operations, so whoever folded has to materialize the constant again. A
/// generic transformation cannot know which Toy operation carries which kind of
/// attribute, so it asks the dialect. Getting this wrong shows up as struct
/// constants that fail to rematerialize after inlining.
mlir::Operation *ToyDialect::materializeConstant(mlir::OpBuilder &builder,
                                                 mlir::Attribute value,
                                                 mlir::Type type,
                                                 mlir::Location loc) {
  if (llvm::isa<StructType>(type))
    return StructConstantOp::create(builder, loc, type,
                                    llvm::cast<mlir::ArrayAttr>(value));
  return ConstantOp::create(builder, loc, type,
                            llvm::cast<mlir::DenseElementsAttr>(value));
}
