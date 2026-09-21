//===- ASTDumper.cpp - Print the Toy AST as a tree ------------------------===//
//
// The Style::Toy half is adapted from mlir/examples/toy/Ch7/parser/AST.cpp in
// the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Two formats over one traversal, so each hook shows both shapes side by side.
//
// Style::Toy reproduces `toyc-ch7 -emit=ast` byte for byte, and is a direct
// port of reference/Ch7/parser/AST.cpp. That constrains it more than taste
// would. Three oddities are deliberate and marked below: the trailing space in
// "Function ", the unlabelled number line, and the struct-literal line that
// runs into its first child. Changing any of them breaks tests/compat silently.
//
// Style::Kaleidoscope is an alternative rendering of the same Toy nodes, not a
// port of anything: a label, a value, `@line:col`, and captioned children one
// level deeper. It exists so a Toy tree can be read beside the tree printed by
// the Kaleidoscope compiler in 06_llvm_tutorial, whose output has that shape.
// Toy's node set is its own, so the labels and captions are chosen here:
//
//     node            printed as                     why this caption
//     ----            ---------                      ----------------
//     Number          Number 3 @l:c                  value is the whole node
//     Variable        Variable a @l:c                  "
//     Binary          Binary '*' + LHS:/RHS:         two named operands
//     Call            Call f + Arg: per argument      variadic operands
//     Print           Print + Operand:               exactly one operand
//     Return          Return + Value: or (void)       optional operand
//     VarDecl         VarDecl a<2, 3> + Init:        type inline, value nested
//     Literal         Literal <2, 3>[ ... ] @l:c     compact; nesting is data
//     StructLiteral   StructLiteral + Element:       one caption per field
//     Struct          Struct S + Members:            a record, not an expr
//
// Numbers are spelled differently per style on purpose. raw_ostream's default
// for a double is exponent form (`1.000000e+00`), which is what upstream Toy
// emits and what Style::Toy must therefore keep; this style prints %g (`1`)
// because a tree meant for reading is easier to scan without the exponents.
//
//===----------------------------------------------------------------------===//

#include "toy/ASTDumper.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Format.h"

#include <string>

