//===- AST.h - Node definitions for the Toy AST ---------------------------===//
//
// Adapted from mlir/examples/toy/Ch7 in the LLVM Project, under the Apache
// License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The parse tree: pure data, owned through unique_ptr, discriminated by a Kind
// enum for LLVM-style RTTI (isa<>/dyn_cast<>/cast<>).
//
// Nothing here knows about MLIR, and nothing here knows about its consumers:
// there is no accept(), no codegen(), no dump() member. Traversal lives in
// ASTVisitor.h and the consumers are ASTDumper and MLIRGen. That keeps the
// bottom of the dependency graph free of the IR, which is what lets
// tests/FrontendTests link the parser without a line of MLIR.
//
// Structure of a module:
//
//   ModuleAST
//     RecordAST            (Function | Struct)
//       FunctionAST  -> PrototypeAST + ExprASTList (the body block)
//       StructAST    -> VarDeclExprAST*
//
//===----------------------------------------------------------------------===//

#ifndef TOY_AST_H
#define TOY_AST_H

#include "toy/Lexer.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toy {

/// A declared variable type: either a shape (`var a<2, 3>`) or the name of a
/// struct (`Struct value`). Both empty means "infer it".
struct VarType {
  std::string name;
  std::vector<int64_t> shape;
};

/// Base class for all expression nodes.
class ExprAST {
public:
  enum ExprASTKind {
    Expr_VarDecl,
    Expr_Return,
    Expr_Num,
    Expr_Literal,
    Expr_StructLiteral,
    Expr_Var,
    Expr_BinOp,
    Expr_Call,
    Expr_Print,
  };

  ExprAST(ExprASTKind kind, Location location)
      : kind(kind), location(std::move(location)) {}
  virtual ~ExprAST() = default;

  ExprASTKind getKind() const { return kind; }

  const Location &loc() { return location; }

private:
  const ExprASTKind kind;
  Location location;
};

/// A block: the statements of a function body, in order.
using ExprASTList = std::vector<std::unique_ptr<ExprAST>>;

/// A numeric literal, e.g. "1.0".
class NumberExprAST : public ExprAST {
  double val;

public:
  NumberExprAST(Location loc, double val)
      : ExprAST(Expr_Num, std::move(loc)), val(val) {}

  double getValue() { return val; }

  static bool classof(const ExprAST *c) { return c->getKind() == Expr_Num; }
};

/// A tensor literal, e.g. "[[1, 2], [3, 4]]". `dims` is the shape recovered
/// from the nesting; `values` is the flattened list of elements, each of which
/// is itself a NumberExprAST or a nested LiteralExprAST.
class LiteralExprAST : public ExprAST {
  std::vector<std::unique_ptr<ExprAST>> values;
  std::vector<int64_t> dims;

public:
  LiteralExprAST(Location loc, std::vector<std::unique_ptr<ExprAST>> values,
                 std::vector<int64_t> dims)
      : ExprAST(Expr_Literal, std::move(loc)), values(std::move(values)),
        dims(std::move(dims)) {}

  llvm::ArrayRef<std::unique_ptr<ExprAST>> getValues() { return values; }
  llvm::ArrayRef<int64_t> getDims() { return dims; }

  static bool classof(const ExprAST *c) { return c->getKind() == Expr_Literal; }
};

/// A struct literal, e.g. "{[[1, 2]], [[3, 4]]}".
class StructLiteralExprAST : public ExprAST {
  std::vector<std::unique_ptr<ExprAST>> values;

public:
  StructLiteralExprAST(Location loc,
                       std::vector<std::unique_ptr<ExprAST>> values)
      : ExprAST(Expr_StructLiteral, std::move(loc)), values(std::move(values)) {
  }

  llvm::ArrayRef<std::unique_ptr<ExprAST>> getValues() { return values; }

  static bool classof(const ExprAST *c) {
    return c->getKind() == Expr_StructLiteral;
  }
};

/// A reference to a variable, e.g. "a".
class VariableExprAST : public ExprAST {
  std::string name;

public:
  VariableExprAST(Location loc, llvm::StringRef name)
      : ExprAST(Expr_Var, std::move(loc)), name(name) {}

  llvm::StringRef getName() { return name; }

  static bool classof(const ExprAST *c) { return c->getKind() == Expr_Var; }
};

/// A variable definition, e.g. "var a<2, 3> = [1, 2, 3, 4, 5, 6];".
///
/// In Toy a definition is also the only way to reshape: the declared shape and
/// the initializer's shape may differ as long as the element count matches.
class VarDeclExprAST : public ExprAST {
  std::string name;
  VarType type;
  std::unique_ptr<ExprAST> initVal;

public:
  VarDeclExprAST(Location loc, llvm::StringRef name, VarType type,
                 std::unique_ptr<ExprAST> initVal = nullptr)
      : ExprAST(Expr_VarDecl, std::move(loc)), name(name),
        type(std::move(type)), initVal(std::move(initVal)) {}

  llvm::StringRef getName() { return name; }
  ExprAST *getInitVal() { return initVal.get(); }
  const VarType &getType() { return type; }

  static bool classof(const ExprAST *c) { return c->getKind() == Expr_VarDecl; }
};

