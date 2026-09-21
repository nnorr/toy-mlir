//===- StructType.cpp - The !toy.struct type ------------------------------===//
//
// Adapted from mlir/examples/toy/Ch7/mlir/Dialect.cpp in the LLVM Project,
// under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A parametric type, end to end: storage, uniquing, and the syntax.
//
// An MLIR Type is a handle, not an object: StructType is two words wide and is
// copied by value everywhere. The data, which is the list of element types,
// lives in a StructTypeStorage owned by the MLIRContext and uniqued on its
// contents. That is why `a == b` on two types is a pointer comparison, and why
// building the same struct type twice costs a hash lookup rather than an
// allocation.
//
// The three pieces the uniquer needs from a storage class are a key type, a way
// to compare a key against an existing instance, and a way to build a new
// instance from a key. Everything else below is registration and syntax.
//
//===----------------------------------------------------------------------===//

#include "toy/Dialect.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/TypeSupport.h"
#include "mlir/IR/Types.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/SMLoc.h"

#include <cassert>

using namespace mlir;
using namespace mlir::toy;

//===----------------------------------------------------------------------===//
// StructTypeStorage
//===----------------------------------------------------------------------===//

namespace mlir {
namespace toy {
namespace detail {

/// The uniqued payload behind a StructType.
struct StructTypeStorage : public mlir::TypeStorage {
  /// Structs are uniqued structurally: two structs with the same element types
  /// are the same type, whatever they were named in the source.
  using KeyTy = llvm::ArrayRef<mlir::Type>;

  StructTypeStorage(llvm::ArrayRef<mlir::Type> elementTypes)
      : elementTypes(elementTypes) {}

  /// Asked by the uniquer before it decides to construct a new instance.
  bool operator==(const KeyTy &key) const { return key == elementTypes; }

  /// Both ArrayRef and Type already hash, so this exists only to be explicit
  /// about what the identity of a struct type is.
  static llvm::hash_code hashKey(const KeyTy &key) {
    return llvm::hash_value(key);
  }

  static KeyTy getKey(llvm::ArrayRef<mlir::Type> elementTypes) {
    return KeyTy(elementTypes);
  }

  /// Every allocation a storage instance needs must come from `allocator`: the
  /// caller's ArrayRef is a temporary, and the storage outlives it for as long
  /// as the context does.
  static StructTypeStorage *construct(mlir::TypeStorageAllocator &allocator,
                                      const KeyTy &key) {
    llvm::ArrayRef<mlir::Type> elementTypes = allocator.copyInto(key);

    return new (allocator.allocate<StructTypeStorage>())
        StructTypeStorage(elementTypes);
  }

  llvm::ArrayRef<mlir::Type> elementTypes;
};

} // namespace detail
} // namespace toy
} // namespace mlir

//===----------------------------------------------------------------------===//
// Registration
//===----------------------------------------------------------------------===//

/// Registers `!toy.struct` with the dialect.
///
/// This lives here rather than in ToyDialect::initialize() because addTypes<>
/// instantiates the storage uniquer, which needs the *complete*
/// StructTypeStorage above, and Dialect.h only forward-declares it. Registering
/// types through a hook defined beside the storage is what in-tree MLIR dialects
/// do; upstream Toy keeps everything in one Dialect.cpp and never faces the
/// question.
void ToyDialect::registerTypes() { addTypes<StructType>(); }

//===----------------------------------------------------------------------===//
// StructType
//===----------------------------------------------------------------------===//

StructType StructType::get(llvm::ArrayRef<mlir::Type> elementTypes) {
  assert(!elementTypes.empty() && "expected at least 1 element type");

  // Base::get is the uniquer: it hashes the trailing arguments into a key,
  // returns the existing instance if there is one, and calls
  // StructTypeStorage::construct otherwise. The context comes from the element
  // types because a type cannot exist outside the context that uniqued it.
  mlir::MLIRContext *ctx = elementTypes.front().getContext();
  return Base::get(ctx, elementTypes);
}

llvm::ArrayRef<mlir::Type> StructType::getElementTypes() {
  return getImpl()->elementTypes;
}

//===----------------------------------------------------------------------===//
// Type syntax
//===----------------------------------------------------------------------===//

/// Parses the dialect's own types.
///
/// MLIR has already consumed `!toy.` and dispatched here, so this method owns
/// everything after the dialect name:
///
///   struct-type ::= `struct` `<` type (`,` type)* `>`
///
/// Every MLIR parse function returns ParseResult, a LogicalResult that converts
/// to true on *failure*, which is what makes the `||` chains below read as "if
/// anything went wrong, give up".
mlir::Type ToyDialect::parseType(mlir::DialectAsmParser &parser) const {
  // Parse: `struct` `<`
  if (parser.parseKeyword("struct") || parser.parseLess())
    return Type();

  SmallVector<mlir::Type, 1> elementTypes;
  do {
    SMLoc typeLoc = parser.getCurrentLocation();
    mlir::Type elementType;
    if (parser.parseType(elementType))
      return nullptr;

    // Toy structs hold tensors or other structs, nothing else. Rejecting here
    // rather than in a verifier means a malformed type never enters the context
    // in the first place.
    if (!llvm::isa<mlir::TensorType, StructType>(elementType)) {
      parser.emitError(typeLoc, "element type for a struct must either "
                                "be a TensorType or a StructType, got: ")
          << elementType;
      return Type();
    }
    elementTypes.push_back(elementType);

    // Parse the optional: `,`
  } while (succeeded(parser.parseOptionalComma()));

  // Parse: `>`
  if (parser.parseGreater())
    return Type();
  return StructType::get(elementTypes);
}

/// Prints the dialect's own types, in the syntax parseType accepts. The two
/// have to agree or the IR stops round-tripping.
void ToyDialect::printType(mlir::Type type,
                           mlir::DialectAsmPrinter &printer) const {
  // StructType is the only type Toy registers, so a failed cast here is a bug
  // in addTypes<> rather than bad input.
  StructType structType = llvm::cast<StructType>(type);

  printer << "struct<";
  llvm::interleaveComma(structType.getElementTypes(), printer);
  printer << '>';
}