namespace toy {

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

void ASTDumper::indent() {
  for (int i = 0; i < curIndent; i++)
    os << "  ";
}

std::string ASTDumper::loc(const Location &location) {
  // This style omits the filename: it is meant for reading one tree at a time,
  // where every line repeating the same path is noise.
  if (style == Style::Kaleidoscope)
    return (llvm::Twine("@") + llvm::Twine(location.line) + ":" +
            llvm::Twine(location.col))
        .str();

  return (llvm::Twine("@") + *location.file + ":" + llvm::Twine(location.line) +
          ":" + llvm::Twine(location.col))
      .str();
}

void ASTDumper::printLiteralHelper(ExprAST &litOrNum) {
  // A literal element is either a number or a nested literal, so the shape is
  // recovered by recursion rather than from the dims of the outermost node.
  if (auto *num = llvm::dyn_cast<NumberExprAST>(&litOrNum)) {
    if (style == Style::Kaleidoscope)
      os << llvm::format("%g", num->getValue());
    else
      os << num->getValue();
    return;
  }
  auto *literal = llvm::cast<LiteralExprAST>(&litOrNum);

  // Dimensions at every level, so `[[1, 2], [3, 4]]` reads as <2,2>[<2>[...]].
  os << "<";
  llvm::interleaveComma(literal->getDims(), os);
  os << ">";

  os << "[ ";
  llvm::interleaveComma(literal->getValues(), os,
                        [&](auto &elt) { printLiteralHelper(*elt); });
  os << "]";
}

//===----------------------------------------------------------------------===//
// Records
//===----------------------------------------------------------------------===//

void ASTDumper::dump(ModuleAST &module) {
  // No module header in this style: the records are the interesting part, and
  // starting them at column zero keeps one tree per top-level definition.
  if (style == Style::Kaleidoscope) {
    for (auto &record : module)
      dump(*record);
    return;
  }

  Indent level(curIndent);
  indent();
  os << "Module:\n";
  for (auto &record : module)
    dump(*record);
}

void ASTDumper::dump(RecordAST &record) {
  if (auto *function = llvm::dyn_cast<FunctionAST>(&record))
    return dump(*function);
  if (auto *str = llvm::dyn_cast<StructAST>(&record))
    return dump(*str);

  // Unreachable while RecordAST has exactly two kinds; kept because upstream
  // prints it, and an unlabelled crash would be worse than a stray line.
  os << "<unknown Record, kind " << record.getKind() << ">\n";
}

void ASTDumper::dump(FunctionAST &func) {
  if (style == Style::Kaleidoscope) {
    indent();
    os << "Function\n";
    Indent level(curIndent);
    dump(*func.getProto());
    indent();
    os << "Body:\n";
    Indent bodyLevel(curIndent);
    dump(*func.getBody());
    return;
  }

  Indent level(curIndent);
  indent();
  os << "Function \n"; // trailing space: upstream's, and tests compare bytes
  dump(*func.getProto());
  dump(*func.getBody());
}

void ASTDumper::dump(StructAST &str) {
  if (style == Style::Kaleidoscope) {
    indent();
    os << "Struct " << str.getName() << " @" << str.loc().line << "\n";
    Indent level(curIndent);
    indent();
    os << "Members:\n";
    Indent memberLevel(curIndent);
    for (auto &variable : str.getVariables())
      dumpExpr(*variable);
    return;
  }

  Indent level(curIndent);
  indent();
  os << "Struct: " << str.getName() << " " << loc(str.loc()) << "\n";
  {
    Indent varsLevel(curIndent);
    indent();
    os << "Variables: [\n";
    for (auto &variable : str.getVariables())
      dumpExpr(*variable);
    indent();
    os << "]\n";
  }
}

void ASTDumper::dump(PrototypeAST &proto) {
  if (style == Style::Kaleidoscope) {
    // Space-separated parameters and a line-only location: a signature has no
    // meaningful column, and the parameter list is short enough not to need
    // commas.
    indent();
    os << "Prototype " << proto.getName() << " (";
    llvm::interleave(
        proto.getArgs(), os, [&](auto &arg) { os << arg->getName(); }, " ");
    os << ") @" << proto.loc().line << "\n";
    return;
  }

  Indent level(curIndent);
  indent();
  os << "Proto '" << proto.getName() << "' " << loc(proto.loc()) << "\n";
  indent();
  os << "Params: [";
  llvm::interleaveComma(proto.getArgs(), os,
                        [&](auto &arg) { os << arg->getName(); });
  os << "]\n";
}

void ASTDumper::dump(const VarType &type) {
  // Either a struct name or a shape, never both: `<Struct>` or `<2, 3>`, and
  // `<>` for a type left to inference.
  os << "<";
  if (!type.name.empty())
    os << type.name;
  else
    llvm::interleaveComma(type.shape, os);
  os << ">";
}

void ASTDumper::dump(ExprASTList &block) {
  // No braces in this style: the indentation already shows where the block
  // ends, so the statements print at the caller's level.
  if (style == Style::Kaleidoscope) {
    for (auto &expr : block)
      dumpExpr(*expr);
    return;
  }

  Indent level(curIndent);
  indent();
  os << "Block {\n";
  for (auto &expr : block)
    dumpExpr(*expr);
  indent();
  os << "} // Block\n";
}

//===----------------------------------------------------------------------===//
// Expressions
//===----------------------------------------------------------------------===//

void ASTDumper::visitVarDecl(VarDeclExprAST &expr) {
  // getType() hands back a const reference but dump(VarType &) does not take
  // one, so the type is copied here. See the note in the report: widening the
  // parameter to `const VarType &` would remove this copy.
  VarType type = expr.getType();

  if (style == Style::Kaleidoscope) {
    indent();
    os << "VarDecl " << expr.getName();
    dump(type);
    os << " " << loc(expr.loc()) << "\n";
    if (auto *initVal = expr.getInitVal()) {
      Indent captionLevel(curIndent);
      indent();
      os << "Init:\n";
      Indent childLevel(curIndent);
      dumpExpr(*initVal);
    }
    return;
  }

  Indent level(curIndent);
  indent();
  os << "VarDecl " << expr.getName();
  dump(type);
  os << " " << loc(expr.loc()) << "\n";
  if (auto *initVal = expr.getInitVal())
    dumpExpr(*initVal);
}

void ASTDumper::visitReturn(ReturnExprAST &expr) {
  if (style == Style::Kaleidoscope) {
    indent();
    os << "Return " << loc(expr.loc()) << "\n";
    Indent level(curIndent);
    if (expr.getExpr().has_value()) {
      indent();
      os << "Value:\n";
      Indent childLevel(curIndent);
      dumpExpr(**expr.getExpr());
      return;
    }
    indent();
    os << "(void)\n";
    return;
  }

  Indent level(curIndent);
  indent();
  os << "Return\n";
  if (expr.getExpr().has_value())
    return dumpExpr(**expr.getExpr());
  {
    Indent voidLevel(curIndent);
    indent();
    os << "(void)\n";
  }
}

void ASTDumper::visitNumber(NumberExprAST &expr) {
  if (style == Style::Kaleidoscope) {
    indent();
    os << "Number " << llvm::format("%g", expr.getValue()) << " "
       << loc(expr.loc()) << "\n";
    return;
  }

  Indent level(curIndent);
  indent();
  // No label upstream: a bare number and its location.
  os << expr.getValue() << " " << loc(expr.loc()) << "\n";
}

void ASTDumper::visitLiteral(LiteralExprAST &expr) {
  if (style == Style::Kaleidoscope) {
    indent();
    os << "Literal ";
    printLiteralHelper(expr);
    os << " " << loc(expr.loc()) << "\n";
    return;
  }

  Indent level(curIndent);
  indent();
  os << "Literal: ";
  printLiteralHelper(expr);
  os << " " << loc(expr.loc()) << "\n";
}

void ASTDumper::visitStructLiteral(StructLiteralExprAST &expr) {
  if (style == Style::Kaleidoscope) {
    indent();
    os << "StructLiteral " << loc(expr.loc()) << "\n";
    for (auto &value : expr.getValues()) {
      Indent captionLevel(curIndent);
      indent();
      os << "Element:\n";
      Indent childLevel(curIndent);
      dumpExpr(*value);
    }
    return;
  }

  Indent level(curIndent);
  indent();
  // Upstream emits no newline here, so the first element's indentation lands on
  // this line and the location ends up on a line of its own below. Reproduced
  // for byte-compatibility, not because it reads well.
  os << "Struct Literal: ";
  for (auto &value : expr.getValues())
    dumpExpr(*value);
  indent();
  os << " " << loc(expr.loc()) << "\n";
}

void ASTDumper::visitVariable(VariableExprAST &expr) {
  if (style == Style::Kaleidoscope) {
    indent();
    os << "Variable " << expr.getName() << " " << loc(expr.loc()) << "\n";
    return;
  }

  Indent level(curIndent);
  indent();
  os << "var: " << expr.getName() << " " << loc(expr.loc()) << "\n";
}

void ASTDumper::visitBinary(BinaryExprAST &expr) {
  if (style == Style::Kaleidoscope) {
    indent();
    os << "Binary '" << expr.getOp() << "' " << loc(expr.loc()) << "\n";
    for (auto [caption, child] :
         {std::pair<const char *, ExprAST *>{"LHS:", expr.getLHS()},
          std::pair<const char *, ExprAST *>{"RHS:", expr.getRHS()}}) {
      Indent captionLevel(curIndent);
      indent();
      os << caption << "\n";
      Indent childLevel(curIndent);
      dumpExpr(*child);
    }
    return;
  }

  Indent level(curIndent);
  indent();
  os << "BinOp: " << expr.getOp() << " " << loc(expr.loc()) << "\n";
  dumpExpr(*expr.getLHS());
  dumpExpr(*expr.getRHS());
}

void ASTDumper::visitCall(CallExprAST &expr) {
  if (style == Style::Kaleidoscope) {
    indent();
    os << "Call " << expr.getCallee() << " " << loc(expr.loc()) << "\n";
    for (auto &arg : expr.getArgs()) {
      Indent captionLevel(curIndent);
      indent();
      os << "Arg:\n";
      Indent childLevel(curIndent);
      dumpExpr(*arg);
    }
    return;
  }

  Indent level(curIndent);
  indent();
  os << "Call '" << expr.getCallee() << "' [ " << loc(expr.loc()) << "\n";
  for (auto &arg : expr.getArgs())
    dumpExpr(*arg);
  indent();
  os << "]\n";
}

void ASTDumper::visitPrint(PrintExprAST &expr) {
  if (style == Style::Kaleidoscope) {
    indent();
    os << "Print " << loc(expr.loc()) << "\n";
    Indent captionLevel(curIndent);
    indent();
    os << "Operand:\n";
    Indent childLevel(curIndent);
    dumpExpr(*expr.getArg());
    return;
  }

  Indent level(curIndent);
  indent();
  os << "Print [ " << loc(expr.loc()) << "\n";
  dumpExpr(*expr.getArg());
  indent();
  os << "]\n";
}

} // namespace toy