/// A return statement, with or without a value.
class ReturnExprAST : public ExprAST {
  std::optional<std::unique_ptr<ExprAST>> expr;

public:
  ReturnExprAST(Location loc, std::optional<std::unique_ptr<ExprAST>> expr)
      : ExprAST(Expr_Return, std::move(loc)), expr(std::move(expr)) {}

  std::optional<ExprAST *> getExpr() {
    if (expr.has_value())
      return expr->get();
    return std::nullopt;
  }

  static bool classof(const ExprAST *c) { return c->getKind() == Expr_Return; }
};

/// A binary operation. Toy has four: + - * and the struct accessor '.'.
class BinaryExprAST : public ExprAST {
  char op;
  std::unique_ptr<ExprAST> lhs, rhs;

public:
  char getOp() { return op; }
  ExprAST *getLHS() { return lhs.get(); }
  ExprAST *getRHS() { return rhs.get(); }

  BinaryExprAST(Location loc, char op, std::unique_ptr<ExprAST> lhs,
                std::unique_ptr<ExprAST> rhs)
      : ExprAST(Expr_BinOp, std::move(loc)), op(op), lhs(std::move(lhs)),
        rhs(std::move(rhs)) {}

  static bool classof(const ExprAST *c) { return c->getKind() == Expr_BinOp; }
};

/// A call to a user-defined function, or to the builtin `transpose`.
class CallExprAST : public ExprAST {
  std::string callee;
  std::vector<std::unique_ptr<ExprAST>> args;

public:
  CallExprAST(Location loc, const std::string &callee,
              std::vector<std::unique_ptr<ExprAST>> args)
      : ExprAST(Expr_Call, std::move(loc)), callee(callee),
        args(std::move(args)) {}

  llvm::StringRef getCallee() { return callee; }
  llvm::ArrayRef<std::unique_ptr<ExprAST>> getArgs() { return args; }

  static bool classof(const ExprAST *c) { return c->getKind() == Expr_Call; }
};

/// A call to the builtin `print`. Separate from CallExprAST because it lowers
/// to its own operation rather than to a call.
class PrintExprAST : public ExprAST {
  std::unique_ptr<ExprAST> arg;

public:
  PrintExprAST(Location loc, std::unique_ptr<ExprAST> arg)
      : ExprAST(Expr_Print, std::move(loc)), arg(std::move(arg)) {}

  ExprAST *getArg() { return arg.get(); }

  static bool classof(const ExprAST *c) { return c->getKind() == Expr_Print; }
};

/// A function signature: name plus parameters. Parameters are VarDecls so that
/// a declared struct type (`def f(Struct s)`) can be carried the same way.
class PrototypeAST {
  Location location;
  std::string name;
  std::vector<std::unique_ptr<VarDeclExprAST>> args;

public:
  PrototypeAST(Location location, const std::string &name,
               std::vector<std::unique_ptr<VarDeclExprAST>> args)
      : location(std::move(location)), name(name), args(std::move(args)) {}

  const Location &loc() { return location; }
  llvm::StringRef getName() const { return name; }
  llvm::ArrayRef<std::unique_ptr<VarDeclExprAST>> getArgs() { return args; }
};

/// Base class for a top-level record in a module.
class RecordAST {
public:
  enum RecordASTKind {
    Record_Function,
    Record_Struct,
  };

  RecordAST(RecordASTKind kind) : kind(kind) {}
  virtual ~RecordAST() = default;

  RecordASTKind getKind() const { return kind; }

private:
  const RecordASTKind kind;
};

/// A function definition: signature plus body block.
class FunctionAST : public RecordAST {
  std::unique_ptr<PrototypeAST> proto;
  std::unique_ptr<ExprASTList> body;

public:
  FunctionAST(std::unique_ptr<PrototypeAST> proto,
              std::unique_ptr<ExprASTList> body)
      : RecordAST(Record_Function), proto(std::move(proto)),
        body(std::move(body)) {}

  PrototypeAST *getProto() { return proto.get(); }
  ExprASTList *getBody() { return body.get(); }

  static bool classof(const RecordAST *r) {
    return r->getKind() == Record_Function;
  }
};

/// A struct definition: a name and its member declarations.
class StructAST : public RecordAST {
  Location location;
  std::string name;
  std::vector<std::unique_ptr<VarDeclExprAST>> variables;

public:
  StructAST(Location location, const std::string &name,
            std::vector<std::unique_ptr<VarDeclExprAST>> variables)
      : RecordAST(Record_Struct), location(std::move(location)), name(name),
        variables(std::move(variables)) {}

  const Location &loc() { return location; }
  llvm::StringRef getName() const { return name; }
  llvm::ArrayRef<std::unique_ptr<VarDeclExprAST>> getVariables() {
    return variables;
  }

  static bool classof(const RecordAST *r) {
    return r->getKind() == Record_Struct;
  }
};

/// A parsed file: the records it defines, in source order.
class ModuleAST {
  std::vector<std::unique_ptr<RecordAST>> records;

public:
  ModuleAST(std::vector<std::unique_ptr<RecordAST>> records)
      : records(std::move(records)) {}

  auto begin() { return records.begin(); }
  auto end() { return records.end(); }
};

} // namespace toy

#endif // TOY_AST_H
