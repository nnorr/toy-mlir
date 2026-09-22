//===- Ops.cpp - Hand-written parts of the Toy operations -----------------===//
//
// Adapted from mlir/examples/toy/Ch7/mlir/Dialect.cpp in the LLVM Project,
// under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Everything Ops.td declared but could not generate: custom builders, the
// parsers and printers for the three operations whose syntax is not
// declarative, the verifiers that check relationships ODS constraints cannot
// state, the shape rules, and the interface methods.
//
// Read this next to include/toy/Ops.td, where each `let hasVerifier = 1`,
// `let hasCustomAssemblyFormat = 1`, `let builders = [...]` and
// `DeclareOpInterfaceMethods<...>` there is a promise that is kept here, and the
// build fails if one is missing.
//
// The generated definitions are included at the bottom of this file, which is
// the one place in the project that may do so.
//
//===----------------------------------------------------------------------===//

#include "toy/Dialect.h"

#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/IR/ValueRange.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "mlir/Interfaces/FunctionImplementation.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/SMLoc.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>

using namespace mlir;
using namespace mlir::toy;

//===----------------------------------------------------------------------===//
// Shared binary-operation syntax
//===----------------------------------------------------------------------===//

/// Parses `%lhs, %rhs attr-dict : type`, where `type` is either the type of
/// everything or a full functional type.
///
/// AddOp and MulOp share this instead of each declaring an assemblyFormat,
/// because the declarative form cannot express "one type means all three".
static mlir::ParseResult parseBinaryOp(mlir::OpAsmParser &parser,
                                       mlir::OperationState &result) {
  SmallVector<mlir::OpAsmParser::UnresolvedOperand, 2> operands;
  SMLoc operandsLoc = parser.getCurrentLocation();
  Type type;
  if (parser.parseOperandList(operands, /*requiredOperandCount=*/2) ||
      parser.parseOptionalAttrDict(result.attributes) ||
      parser.parseColonType(type))
    return mlir::failure();

  // A functional type spells the operand and result types separately, which is
  // what the printer falls back to when they disagree.
  if (FunctionType funcType = llvm::dyn_cast<FunctionType>(type)) {
    if (parser.resolveOperands(operands, funcType.getInputs(), operandsLoc,
                               result.operands))
      return mlir::failure();
    result.addTypes(funcType.getResults());
    return mlir::success();
  }

  // Otherwise the single type applies to both operands and the result.
  if (parser.resolveOperands(operands, type, result.operands))
    return mlir::failure();
  result.addTypes(type);
  return mlir::success();
}

/// Prints the short form when every type matches, the functional form when they
/// do not. Before shape inference runs the operands are ranked and the result is
/// not, so the same operation prints both ways at different points in the
/// pipeline.
static void printBinaryOp(mlir::OpAsmPrinter &printer, mlir::Operation *op) {
  printer << " " << op->getOperands();
  printer.printOptionalAttrDict(op->getAttrs());
  printer << " : ";

  Type resultType = *op->result_type_begin();
  if (llvm::all_of(op->getOperandTypes(),
                   [=](Type type) { return type == resultType; })) {
    printer << resultType;
    return;
  }

  printer.printFunctionalType(op->getOperandTypes(), op->getResultTypes());
}

/// Element-wise ops need operands of one shape. Unranked operands have not
/// been inferred yet and pass; the lowering loops over the lhs shape and would
/// otherwise read past the end of a smaller rhs.
static llvm::LogicalResult verifyElementwiseOperands(mlir::Operation *op) {
  Type lhsType = op->getOperand(0).getType();
  Type rhsType = op->getOperand(1).getType();
  if (!llvm::isa<RankedTensorType>(lhsType) ||
      !llvm::isa<RankedTensorType>(rhsType) || lhsType == rhsType)
    return mlir::success();
  return op->emitOpError("operand shapes must match, got ")
         << lhsType << " and " << rhsType;
}

//===----------------------------------------------------------------------===//
// ConstantOp
//===----------------------------------------------------------------------===//

/// Builds a rank-0 constant from a plain double, so callers emitting a scalar
/// literal do not have to construct the attribute and its type themselves.
void ConstantOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                       double value) {
  auto dataType = RankedTensorType::get({}, builder.getF64Type());
  auto dataAttribute = DenseElementsAttr::get(dataType, value);
  ConstantOp::build(builder, state, dataType, dataAttribute);
}

