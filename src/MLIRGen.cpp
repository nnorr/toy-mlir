//===- MLIRGen.cpp - AST -> Toy dialect IR --------------------------------===//
//
// Adapted from mlir/examples/toy/Ch7/mlir/MLIRGen.cpp in the LLVM Project,
// under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Where Toy's semantics become operations.
//
// The tree walk is not here: MLIRGenImpl derives from ASTVisitor, which owns
// the one switch over ExprAST kinds and shares it with ASTDumper. What lives
// here is only the meaning of each node (which op to build, with which types
// and which location) plus the two tables that meaning depends on: the scoped
// symbol table for variables and the struct table for named struct types.
//
// Two conventions worth stating up front; they explain the return types:
//
//  * Every function is generic: parameters and results are tensor<*xf64>. Real
//    shapes arrive later, from inlining plus shape inference. That is why
//    prototypes are built with no result type at all and the result is patched
//    in afterwards from whatever the body returned.
//  * A hook returns FailureOr<Value>: expressions yield the value they define,
//    statements yield success with a null Value, and a failure has already
//    emitted its diagnostic. Upstream signals the same three cases with a null
//    Value plus a separate LogicalResult overload per statement kind.
//
//===----------------------------------------------------------------------===//

#include "toy/MLIRGen.h"
#include "toy/AST.h"
#include "toy/ASTVisitor.h"
#include "toy/Dialect.h"
#include "toy/Lexer.h"

#include "mlir/IR/Attributes.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Value.h"
#include "mlir/IR/Verifier.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopedHashTable.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

using namespace mlir::toy;
using namespace toy;

using llvm::ArrayRef;
using llvm::cast;
using llvm::dyn_cast;
using llvm::isa;
using llvm::SmallVector;
using llvm::StringRef;
using llvm::Twine;

