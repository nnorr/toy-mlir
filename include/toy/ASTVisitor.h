//===- ASTVisitor.h - CRTP dispatcher over the Toy AST --------------------===//
//
// Part of toy-mlir. Not present in the upstream tutorial.
//
//===----------------------------------------------------------------------===//
//
// One switch over ExprAST::getKind(), shared by every consumer of the AST.
//
// The upstream tutorial writes that switch twice: once in AST.cpp to dump the
// tree, once in MLIRGen.cpp to emit IR. Both are exhaustive switches over the
// same nine node kinds, and both must be edited when a node is added. Here the
// dispatch lives in one place and the consumers provide hooks:
//
//   class ASTDumper : public ASTVisitor<ASTDumper>   // RetTy = void
//   class MLIRGenImpl                                // RetTy = FailureOr<Value>
//       : public ASTVisitor<MLIRGenImpl, mlir::FailureOr<mlir::Value>>
//
// Dispatch is not virtual, since Derived is a template parameter, so each
// visitor picks its own return type. That is what lets MLIRGen return a
// value-or-error while the dumper returns nothing. This is the shape clang uses
// in clang/AST/StmtVisitor.h, and the same one used in 06_llvm_tutorial.
//
// Adding a node kind to AST.h makes the switch below fire -Wswitch in exactly
// one file, and every visitor then fails to compile until it handles it. That
// is the trade the Expression Problem makes here: closed node set, open set of
// operations over it. MLIR makes the opposite trade, where the op set is open
// and every transformation must cope with ops it has never seen. docs/02-ast.md
// sets the two side by side; docs/07-interfaces.md shows how MLIR gets away
// with it.
//
//===----------------------------------------------------------------------===//

#ifndef TOY_ASTVISITOR_H
#define TOY_ASTVISITOR_H

#include "toy/AST.h"

#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"

namespace toy {

/// Dispatches an ExprAST to a per-node hook on Derived.
///
/// Derived declares its hooks private and befriends this class, so the hooks
/// are reachable only through visit().
template <typename Derived, typename RetTy = void> class ASTVisitor {
  Derived &derived() { return *static_cast<Derived *>(this); }

public:
  RetTy visit(ExprAST &expr) {
    switch (expr.getKind()) {
    case ExprAST::Expr_VarDecl:
      return derived().visitVarDecl(llvm::cast<VarDeclExprAST>(expr));
    case ExprAST::Expr_Return:
      return derived().visitReturn(llvm::cast<ReturnExprAST>(expr));
    case ExprAST::Expr_Num:
      return derived().visitNumber(llvm::cast<NumberExprAST>(expr));
    case ExprAST::Expr_Literal:
      return derived().visitLiteral(llvm::cast<LiteralExprAST>(expr));
    case ExprAST::Expr_StructLiteral:
      return derived().visitStructLiteral(
          llvm::cast<StructLiteralExprAST>(expr));
    case ExprAST::Expr_Var:
      return derived().visitVariable(llvm::cast<VariableExprAST>(expr));
    case ExprAST::Expr_BinOp:
      return derived().visitBinary(llvm::cast<BinaryExprAST>(expr));
    case ExprAST::Expr_Call:
      return derived().visitCall(llvm::cast<CallExprAST>(expr));
    case ExprAST::Expr_Print:
      return derived().visitPrint(llvm::cast<PrintExprAST>(expr));
    }
    // No default: a new ExprASTKind makes -Wswitch fire here rather than
    // silently falling through at run time.
    llvm_unreachable("unknown ExprASTKind");
  }
};

} // namespace toy

#endif // TOY_ASTVISITOR_H