/// Parses `toy.constant dense<...> : tensor<...>`.
///
/// The result type is not written separately: it comes from the attribute,
/// which is exactly the coupling that makes the declarative format unusable
/// here.
mlir::ParseResult ConstantOp::parse(mlir::OpAsmParser &parser,
                                    mlir::OperationState &result) {
  mlir::DenseElementsAttr value;
  if (parser.parseOptionalAttrDict(result.attributes) ||
      parser.parseAttribute(value, "value", result.attributes))
    return failure();

  result.addTypes(value.getType());
  return success();
}

void ConstantOp::print(mlir::OpAsmPrinter &printer) {
  printer << " ";
  // `value` is elided from the dictionary because it is printed positionally
  // just below; printing it twice would not parse back.
  printer.printOptionalAttrDict((*this)->getAttrs(), /*elidedAttrs=*/{"value"});
  printer << getValue();
}

/// Checks an initializer attribute against the type it is meant to produce.
///
/// Recursive because a struct constant's initializer is an array of the
/// elements' own initializers, and each of those has to be checked the same way.
static llvm::LogicalResult verifyConstantForType(mlir::Type type,
                                                 mlir::Attribute opaqueValue,
                                                 mlir::Operation *op) {
  if (llvm::isa<mlir::TensorType>(type)) {
    auto attrValue = llvm::dyn_cast<mlir::DenseFPElementsAttr>(opaqueValue);
    if (!attrValue)
      return op->emitError("constant of TensorType must be initialized by "
                           "a DenseFPElementsAttr, got ")
             << opaqueValue;

    // An unranked result says nothing about the data, so there is nothing left
    // to check. This is the normal state before shape inference.
    auto resultType = llvm::dyn_cast<mlir::RankedTensorType>(type);
    if (!resultType)
      return success();

    auto attrType = llvm::cast<mlir::RankedTensorType>(attrValue.getType());
    if (attrType.getRank() != resultType.getRank()) {
      return op->emitOpError("return type must match the one of the attached "
                             "value attribute: ")
             << attrType.getRank() << " != " << resultType.getRank();
    }

    for (int dim = 0, dimE = attrType.getRank(); dim < dimE; ++dim) {
      if (attrType.getShape()[dim] != resultType.getShape()[dim]) {
        return op->emitOpError(
                   "return type shape mismatches its attribute at dimension ")
               << dim << ": " << attrType.getShape()[dim]
               << " != " << resultType.getShape()[dim];
      }
    }
    return mlir::success();
  }

  auto resultType = llvm::cast<StructType>(type);
  llvm::ArrayRef<mlir::Type> resultElementTypes = resultType.getElementTypes();

  auto attrValue = llvm::dyn_cast<ArrayAttr>(opaqueValue);
  if (!attrValue || attrValue.getValue().size() != resultElementTypes.size())
    return op->emitError("constant of StructType must be initialized by an "
                         "ArrayAttr with the same number of elements, got ")
           << opaqueValue;

  llvm::ArrayRef<mlir::Attribute> attrElementValues = attrValue.getValue();
  for (const auto it : llvm::zip(resultElementTypes, attrElementValues))
    if (failed(verifyConstantForType(std::get<0>(it), std::get<1>(it), op)))
      return mlir::failure();
  return mlir::success();
}

llvm::LogicalResult ConstantOp::verify() {
  return verifyConstantForType(getResult().getType(), getValue(), *this);
}

void ConstantOp::inferShapes() {
  getResult().setType(cast<TensorType>(getValue().getType()));
}

//===----------------------------------------------------------------------===//
// AddOp
//===----------------------------------------------------------------------===//

/// Builds an add whose result shape is not known yet: MLIRGen has no way to
/// compute it before shape inference, so the result starts unranked and every
/// Toy operation is built this way.
void AddOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                  mlir::Value lhs, mlir::Value rhs) {
  state.addTypes(UnrankedTensorType::get(builder.getF64Type()));
  state.addOperands({lhs, rhs});
}

mlir::ParseResult AddOp::parse(mlir::OpAsmParser &parser,
                               mlir::OperationState &result) {
  return parseBinaryOp(parser, result);
}

void AddOp::print(mlir::OpAsmPrinter &p) { printBinaryOp(p, *this); }

void AddOp::inferShapes() { getResult().setType(getLhs().getType()); }

llvm::LogicalResult AddOp::verify() { return verifyElementwiseOperands(*this); }

//===----------------------------------------------------------------------===//
// CastOp
//===----------------------------------------------------------------------===//

void CastOp::inferShapes() { getResult().setType(getInput().getType()); }