namespace {

/// Emits Toy dialect IR for one parsed module.
///
/// One instance per module: the builder, the symbol table and the two maps are
/// all per-module state, and the class is created and destroyed by mlirGen()
/// below.
class MLIRGenImpl
    : public toy::ASTVisitor<MLIRGenImpl, mlir::FailureOr<mlir::Value>> {
  /// The dispatcher reaches the private visit* hooks below.
  friend class toy::ASTVisitor<MLIRGenImpl, mlir::FailureOr<mlir::Value>>;

public:
  MLIRGenImpl(mlir::MLIRContext &context) : builder(&context) {}

  /// Emits the module, or returns a null ModuleOp after a diagnostic.
  mlir::ModuleOp mlirGen(ModuleAST &moduleAST) {
    theModule = mlir::ModuleOp::create(builder.getUnknownLoc());

    for (auto &record : moduleAST) {
      if (FunctionAST *funcAST = dyn_cast<FunctionAST>(record.get())) {
        mlir::toy::FuncOp func = mlirGenFunction(*funcAST);
        if (!func)
          return nullptr;
        // Recorded so a later call can find the callee's result type; Toy has
        // no forward declarations, so definition order is call order.
        functionMap.insert({func.getName(), func});
      } else if (StructAST *str = dyn_cast<StructAST>(record.get())) {
        if (failed(mlirGenStruct(*str)))
          return nullptr;
      } else {
        llvm_unreachable("unknown record type");
      }
    }

    // Verify before handing the module on. A generator bug caught here points
    // at the op that is wrong; the same bug caught three passes later does not.
    if (failed(mlir::verify(theModule))) {
      theModule.emitError("module verification error");
      return nullptr;
    }

    return theModule;
  }

private:
  /// The module being built: one per Toy source file.
  mlir::ModuleOp theModule;

  /// Stateful op factory. Its insertion point is moved as generation descends
  /// into a function body, and every op is created at wherever it points.
  mlir::OpBuilder builder;

  /// Variable name -> (value, declaration). The declaration is kept because
  /// struct member access needs the declared type name, which the Value alone
  /// does not carry.
  llvm::ScopedHashTable<StringRef, std::pair<mlir::Value, VarDeclExprAST *>>
      symbolTable;
  using SymbolTableScopeT =
      llvm::ScopedHashTableScope<StringRef,
                                 std::pair<mlir::Value, VarDeclExprAST *>>;

  /// Functions generated so far, by name.
  llvm::StringMap<mlir::toy::FuncOp> functionMap;

  /// Struct name -> (the MLIR type, the AST node). The AST node stays reachable
  /// because member *indices* are resolved from the declaration order there.
  llvm::StringMap<std::pair<mlir::Type, StructAST *>> structMap;

  //===--------------------------------------------------------------------===//
  // Helpers
  //===--------------------------------------------------------------------===//

  /// Converts a front-end location to an MLIR one. Every op gets one of these,
  /// and they survive inlining, shape inference and both lowerings, which is
  /// why no debug-info bookkeeping appears in this file at all.
  mlir::Location loc(const Location &loc) {
    return mlir::FileLineColLoc::get(builder.getStringAttr(*loc.file), loc.line,
                                     loc.col);
  }

  /// Declares a variable in the current scope.
  ///
  /// D4: upstream returns failure with no diagnostic here, so a redeclaration
  /// makes the whole function vanish from the output with nothing printed.
  ///
  /// Note the missing "error: " in the message: emitError already prints that
  /// prefix. The other diagnostics in this file do spell it out, which is why
  /// they come out as "error: error: unknown variable ...", an upstream defect
  /// this file reproduces deliberately, because those strings are compared
  /// against toyc-ch7 byte for byte. This message is ours, so it is not
  /// doubled.
  llvm::LogicalResult declare(VarDeclExprAST &var, mlir::Value value) {
    if (symbolTable.count(var.getName()))
      return mlir::emitError(loc(var.loc()))
             << "variable '" << var.getName()
             << "' is already declared in this scope";
    symbolTable.insert(var.getName(), {value, &var});
    return mlir::success();
  }

  /// Builds a tensor type from a shape, unranked when the shape is empty.
  mlir::Type getType(ArrayRef<int64_t> shape) {
    if (shape.empty())
      return mlir::UnrankedTensorType::get(builder.getF64Type());
    return mlir::RankedTensorType::get(shape, builder.getF64Type());
  }

  /// Builds a type from a declared Toy type: a named struct, or a tensor.
  mlir::Type getType(const VarType &type, const Location &location) {
    if (!type.name.empty()) {
      auto it = structMap.find(type.name);
      if (it == structMap.end()) {
        mlir::emitError(loc(location))
            << "error: unknown struct type '" << type.name << "'";
        return nullptr;
      }
      return it->second.first;
    }

    return getType(type.shape);
  }

  //===--------------------------------------------------------------------===//
  // Records: structs, prototypes, functions
  //===--------------------------------------------------------------------===//

  /// Turns a struct definition into a StructType entry in the struct table.
  /// No IR is produced: a struct definition is a type, not a computation.
  llvm::LogicalResult mlirGenStruct(StructAST &str) {
    if (structMap.count(str.getName()))
      return mlir::emitError(loc(str.loc()))
             << "error: struct type with name `" << str.getName()
             << "' already exists";

    auto variables = str.getVariables();
    std::vector<mlir::Type> elementTypes;
    elementTypes.reserve(variables.size());
    for (auto &variable : variables) {
      if (variable->getInitVal())
        return mlir::emitError(loc(variable->loc()))
               << "error: variables within a struct definition must not have "
                  "initializers";
      if (!variable->getType().shape.empty())
        return mlir::emitError(loc(variable->loc()))
               << "error: variables within a struct definition must not have "
                  "initializers";

      mlir::Type type = getType(variable->getType(), variable->loc());
      if (!type)
        return mlir::failure();
      elementTypes.push_back(type);
    }

    structMap.try_emplace(str.getName(), StructType::get(elementTypes), &str);
    return mlir::success();
  }

  /// Builds the function op for a prototype, with no result type.
  ///
  /// The result is deliberately absent rather than unknown: it is filled in
  /// from the body's return below, and only shape inference decides its shape.
  mlir::toy::FuncOp mlirGenPrototype(PrototypeAST &proto) {
    auto location = loc(proto.loc());

    llvm::SmallVector<mlir::Type, 4> argTypes;
    argTypes.reserve(proto.getArgs().size());
    for (auto &arg : proto.getArgs()) {
      mlir::Type type = getType(arg->getType(), arg->loc());
      if (!type)
        return nullptr;
      argTypes.push_back(type);
    }
    auto funcType = builder.getFunctionType(argTypes, /*results=*/{});
    return mlir::toy::FuncOp::create(builder, location, proto.getName(),
                                     funcType);
  }

  /// Emits a whole function and appends it to the module.
  mlir::toy::FuncOp mlirGenFunction(FunctionAST &funcAST) {
    // The parameters live in this scope, so it has to outlive the body walk.
    SymbolTableScopeT varScope(symbolTable);

    builder.setInsertionPointToEnd(theModule.getBody());
    mlir::toy::FuncOp function = mlirGenPrototype(*funcAST.getProto());
    if (!function)
      return nullptr;

    mlir::Block &entryBlock = function.front();
    auto protoArgs = funcAST.getProto()->getArgs();

    // Block arguments are the parameters' values; bind the names to them.
    for (const auto nameValue :
         llvm::zip(protoArgs, entryBlock.getArguments())) {
      if (failed(declare(*std::get<0>(nameValue), std::get<1>(nameValue))))
        return nullptr;
    }

    builder.setInsertionPointToStart(&entryBlock);

    if (mlir::failed(mlirGenBlock(*funcAST.getBody()))) {
      // Leaving a half-built function in the module would fail verification
      // with a confusing error instead of the one already reported.
      function.erase();
      return nullptr;
    }

    // A Toy body need not end in `return`, but a block must end in a
    // terminator, so supply one.
    ReturnOp returnOp;
    if (!entryBlock.empty())
      returnOp = dyn_cast<ReturnOp>(entryBlock.back());
    if (!returnOp) {
      ReturnOp::create(builder, loc(funcAST.getProto()->loc()));
    } else if (returnOp.hasOperand()) {
      // The signature was built without a result; now that the body is known,
      // adopt the returned value's type.
      function.setType(
          builder.getFunctionType(function.getFunctionType().getInputs(),
                                  *returnOp.operand_type_begin()));
    }

    // Private visibility is what allows the inliner to delete a callee once it
    // has been inlined everywhere; `main` must stay, since it is the entry
    // point the JIT and the object file expose.
    if (funcAST.getProto()->getName() != "main")
      function.setPrivate();

    return function;
  }

  //===--------------------------------------------------------------------===//
  // Blocks and expression contexts
  //===--------------------------------------------------------------------===//

  /// Emits a block's statements in order.
  ///
  /// Variable declarations, `return` and `print` are handled here rather than
  /// through the dispatcher because each carries a statement-level rule that
  /// only applies at block level: a declaration adds to this scope, a return
  /// ends the block, and a print produces no value.
  llvm::LogicalResult mlirGenBlock(ExprASTList &blockAST) {
    SymbolTableScopeT varScope(symbolTable);
    for (auto &expr : blockAST) {
      if (auto *vardecl = dyn_cast<VarDeclExprAST>(expr.get())) {
        if (failed(visitVarDecl(*vardecl)))
          return mlir::failure();
        continue;
      }
      if (auto *ret = dyn_cast<ReturnExprAST>(expr.get()))
        return failed(visitReturn(*ret)) ? mlir::failure() : mlir::success();
      if (auto *print = dyn_cast<PrintExprAST>(expr.get())) {
        // Upstream returns success here, so a failed print exits 0 after its
        // diagnostic (deviation D12).
        if (mlir::failed(visitPrint(*print)))
          return mlir::failure();
        continue;
      }

      if (failed(mlirGenExpr(*expr)))
        return mlir::failure();
    }
    return mlir::success();
  }

  /// Emits an expression, rejecting the nodes that are statements.
  ///
  /// The parser can place a `print` inside an expression (`var a = print(b);`),
  /// and upstream's dispatch switch rejected that in its `default:` case. The
  /// visitor has a hook for every kind, so that check lives here instead, in
  /// the one place that knows the context is an expression.
  mlir::FailureOr<mlir::Value> mlirGenExpr(ExprAST &expr) {
    switch (expr.getKind()) {
    case ExprAST::Expr_VarDecl:
    case ExprAST::Expr_Return:
    case ExprAST::Expr_Print:
      mlir::emitError(loc(expr.loc()))
          << "MLIR codegen encountered an unhandled expr kind '"
          << Twine(expr.getKind()) << "'";
      return mlir::failure();
    default:
      return visit(expr);
    }
  }

  //===--------------------------------------------------------------------===//
  // Struct access resolution
  //===--------------------------------------------------------------------===//

  /// The struct definition an expression evaluates to, or null.
  ///
  /// Toy has no type checker: the "type" of an expression is recovered here,
  /// from the declaration a name resolves to, and recursively through nested
  /// `.` accesses.
  StructAST *getStructFor(ExprAST *expr) {
    llvm::StringRef structName;
    if (auto *decl = dyn_cast<VariableExprAST>(expr)) {
      auto varIt = symbolTable.lookup(decl->getName());
      if (!varIt.first)
        return nullptr;
      structName = varIt.second->getType().name;
    } else if (auto *access = dyn_cast<BinaryExprAST>(expr)) {
      if (access->getOp() != '.')
        return nullptr;
      // The member name is on the right of the access.
      auto *name = dyn_cast<VariableExprAST>(access->getRHS());
      if (!name)
        return nullptr;
      StructAST *parentStruct = getStructFor(access->getLHS());
      if (!parentStruct)
        return nullptr;

      VarDeclExprAST *decl = nullptr;
      for (auto &var : parentStruct->getVariables()) {
        if (var->getName() == name->getName()) {
          decl = var.get();
          break;
        }
      }
      if (!decl)
        return nullptr;
      structName = decl->getType().name;
    }
    if (structName.empty())
      return nullptr;

    auto structIt = structMap.find(structName);
    if (structIt == structMap.end())
      return nullptr;
    return structIt->second.second;
  }

  /// The element index a `.` access selects, by position in the definition.
  std::optional<size_t> getMemberIndex(BinaryExprAST &accessOp) {
    assert(accessOp.getOp() == '.' && "expected access operation");

    StructAST *structAST = getStructFor(accessOp.getLHS());
    if (!structAST)
      return std::nullopt;

    VariableExprAST *name = dyn_cast<VariableExprAST>(accessOp.getRHS());
    if (!name)
      return std::nullopt;

    auto structVars = structAST->getVariables();
    const auto *it = llvm::find_if(structVars, [&](auto &var) {
      return var->getName() == name->getName();
    });
    if (it == structVars.end())
      return std::nullopt;
    return it - structVars.begin();
  }

  //===--------------------------------------------------------------------===//
  // Constant attributes
  //===--------------------------------------------------------------------===//

  /// Flattens a tensor literal into the attribute a toy.constant carries.
  ///
  /// The data becomes an attribute rather than operands because it is known at
  /// compile time: `var a<2, 3> = [[1, 2, 3], [4, 5, 6]];` turns into one
  /// toy.constant holding dense<...> : tensor<2x3xf64>, not six values.
  mlir::DenseElementsAttr getConstantAttr(LiteralExprAST &lit) {
    std::vector<double> data;
    data.reserve(llvm::product_of(lit.getDims()));
    collectData(lit, data);

    mlir::Type elementType = builder.getF64Type();
    auto dataType = mlir::RankedTensorType::get(lit.getDims(), elementType);

    return mlir::DenseElementsAttr::get(dataType, llvm::ArrayRef(data));
  }

  /// The same for a bare number, which is a rank-0 tensor.
  mlir::DenseElementsAttr getConstantAttr(NumberExprAST &lit) {
    mlir::Type elementType = builder.getF64Type();
    auto dataType = mlir::RankedTensorType::get({}, elementType);

    return mlir::DenseElementsAttr::get(dataType,
                                        llvm::ArrayRef(lit.getValue()));
  }

  /// Builds the array attribute and the struct type for a struct literal.
  ///
  /// A struct constant is an array of its elements' constant attributes, which
  /// is what lets struct_access(struct_constant) fold to one element later.
  std::pair<mlir::ArrayAttr, mlir::Type>
  getConstantAttr(StructLiteralExprAST &lit) {
    std::vector<mlir::Attribute> attrElements;
    std::vector<mlir::Type> typeElements;

    for (auto &var : lit.getValues()) {
      if (auto *number = dyn_cast<NumberExprAST>(var.get())) {
        attrElements.push_back(getConstantAttr(*number));
        typeElements.push_back(getType(/*shape=*/{}));
      } else if (auto *lit = dyn_cast<LiteralExprAST>(var.get())) {
        attrElements.push_back(getConstantAttr(*lit));
        typeElements.push_back(getType(/*shape=*/{}));
      } else {
        auto *structLit = cast<StructLiteralExprAST>(var.get());
        auto attrTypePair = getConstantAttr(*structLit);
        attrElements.push_back(attrTypePair.first);
        typeElements.push_back(attrTypePair.second);
      }
    }
    mlir::ArrayAttr dataAttr = builder.getArrayAttr(attrElements);
    mlir::Type dataType = StructType::get(typeElements);
    return std::make_pair(dataAttr, dataType);
  }

  /// Flattens nested literals: [[1, 2], [3, 4]] becomes [1, 2, 3, 4].
  void collectData(ExprAST &expr, std::vector<double> &data) {
    if (auto *lit = dyn_cast<LiteralExprAST>(&expr)) {
      for (auto &value : lit->getValues())
        collectData(*value, data);
      return;
    }

    assert(isa<NumberExprAST>(expr) && "expected literal or number expr");
    data.push_back(cast<NumberExprAST>(expr).getValue());
  }

  //===--------------------------------------------------------------------===//
  // Per-node hooks, reached through ASTVisitor::visit()
  //===--------------------------------------------------------------------===//

  /// A declaration evaluates its initializer and binds the name to it.
  mlir::FailureOr<mlir::Value> visitVarDecl(VarDeclExprAST &vardecl) {
    auto *init = vardecl.getInitVal();
    if (!init) {
      mlir::emitError(loc(vardecl.loc()),
                      "missing initializer in variable declaration");
      return mlir::failure();
    }

    mlir::FailureOr<mlir::Value> value = mlirGenExpr(*init);
    if (failed(value))
      return mlir::failure();

    VarType varType = vardecl.getType();
    if (!varType.name.empty()) {
      // A struct-typed declaration is checked, not converted: there is no cast
      // between struct types.
      mlir::Type type = getType(varType, vardecl.loc());
      if (!type)
        return mlir::failure();
      if (type != value->getType()) {
        mlir::emitError(loc(vardecl.loc()))
            << "struct type of initializer is different than the variable "
               "declaration. Got "
            << value->getType() << ", but expected " << type;
        return mlir::failure();
      }
    } else if (!varType.shape.empty()) {
      // A declared shape is Toy's only reshape: emit one and let
      // canonicalization remove it when it turns out to be a no-op.
      value = mlir::Value(ReshapeOp::create(
          builder, loc(vardecl.loc()), getType(varType.shape), *value));
    }

    if (failed(declare(vardecl, *value)))
      return mlir::failure();
    return value;
  }

  /// `return` with or without a value.
  mlir::FailureOr<mlir::Value> visitReturn(ReturnExprAST &ret) {
    auto location = loc(ret.loc());

    mlir::Value expr = nullptr;
    if (ret.getExpr().has_value()) {
      mlir::FailureOr<mlir::Value> value = mlirGenExpr(**ret.getExpr());
      if (failed(value))
        return mlir::failure();
      expr = *value;
    }

    ReturnOp::create(builder, location,
                     expr ? ArrayRef(expr) : ArrayRef<mlir::Value>());
    return mlir::Value();
  }

  /// A number becomes a rank-0 constant.
  mlir::FailureOr<mlir::Value> visitNumber(NumberExprAST &num) {
    return mlir::Value(
        ConstantOp::create(builder, loc(num.loc()), num.getValue()));
  }

  /// A tensor literal becomes one constant holding the flattened data.
  mlir::FailureOr<mlir::Value> visitLiteral(LiteralExprAST &lit) {
    mlir::Type type = getType(lit.getDims());
    mlir::DenseElementsAttr dataAttribute = getConstantAttr(lit);

    return mlir::Value(
        ConstantOp::create(builder, loc(lit.loc()), type, dataAttribute));
  }

  /// A struct literal becomes one struct_constant.
  mlir::FailureOr<mlir::Value> visitStructLiteral(StructLiteralExprAST &lit) {
    mlir::ArrayAttr dataAttr;
    mlir::Type dataType;
    std::tie(dataAttr, dataType) = getConstantAttr(lit);

    return mlir::Value(StructConstantOp::create(builder, loc(lit.loc()),
                                                dataType, dataAttr));
  }

  /// A name resolves to the value bound at its declaration; nothing is emitted.
  mlir::FailureOr<mlir::Value> visitVariable(VariableExprAST &expr) {
    if (auto variable = symbolTable.lookup(expr.getName()).first)
      return variable;

    mlir::emitError(loc(expr.loc()), "error: unknown variable '")
        << expr.getName() << "'";
    return mlir::failure();
  }

  /// `+`, `*`, and the struct accessor `.`.
  mlir::FailureOr<mlir::Value> visitBinary(BinaryExprAST &binop) {
    // The left side is emitted first so that a diagnostic from it is reported
    // before anything is built for this operation.
    mlir::FailureOr<mlir::Value> lhs = mlirGenExpr(*binop.getLHS());
    if (failed(lhs))
      return mlir::failure();
    auto location = loc(binop.loc());

    // `.` is resolved at compile time to an index, so its right side is a
    // member name rather than a value and must not be emitted.
    if (binop.getOp() == '.') {
      std::optional<size_t> accessIndex = getMemberIndex(binop);
      if (!accessIndex) {
        mlir::emitError(location, "invalid access into struct expression");
        return mlir::failure();
      }
      return mlir::Value(
          StructAccessOp::create(builder, location, *lhs, *accessIndex));
    }

    mlir::FailureOr<mlir::Value> rhs = mlirGenExpr(*binop.getRHS());
    if (failed(rhs))
      return mlir::failure();

    switch (binop.getOp()) {
    case '+':
      return mlir::Value(AddOp::create(builder, location, *lhs, *rhs));
    case '*':
      return mlir::Value(MulOp::create(builder, location, *lhs, *rhs));
    }

    mlir::emitError(location, "invalid binary operator '")
        << binop.getOp() << "'";
    return mlir::failure();
  }

  /// `transpose` is a builtin with its own op; anything else is a call.
  mlir::FailureOr<mlir::Value> visitCall(CallExprAST &call) {
    llvm::StringRef callee = call.getCallee();
    auto location = loc(call.loc());

    SmallVector<mlir::Value, 4> operands;
    for (auto &expr : call.getArgs()) {
      mlir::FailureOr<mlir::Value> arg = mlirGenExpr(*expr);
      if (failed(arg))
        return mlir::failure();
      operands.push_back(*arg);
    }

    if (callee == "transpose") {
      if (call.getArgs().size() != 1) {
        mlir::emitError(location,
                        "MLIR codegen encountered an error: toy.transpose "
                        "does not accept multiple arguments");
        return mlir::failure();
      }
      return mlir::Value(TransposeOp::create(builder, location, operands[0]));
    }

    auto calledFuncIt = functionMap.find(callee);
    if (calledFuncIt == functionMap.end()) {
      mlir::emitError(location)
          << "no defined function found for '" << callee << "'";
      return mlir::failure();
    }
    mlir::toy::FuncOp calledFunc = calledFuncIt->second;
    // A call is an expression, so its callee has to produce a value. Upstream
    // indexes the empty result list and aborts.
    if (calledFunc.getFunctionType().getNumResults() == 0) {
      mlir::emitError(location)
          << "function '" << callee << "' does not return a value";
      return mlir::failure();
    }
    // The call takes the callee's declared result type, which at this point is
    // still unranked. Shape inference resolves it after inlining.
    return mlir::Value(
        GenericCallOp::create(builder, location,
                              calledFunc.getFunctionType().getResult(0), callee,
                              operands));
  }

  /// `print` produces no value, only an effect.
  mlir::FailureOr<mlir::Value> visitPrint(PrintExprAST &call) {
    mlir::FailureOr<mlir::Value> arg = mlirGenExpr(*call.getArg());
    if (failed(arg))
      return mlir::failure();

    PrintOp::create(builder, loc(call.loc()), *arg);
    return mlir::Value();
  }
};

} // namespace

namespace toy {

mlir::OwningOpRef<mlir::ModuleOp> mlirGen(mlir::MLIRContext &context,
                                          ModuleAST &moduleAST) {
  return MLIRGenImpl(context).mlirGen(moduleAST);
}

} // namespace toy
