//===- Parser.h - Toy language parser -------------------------------------===//
//
// Adapted from mlir/examples/toy/Ch7 in the LLVM Project, under the Apache
// License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Recursive descent over the token stream, with precedence climbing for binary
// operators. The only component that builds AST nodes.
//
// Deviations from upstream, documented in ARCHITECTURE.md:
//
//  1. Declaration and implementation are split (upstream puts all 680 lines in
//     the header), so the grammar reads as a list of productions here.
//  2. A binary expression's location is the operator's own location. Upstream
//     reads the location *after* consuming the operator, so `a * b` reports
//     the column of `b`; that skew reaches the MLIR loc() and the DWARF line
//     table.
//  3. A failed initializer in a `var` declaration is propagated, with a
//     diagnostic naming the initializer. Upstream builds a VarDeclExprAST with
//     a null initializer and exits 0, so `var a = ;` compiles "successfully"
//     into an AST with a hole in it.
//  4. `var a;`, a declaration with no initializer at all, is reported as a
//     parse error. Upstream calls consume(Token('=')) without testing for it
//     first, so the assertion inside consume() aborts the compiler (exit 134)
//     on a one-token typo.
//
//===----------------------------------------------------------------------===//

#ifndef TOY_PARSER_H
#define TOY_PARSER_H

#include "toy/AST.h"
#include "toy/Lexer.h"

#include <memory>
#include <optional>

namespace toy {

/// Parses a Toy module.
///
/// Every production returns nullptr on error after reporting it, and callers
/// propagate that up: there is no error recovery, because a Toy source file is
/// small enough that the first error is the useful one.
class Parser {
public:
  explicit Parser(Lexer &lexer) : lexer(lexer) {}

  /// module ::= (definition | struct-definition)*
  /// Returns nullptr if anything failed to parse.
  std::unique_ptr<ModuleAST> parseModule();

private:
  //===--------------------------------------------------------------------===//
  // Expressions
  //===--------------------------------------------------------------------===//

  /// expression ::= primary binoprhs
  std::unique_ptr<ExprAST> parseExpression();

  /// primary ::= identifierexpr | numberexpr | parenexpr | tensorliteral
  std::unique_ptr<ExprAST> parsePrimary();

  /// binoprhs ::= (binop primary)*
  std::unique_ptr<ExprAST> parseBinOpRHS(int exprPrec,
                                         std::unique_ptr<ExprAST> lhs);

  /// numberexpr ::= number
  std::unique_ptr<ExprAST> parseNumberExpr();

  /// parenexpr ::= '(' expression ')'
  std::unique_ptr<ExprAST> parseParenExpr();

  /// identifierexpr ::= identifier | identifier '(' expression* ')'
  std::unique_ptr<ExprAST> parseIdentifierExpr();

  /// tensorliteral ::= '[' literalList ']' | number
  /// literalList   ::= tensorliteral | tensorliteral ',' literalList
  std::unique_ptr<ExprAST> parseTensorLiteralExpr();

  /// structliteral ::= '{' (structliteral | tensorliteral)* '}'
  std::unique_ptr<ExprAST> parseStructLiteralExpr();

  /// return ::= 'return' ';' | 'return' expression ';'
  std::unique_ptr<ReturnExprAST> parseReturn();

  //===--------------------------------------------------------------------===//
  // Declarations
  //===--------------------------------------------------------------------===//

  /// decl ::= var identifier [ type ] (= expr)?
  /// decl ::= identifier identifier (= expr)?
  std::unique_ptr<VarDeclExprAST> parseDeclaration(bool requiresInitializer);

  /// type ::= '<' shape_list '>'
  /// shape_list ::= num | num ',' shape_list
  std::unique_ptr<VarType> parseType();

  /// block ::= '{' expression_list '}'
  std::unique_ptr<ExprASTList> parseBlock();

  /// prototype ::= 'def' id '(' decl_list ')'
  std::unique_ptr<PrototypeAST> parsePrototype();

  /// definition ::= prototype block
  std::unique_ptr<FunctionAST> parseDefinition();

  /// struct-definition ::= 'struct' identifier '{' decl+ '}'
  std::unique_ptr<StructAST> parseStruct();

  //===--------------------------------------------------------------------===//
  // Helpers
  //===--------------------------------------------------------------------===//

  /// Precedence of the current token as a binary operator, -1 if it is not one.
  int getTokPrecedence();

  /// Reports "expected <expected> <context> but has <token>" at the current
  /// location and returns nullptr, for use as `return parseError<T>(...)`.
  template <typename R, typename T, typename U = const char *>
  std::unique_ptr<R> parseError(T &&expected, U &&context = "");

  Lexer &lexer;
};

} // namespace toy

#endif // TOY_PARSER_H