/// Whether this cast is meaningful, asked by CastOpInterface's verifier.
///
/// A Toy cast only ever adds or removes rank information, so the element types
/// must agree and two ranked types must be identical. A cast that changed a
/// shape would silently reinterpret data.
bool CastOp::areCastCompatible(TypeRange inputs, TypeRange outputs) {
  if (inputs.size() != 1 || outputs.size() != 1)
    return false;
  TensorType input = llvm::dyn_cast<TensorType>(inputs.front());
  TensorType output = llvm::dyn_cast<TensorType>(outputs.front());
  if (!input || !output || input.getElementType() != output.getElementType())
    return false;
  return !input.hasRank() || !output.hasRank() || input == output;
}

//===----------------------------------------------------------------------===//
// FuncOp
//===----------------------------------------------------------------------===//

void FuncOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                   llvm::StringRef name, mlir::FunctionType type,
                   llvm::ArrayRef<mlir::NamedAttribute> attrs) {
  // FunctionOpInterface supplies the boilerplate: the symbol name attribute,
  // the function type attribute, and an entry block whose arguments match the
  // signature.
  buildWithEntryBlock(builder, state, name, type, attrs, type.getInputs());
}

mlir::ParseResult FuncOp::parse(mlir::OpAsmParser &parser,
                                mlir::OperationState &result) {
  // Toy's function syntax is the standard one, so the shared implementation
  // parses it. The callback exists only to say how an argument/result type list
  // becomes a FunctionType, which is the one dialect-specific decision.
  auto buildFuncType =
      [](mlir::Builder &builder, llvm::ArrayRef<mlir::Type> argTypes,
         llvm::ArrayRef<mlir::Type> results,
         mlir::function_interface_impl::VariadicFlag,
         std::string &) { return builder.getFunctionType(argTypes, results); };

  return mlir::function_interface_impl::parseFunctionOp(
      parser, result, /*allowVariadic=*/false,
      getFunctionTypeAttrName(result.name), buildFuncType,
      getArgAttrsAttrName(result.name), getResAttrsAttrName(result.name));
}

void FuncOp::print(mlir::OpAsmPrinter &p) {
  mlir::function_interface_impl::printFunctionOp(
      p, *this, /*isVariadic=*/false, getFunctionTypeAttrName(),
      getArgAttrsAttrName(), getResAttrsAttrName());
}

//===----------------------------------------------------------------------===//
// GenericCallOp
//===----------------------------------------------------------------------===//

void GenericCallOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                          mlir::Type resultType, StringRef callee,
                          ArrayRef<mlir::Value> arguments) {
  state.addTypes(resultType);
  state.addOperands(arguments);
  state.addAttribute("callee",
                     mlir::SymbolRefAttr::get(builder.getContext(), callee));
}

//===----------------------------------------------------------------------===//
// CallOpInterface
//
// These four methods are the entire reason MLIR's inliner can process Toy: they
// let it find the callee, rewrite it, and reach the argument operands without
// knowing that `toy.generic_call` exists.
//===----------------------------------------------------------------------===//

CallInterfaceCallable GenericCallOp::getCallableForCallee() {
  return (*this)->getAttrOfType<SymbolRefAttr>("callee");
}

void GenericCallOp::setCalleeFromCallable(CallInterfaceCallable callee) {
  (*this)->setAttr("callee", cast<SymbolRefAttr>(callee));
}

/// The callee must be a toy.func taking as many arguments as are passed.
/// Without this an arity mismatch surfaces after inlining as a shape inference
/// failure that never mentions the call.
llvm::LogicalResult
GenericCallOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  auto callee =
      symbolTable.lookupNearestSymbolFrom<FuncOp>(*this, getCalleeAttr());
  if (!callee)
    return emitOpError() << "'" << getCallee()
                         << "' does not reference a toy.func";
  if (callee.getNumArguments() != getInputs().size())
    return emitOpError() << "passes " << getInputs().size() << " arguments to '"
                         << getCallee() << "', which takes "
                         << callee.getNumArguments();
  return mlir::success();
}

Operation::operand_range GenericCallOp::getArgOperands() { return getInputs(); }

MutableOperandRange GenericCallOp::getArgOperandsMutable() {
  return getInputsMutable();
}

//===----------------------------------------------------------------------===//
// MulOp
//===----------------------------------------------------------------------===//

void MulOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                  mlir::Value lhs, mlir::Value rhs) {
  state.addTypes(UnrankedTensorType::get(builder.getF64Type()));
  state.addOperands({lhs, rhs});
}

mlir::ParseResult MulOp::parse(mlir::OpAsmParser &parser,
                               mlir::OperationState &result) {
  return parseBinaryOp(parser, result);
}

void MulOp::print(mlir::OpAsmPrinter &p) { printBinaryOp(p, *this); }

void MulOp::inferShapes() { getResult().setType(getLhs().getType()); }

