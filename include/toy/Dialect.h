//===- Dialect.h - The Toy dialect ----------------------------------------===//
//
// Adapted from mlir/examples/toy/Ch7 in the LLVM Project, under the Apache
// License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The public face of the Toy IR: the dialect class, the operation classes, and
// the one type Toy defines itself.
//
// Most of this file is generated. Ops.td produces Dialect.h.inc (the ToyDialect
// class) and Ops.h.inc (one class per operation, each a wrapper around an
// Operation*; see docs/05-dialect.md on Op vs Operation). What is left to
// write by hand is StructType, because a type with parameters needs a storage
// class, and storage classes are not yet expressible in ODS for this tutorial's
// shape.
//
//===----------------------------------------------------------------------===//

#ifndef TOY_DIALECT_H
#define TOY_DIALECT_H

#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "mlir/Interfaces/CastInterfaces.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "toy/ShapeInferenceInterface.h"

namespace mlir {
namespace toy {
namespace detail {
/// Defined in dialect/StructType.cpp. Forward-declared here because the
/// generated code below needs the name, not the layout.
struct StructTypeStorage;
} // namespace detail
} // namespace toy
} // namespace mlir

/// The generated ToyDialect class.
#include "toy/Dialect.h.inc"

//===----------------------------------------------------------------------===//
// Toy Operations
//===----------------------------------------------------------------------===//

/// The generated operation classes: ConstantOp, AddOp, CastOp, FuncOp,
/// GenericCallOp, MulOp, PrintOp, ReshapeOp, ReturnOp, StructAccessOp,
/// StructConstantOp, TransposeOp.
#define GET_OP_CLASSES
#include "toy/Ops.h.inc"

namespace mlir {
namespace toy {

//===----------------------------------------------------------------------===//
// Toy Types
//===----------------------------------------------------------------------===//

/// A fixed sequence of element types, spelled
/// `!toy.struct<tensor<*xf64>, tensor<*xf64>>`.
///
/// Like every MLIR Type this is a value-semantics handle: the object is two
/// words wide, copied freely, and the data lives in a uniqued storage instance
/// owned by the MLIRContext. Two StructTypes with equal element types *are* the
/// same pointer, which is why comparing types is a pointer comparison.
class StructType : public mlir::Type::TypeBase<StructType, mlir::Type,
                                               detail::StructTypeStorage> {
public:
  using Base::Base;

  /// Gets the uniqued instance for these element types. There must be at least
  /// one.
  static StructType get(llvm::ArrayRef<mlir::Type> elementTypes);

  /// The element types of this struct.
  llvm::ArrayRef<mlir::Type> getElementTypes();

  /// The number of elements this struct holds.
  size_t getNumElementTypes() { return getElementTypes().size(); }

  /// Required by TypeBase for diagnostics and registration.
  static constexpr StringLiteral name = "toy.struct";
};

} // namespace toy
} // namespace mlir

#endif // TOY_DIALECT_H
