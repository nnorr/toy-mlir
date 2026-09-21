//===- ASTDumper.h - Print the Toy AST as a tree --------------------------===//
//
// Part of toy-mlir. Upstream keeps this as a free `dump(ModuleAST &)` in
// parser/AST.cpp; here it is a visitor, and it can print in two formats.
//
//===----------------------------------------------------------------------===//
//
// Two styles, because this repo is read next to two tutorials:
//
//   Style::Toy           byte-identical to `toyc-ch7 -emit=ast`, so the output
//                        can be diffed against upstream (tests/compat).
//   Style::Kaleidoscope  the format used by 06_llvm_tutorial's ASTDumper, so an
//                        AST from either compiler can be read side by side.
//
// The dumper is the AST's second consumer and the one with nothing behind it:
// it includes no MLIR header at all, which is the check that AST.h really is
// independent of the IR.
//
//===----------------------------------------------------------------------===//

#ifndef TOY_ASTDUMPER_H
#define TOY_ASTDUMPER_H

#include "toy/AST.h"
#include "toy/ASTVisitor.h"

#include "llvm/Support/raw_ostream.h"

namespace toy {

/// Prints an AST as an indented tree.
class ASTDumper : public ASTVisitor<ASTDumper> {
  friend class ASTVisitor<ASTDumper>; // reaches the visit* hooks below

public:
  enum class Style {
    /// Upstream Toy's format: `Proto 'name' @file:line:col`, curly-braced
    /// blocks, two-space indent starting at one level in.
    Toy,
    /// 06_llvm_tutorial's format: `Prototype name (args) @line`, captioned
    /// children, no braces.
    Kaleidoscope,
  };

  explicit ASTDumper(llvm::raw_ostream &os, Style style = Style::Toy)
      : os(os), style(style) {}

  /// Entry point: dumps every record in the module.
  void dump(ModuleAST &module);

private:
  void dump(RecordAST &record);
  void dump(FunctionAST &func);
  void dump(StructAST &str);
  void dump(PrototypeAST &proto);
  void dump(const VarType &type);
  void dump(ExprASTList &block);
  void dumpExpr(ExprAST &expr) { visit(expr); }

  /// Per-node hooks, reached through the inherited visit(ExprAST &).
  void visitVarDecl(VarDeclExprAST &expr);
  void visitReturn(ReturnExprAST &expr);
  void visitNumber(NumberExprAST &expr);
  void visitLiteral(LiteralExprAST &expr);
  void visitStructLiteral(StructLiteralExprAST &expr);
  void visitVariable(VariableExprAST &expr);
  void visitBinary(BinaryExprAST &expr);
  void visitCall(CallExprAST &expr);
  void visitPrint(PrintExprAST &expr);

  /// Writes the current indentation.
  void indent();

  /// Formats a location as the active style spells it.
  std::string loc(const Location &location);

  /// Prints a nested literal's dimensions and values (Toy style).
  void printLiteralHelper(ExprAST &litOrNum);

  llvm::raw_ostream &os;
  Style style;
  int curIndent = 0;

  /// RAII indentation, so an early return cannot leave the level wrong.
  struct Indent {
    explicit Indent(int &level) : level(++level) {}
    ~Indent() { --level; }
    int &level;
  };
};

} // namespace toy

#endif // TOY_ASTDUMPER_H