llvm::LogicalResult MulOp::verify() { return verifyElementwiseOperands(*this); }

//===----------------------------------------------------------------------===//
// ReturnOp
//===----------------------------------------------------------------------===//

/// Checks the return against its function's signature.
///
/// This is the verifier ODS cannot generate: the constraint relates two
/// operations, and only one of them is `this`. The HasParent<"FuncOp"> trait
/// has already guaranteed the parent, so the cast below cannot fail.
llvm::LogicalResult ReshapeOp::verify() {
  auto inputType = llvm::dyn_cast<RankedTensorType>(getInput().getType());
  if (!inputType || !inputType.hasStaticShape())
    return mlir::success();
  int64_t inputCount = inputType.getNumElements();
  int64_t resultCount = getType().getNumElements();
  if (inputCount == resultCount || inputCount == 1)
    return mlir::success();
  return emitOpError() << "cannot reshape " << inputCount << " elements into "
                       << getType();
}

llvm::LogicalResult ReturnOp::verify() {
  auto function = cast<FuncOp>((*this)->getParentOp());

  if (getNumOperands() > 1)
    return emitOpError() << "expects at most 1 return operand";

  const auto &results = function.getFunctionType().getResults();
  if (getNumOperands() != results.size())
    return emitOpError() << "does not return the same number of values ("
                         << getNumOperands() << ") as the enclosing function ("
                         << results.size() << ")";

  if (!hasOperand())
    return mlir::success();

  auto inputType = *operand_type_begin();
  auto resultType = results.front();

  // An unranked type on either side is a shape that has not been inferred yet,
  // not a mismatch. Rejecting it here would make the IR invalid between
  // MLIRGen and the shape inference pass.
  if (inputType == resultType ||
      llvm::isa<mlir::UnrankedTensorType>(inputType) ||
      llvm::isa<mlir::UnrankedTensorType>(resultType))
    return mlir::success();

  return emitError() << "type of return operand (" << inputType
                     << ") doesn't match function result type (" << resultType
                     << ")";
}

//===----------------------------------------------------------------------===//
// StructAccessOp
//===----------------------------------------------------------------------===//

/// Builds an access whose result type is read out of the struct being indexed,
/// so callers pass a value and an index and nothing else.
void StructAccessOp::build(mlir::OpBuilder &b, mlir::OperationState &state,
                           mlir::Value input, size_t index) {
  StructType structTy = llvm::cast<StructType>(input.getType());
  assert(index < structTy.getNumElementTypes());
  mlir::Type resultType = structTy.getElementTypes()[index];

  build(b, state, resultType, input, b.getI64IntegerAttr(index));
}

llvm::LogicalResult StructAccessOp::verify() {
  StructType structTy = llvm::cast<StructType>(getInput().getType());
  size_t indexValue = getIndex();
  if (indexValue >= structTy.getNumElementTypes())
    return emitOpError()
           << "index should be within the range of the input struct type";
  mlir::Type resultType = getResult().getType();
  if (resultType != structTy.getElementTypes()[indexValue])
    return emitOpError() << "must have the same result type as the struct "
                            "element referred to by the index";
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// StructConstantOp
//===----------------------------------------------------------------------===//

llvm::LogicalResult StructConstantOp::verify() {
  return verifyConstantForType(getResult().getType(), getValue(), *this);
}

//===----------------------------------------------------------------------===//
// TransposeOp
//===----------------------------------------------------------------------===//

void TransposeOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                        mlir::Value value) {
  state.addTypes(UnrankedTensorType::get(builder.getF64Type()));
  state.addOperands(value);
}

void TransposeOp::inferShapes() {
  auto arrayTy = llvm::cast<RankedTensorType>(getOperand().getType());
  SmallVector<int64_t, 2> dims(llvm::reverse(arrayTy.getShape()));
  getResult().setType(RankedTensorType::get(dims, arrayTy.getElementType()));
}

llvm::LogicalResult TransposeOp::verify() {
  auto inputType = llvm::dyn_cast<RankedTensorType>(getOperand().getType());
  auto resultType = llvm::dyn_cast<RankedTensorType>(getType());
  // Nothing to check until both shapes are known.
  if (!inputType || !resultType)
    return mlir::success();

  auto inputShape = inputType.getShape();
  if (!std::equal(inputShape.begin(), inputShape.end(),
                  resultType.getShape().rbegin())) {
    return emitError()
           << "expected result shape to be a transpose of the input";
  }
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// TableGen'd op method definitions
//===----------------------------------------------------------------------===//

#define GET_OP_CLASSES
#include "toy/Ops.cpp.inc"
