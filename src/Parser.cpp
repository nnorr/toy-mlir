//===- Parser.cpp - Toy language parser -----------------------------------===//
//
// Adapted from mlir/examples/toy/Ch7/include/toy/Parser.h in the LLVM Project,
// under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// One function per production, in the order include/toy/Parser.h lists them.
// No semantic checking happens here: an undeclared variable, a call to a
// function that does not exist, or a reshape that changes the element count all
// parse cleanly and are caught later by MLIRGen or by verification.
//
//===----------------------------------------------------------------------===//

#include "toy/Parser.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/raw_ostream.h"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace toy {

//===----------------------------------------------------------------------===//
// Diagnostics
//===----------------------------------------------------------------------===//

/// The one diagnostic format, shared by Parser::parseError and the file-local
/// helpers below so the wording cannot drift between them.
static void emitParseError(const Lexer &lexer, const llvm::Twine &expected,
                           const llvm::Twine &context) {
  int curToken = lexer.getCurToken();
  llvm::errs() << "Parse error (" << lexer.getLastLocation().line << ", "
               << lexer.getLastLocation().col << "): expected '" << expected
               << "' " << context << " but has Token " << curToken;
  if (curToken >= 0 && isprint(curToken))
    llvm::errs() << " '" << static_cast<char>(curToken) << "'";
  llvm::errs() << "\n";
}

template <typename R, typename T, typename U>
std::unique_ptr<R> Parser::parseError(T &&expected, U &&context) {
  emitParseError(lexer, expected, context);
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Productions shared by two callers
//===----------------------------------------------------------------------===//

/// Parses `'(' (expression (',' expression)*)? ')'` once the callee name has
/// been consumed.
///
/// A file-local helper rather than a method: a call appears both inside an
/// expression (parseIdentifierExpr) and as a whole statement (parseBlock), and
/// Parser's interface lists only the grammar's own productions. The recursion
/// back into expressions is passed in for the same reason.
static std::unique_ptr<ExprAST>
parseCallTail(Lexer &lexer, llvm::StringRef name, const Location &loc,
              llvm::function_ref<std::unique_ptr<ExprAST>()> parseExpression) {
  lexer.consume(Token('('));

  std::vector<std::unique_ptr<ExprAST>> args;
  if (lexer.getCurToken() != ')') {
    while (true) {
      auto arg = parseExpression();
      if (!arg)
        return nullptr;
      args.push_back(std::move(arg));

      if (lexer.getCurToken() == ')')
        break;

      if (lexer.getCurToken() != ',') {
        emitParseError(lexer, ", or )", "in argument list");
        return nullptr;
      }
      lexer.getNextToken(); // eat ,
    }
  }
  lexer.consume(Token(')'));

  // `print` is a builtin with its own node: it becomes toy.print, not a call.
  if (name == "print") {
    if (args.size() != 1) {
      emitParseError(lexer, "<single arg>", "as argument to print()");
      return nullptr;
    }
    return std::make_unique<PrintExprAST>(loc, std::move(args[0]));
  }

  return std::make_unique<CallExprAST>(loc, std::string(name), std::move(args));
}

/// Parses `identifier ('=' expression)?` once a type name has been consumed,
/// e.g. the `value = {...}` of `Struct value = {...}`.
///
/// Shared by parseDeclaration and parseBlock for the same reason as
/// parseCallTail.
static std::unique_ptr<VarDeclExprAST> parseTypedDeclarationTail(
    Lexer &lexer, llvm::StringRef typeName, bool requiresInitializer,
    const Location &loc,
    llvm::function_ref<std::unique_ptr<ExprAST>()> parseExpression) {
  if (lexer.getCurToken() != tok_identifier) {
    emitParseError(lexer, "name", "in variable declaration");
    return nullptr;
  }
  std::string id(lexer.getId());
  lexer.getNextToken(); // eat id

  std::unique_ptr<ExprAST> expr;
  if (requiresInitializer) {
    if (lexer.getCurToken() != '=') {
      emitParseError(lexer, "initializer", "in variable declaration");
      return nullptr;
    }
    lexer.consume(Token('='));

    expr = parseExpression();
    // Propagate the failure instead of building a declaration with a null
    // initializer, which every later consumer would have to defend against --
    // and name the initializer here, where the context is known. Left to the
    // caller, the same failure arrives as a complaint about the end of the
    // module, nowhere near the declaration that is actually wrong.
    if (!expr) {
      emitParseError(lexer, "expression",
                     "as the initializer of a variable declaration");
      return nullptr;
    }
  }

  VarType type;
  type.name = std::string(typeName);
  return std::make_unique<VarDeclExprAST>(loc, std::move(id), std::move(type),
                                          std::move(expr));
}

//===----------------------------------------------------------------------===//
// Expressions
//===----------------------------------------------------------------------===//

std::unique_ptr<ExprAST> Parser::parseNumberExpr() {
  auto loc = lexer.getLastLocation();
  auto result =
      std::make_unique<NumberExprAST>(std::move(loc), lexer.getValue());
  lexer.consume(tok_number);
  return result;
}

std::unique_ptr<ExprAST> Parser::parseParenExpr() {
  lexer.getNextToken(); // eat (
  auto v = parseExpression();
  if (!v)
    return nullptr;

  if (lexer.getCurToken() != ')')
    return parseError<ExprAST>(")", "to close expression with parentheses");
  lexer.consume(Token(')'));
  return v;
}

std::unique_ptr<ExprAST> Parser::parseIdentifierExpr() {
  std::string name(lexer.getId());

  auto loc = lexer.getLastLocation();
  lexer.getNextToken(); // eat identifier

  if (lexer.getCurToken() != '(') // a plain variable reference
    return std::make_unique<VariableExprAST>(std::move(loc), name);

  return parseCallTail(lexer, name, loc, [this] { return parseExpression(); });
}

std::unique_ptr<ExprAST> Parser::parseTensorLiteralExpr() {
  auto loc = lexer.getLastLocation();
  lexer.consume(Token('['));

  // The values at this nesting level, and the shape of everything below it.
  std::vector<std::unique_ptr<ExprAST>> values;
  std::vector<int64_t> dims;
  do {
    // Either a nested literal or a number.
    if (lexer.getCurToken() == '[') {
      values.push_back(parseTensorLiteralExpr());
      if (!values.back())
        return nullptr; // parse error in the nested literal
    } else {
      if (lexer.getCurToken() != tok_number)
        return parseError<ExprAST>("<num> or [", "in literal expression");
      values.push_back(parseNumberExpr());
    }

    if (lexer.getCurToken() == ']')
      break;

    if (lexer.getCurToken() != ',')
      return parseError<ExprAST>("] or ,", "in literal expression");

    lexer.getNextToken(); // eat ,
  } while (true);
  if (values.empty())
    return parseError<ExprAST>("<something>", "to fill literal expression");
  lexer.getNextToken(); // eat ]

  // The shape is recovered from the nesting rather than declared: this level
  // contributes its own length first.
  dims.push_back(values.size());

  // If anything here is itself a literal, every element must be one and they
  // must agree, since that uniformity is what makes the shape well defined.
  if (llvm::any_of(values, [](std::unique_ptr<ExprAST> &expr) {
        return llvm::isa<LiteralExprAST>(expr.get());
      })) {
    auto *firstLiteral = llvm::dyn_cast<LiteralExprAST>(values.front().get());
    if (!firstLiteral)
      return parseError<ExprAST>("uniform well-nested dimensions",
                                 "inside literal expression");

    auto firstDims = firstLiteral->getDims();
    dims.insert(dims.end(), firstDims.begin(), firstDims.end());

    for (auto &expr : values) {
      auto *exprLiteral = llvm::dyn_cast<LiteralExprAST>(expr.get());
      if (!exprLiteral)
        return parseError<ExprAST>("uniform well-nested dimensions",
                                   "inside literal expression");
      if (exprLiteral->getDims() != firstDims)
        return parseError<ExprAST>("uniform well-nested dimensions",
                                   "inside literal expression");
    }
  }
  return std::make_unique<LiteralExprAST>(std::move(loc), std::move(values),
                                          std::move(dims));
}

std::unique_ptr<ExprAST> Parser::parseStructLiteralExpr() {
  auto loc = lexer.getLastLocation();
  lexer.consume(Token('{'));

  std::vector<std::unique_ptr<ExprAST>> values;
  do {
    // A struct literal's elements are tensors, numbers or nested structs.
    if (lexer.getCurToken() == '[') {
      values.push_back(parseTensorLiteralExpr());
      if (!values.back())
        return nullptr;
    } else if (lexer.getCurToken() == tok_number) {
      values.push_back(parseNumberExpr());
      if (!values.back())
        return nullptr;
    } else {
      if (lexer.getCurToken() != '{')
        return parseError<ExprAST>("{, [, or number",
                                   "in struct literal expression");
      values.push_back(parseStructLiteralExpr());
      if (!values.back())
        return nullptr;
    }

    if (lexer.getCurToken() == '}')
      break;

    if (lexer.getCurToken() != ',')
      return parseError<ExprAST>("} or ,", "in struct literal expression");

    lexer.getNextToken(); // eat ,
  } while (true);
  if (values.empty())
    return parseError<ExprAST>("<something>",
                               "to fill struct literal expression");
  lexer.getNextToken(); // eat }

  return std::make_unique<StructLiteralExprAST>(std::move(loc),
                                                std::move(values));
}

std::unique_ptr<ExprAST> Parser::parsePrimary() {
  switch (lexer.getCurToken()) {
  default:
    llvm::errs() << "unknown token '" << lexer.getCurToken()
                 << "' when expecting an expression\n";
    return nullptr;
  case tok_identifier:
    return parseIdentifierExpr();
  case tok_number:
    return parseNumberExpr();
  case '(':
    return parseParenExpr();
  case '[':
    return parseTensorLiteralExpr();
  case '{':
    return parseStructLiteralExpr();
  case ';':
    return nullptr;
  case '}':
    return nullptr;
  }
}

std::unique_ptr<ExprAST> Parser::parseBinOpRHS(int exprPrec,
                                               std::unique_ptr<ExprAST> lhs) {
  while (true) {
    int tokPrec = getTokPrecedence();

    // Anything binding less tightly than the caller's operator belongs to the
    // caller, not to us.
    if (tokPrec < exprPrec)
      return lhs;

    int binOp = lexer.getCurToken();
    // The operator's own location, taken before it is consumed. Reading it
    // afterwards, as upstream does, records the column of the right-hand
    // operand instead, and that skew survives into MLIR locations and DWARF.
    auto loc = lexer.getLastLocation();
    lexer.consume(Token(binOp));

    auto rhs = parsePrimary();
    if (!rhs)
      return parseError<ExprAST>("expression", "to complete binary operator");

    // If the operator after rhs binds more tightly, rhs belongs to it.
    int nextPrec = getTokPrecedence();
    if (tokPrec < nextPrec) {
      rhs = parseBinOpRHS(tokPrec + 1, std::move(rhs));
      if (!rhs)
        return nullptr;
    }

    lhs = std::make_unique<BinaryExprAST>(std::move(loc), binOp, std::move(lhs),
                                          std::move(rhs));
  }
}

std::unique_ptr<ExprAST> Parser::parseExpression() {
  auto lhs = parsePrimary();
  if (!lhs)
    return nullptr;

  return parseBinOpRHS(0, std::move(lhs));
}

std::unique_ptr<ReturnExprAST> Parser::parseReturn() {
  auto loc = lexer.getLastLocation();
  lexer.consume(tok_return);

  // The operand is optional: `return;` ends a function returning nothing.
  std::optional<std::unique_ptr<ExprAST>> expr;
  if (lexer.getCurToken() != ';') {
    expr = parseExpression();
    if (!expr)
      return nullptr;
  }
  return std::make_unique<ReturnExprAST>(std::move(loc), std::move(expr));
}

//===----------------------------------------------------------------------===//
// Declarations
//===----------------------------------------------------------------------===//

std::unique_ptr<VarType> Parser::parseType() {
  if (lexer.getCurToken() != '<')
    return parseError<VarType>("<", "to begin type");
  lexer.getNextToken(); // eat <

  auto type = std::make_unique<VarType>();

  while (lexer.getCurToken() == tok_number) {
    type->shape.push_back(lexer.getValue());
    lexer.getNextToken();
    if (lexer.getCurToken() == ',')
      lexer.getNextToken();
  }

  if (lexer.getCurToken() != '>')
    return parseError<VarType>(">", "to end type");
  lexer.getNextToken(); // eat >
  return type;
}

std::unique_ptr<VarDeclExprAST>
Parser::parseDeclaration(bool requiresInitializer) {
  // `var a<2, 3> = ...`: the shape is optional, and absent means "infer it".
  if (lexer.getCurToken() == tok_var) {
    auto loc = lexer.getLastLocation();
    lexer.getNextToken(); // eat var

    if (lexer.getCurToken() != tok_identifier)
      return parseError<VarDeclExprAST>("identified",
                                        "after 'var' declaration");
    std::string id(lexer.getId());
    lexer.getNextToken(); // eat id

    VarType type;
    if (lexer.getCurToken() == '<') {
      auto parsedType = parseType();
      if (!parsedType)
        return nullptr;
      type = std::move(*parsedType);
    }

    std::unique_ptr<ExprAST> expr;
    if (requiresInitializer) {
      // Test for the '=' rather than handing it straight to consume(), whose
      // assertion aborts the process. Upstream does the latter, so `var a;`
      // takes down the compiler over a one-token typo. The wording matches the
      // typed form above, since both mean "this declaration needs a value".
      if (lexer.getCurToken() != '=')
        return parseError<VarDeclExprAST>("initializer",
                                          "in variable declaration");
      lexer.consume(Token('='));

      expr = parseExpression();
      // As in the typed form: say what was expected and where.
      if (!expr)
        return parseError<VarDeclExprAST>(
            "expression", "as the initializer of a variable declaration");
    }
    return std::make_unique<VarDeclExprAST>(std::move(loc), std::move(id),
                                            std::move(type), std::move(expr));
  }

  // `Struct value = ...`: the leading identifier is the type name.
  if (lexer.getCurToken() != tok_identifier)
    return parseError<VarDeclExprAST>("type name", "in variable declaration");
  auto loc = lexer.getLastLocation();
  std::string typeName(lexer.getId());
  lexer.getNextToken(); // eat id

  return parseTypedDeclarationTail(lexer, typeName, requiresInitializer, loc,
                                   [this] { return parseExpression(); });
}

std::unique_ptr<ExprASTList> Parser::parseBlock() {
  if (lexer.getCurToken() != '{')
    return parseError<ExprASTList>("{", "to begin block");
  lexer.consume(Token('{'));

  auto exprList = std::make_unique<ExprASTList>();

  // An empty statement is legal and means nothing, so runs of ';' collapse.
  while (lexer.getCurToken() == ';')
    lexer.consume(Token(';'));

  while (lexer.getCurToken() != '}' && lexer.getCurToken() != tok_eof) {
    if (lexer.getCurToken() == tok_identifier) {
      // An identifier here is either a call or a typed declaration, and one
      // token of lookahead cannot tell them apart: `transpose(a)` is a call,
      // `Struct value` a declaration. Consume it and decide from what follows.
      auto loc = lexer.getLastLocation();
      std::string id(lexer.getId());
      lexer.consume(tok_identifier);

      std::unique_ptr<ExprAST> expr;
      if (lexer.getCurToken() == '(')
        expr = parseCallTail(lexer, id, loc,
                             [this] { return parseExpression(); });
      else
        expr = parseTypedDeclarationTail(lexer, id,
                                         /*requiresInitializer=*/true, loc,
                                         [this] { return parseExpression(); });
      if (!expr)
        return nullptr;
      exprList->push_back(std::move(expr));
    } else if (lexer.getCurToken() == tok_var) {
      auto varDecl = parseDeclaration(/*requiresInitializer=*/true);
      if (!varDecl)
        return nullptr;
      exprList->push_back(std::move(varDecl));
    } else if (lexer.getCurToken() == tok_return) {
      auto ret = parseReturn();
      if (!ret)
        return nullptr;
      exprList->push_back(std::move(ret));
    } else {
      auto expr = parseExpression();
      if (!expr)
        return nullptr;
      exprList->push_back(std::move(expr));
    }

    if (lexer.getCurToken() != ';')
      return parseError<ExprASTList>(";", "after expression");

    while (lexer.getCurToken() == ';')
      lexer.consume(Token(';'));
  }

  if (lexer.getCurToken() != '}')
    return parseError<ExprASTList>("}", "to close block");

  lexer.consume(Token('}'));
  return exprList;
}

std::unique_ptr<PrototypeAST> Parser::parsePrototype() {
  auto loc = lexer.getLastLocation();

  if (lexer.getCurToken() != tok_def)
    return parseError<PrototypeAST>("def", "in prototype");
  lexer.consume(tok_def);

  if (lexer.getCurToken() != tok_identifier)
    return parseError<PrototypeAST>("function name", "in prototype");

  std::string fnName(lexer.getId());
  lexer.consume(tok_identifier);

  if (lexer.getCurToken() != '(')
    return parseError<PrototypeAST>("(", "in prototype");
  lexer.consume(Token('('));

  std::vector<std::unique_ptr<VarDeclExprAST>> args;
  if (lexer.getCurToken() != ')') {
    do {
      VarType type;
      std::string name;

      // A parameter is `name` or `Type name`, so the first identifier is only
      // known to be a type once a second one follows it.
      std::string nameOrType(lexer.getId());
      auto argLoc = lexer.getLastLocation();
      lexer.consume(tok_identifier);

      if (lexer.getCurToken() == tok_identifier) {
        type.name = std::move(nameOrType);

        name = std::string(lexer.getId());
        lexer.consume(tok_identifier);
      } else {
        name = std::move(nameOrType);
      }

      args.push_back(
          std::make_unique<VarDeclExprAST>(std::move(argLoc), name, type));
      if (lexer.getCurToken() != ',')
        break;
      lexer.consume(Token(','));
      if (lexer.getCurToken() != tok_identifier)
        return parseError<PrototypeAST>(
            "identifier", "after ',' in function parameter list");
    } while (true);
  }
  if (lexer.getCurToken() != ')')
    return parseError<PrototypeAST>(")", "to end function prototype");

  lexer.consume(Token(')'));
  return std::make_unique<PrototypeAST>(std::move(loc), fnName,
                                        std::move(args));
}

std::unique_ptr<FunctionAST> Parser::parseDefinition() {
  auto proto = parsePrototype();
  if (!proto)
    return nullptr;

  if (auto block = parseBlock())
    return std::make_unique<FunctionAST>(std::move(proto), std::move(block));
  return nullptr;
}

std::unique_ptr<StructAST> Parser::parseStruct() {
  auto loc = lexer.getLastLocation();
  lexer.consume(tok_struct);
  if (lexer.getCurToken() != tok_identifier)
    return parseError<StructAST>("name", "in struct definition");
  std::string name(lexer.getId());
  lexer.consume(tok_identifier);

  if (lexer.getCurToken() != '{')
    return parseError<StructAST>("{", "in struct definition");
  lexer.consume(Token('{'));

  // Members are declarations without initializers: a struct definition says
  // what the fields are, a struct literal says what they hold.
  std::vector<std::unique_ptr<VarDeclExprAST>> decls;
  do {
    auto decl = parseDeclaration(/*requiresInitializer=*/false);
    if (!decl)
      return nullptr;
    decls.push_back(std::move(decl));

    if (lexer.getCurToken() != ';')
      return parseError<StructAST>(";", "after variable in struct definition");
    lexer.consume(Token(';'));
  } while (lexer.getCurToken() != '}');

  lexer.consume(Token('}'));
  return std::make_unique<StructAST>(loc, name, std::move(decls));
}

//===----------------------------------------------------------------------===//
// Module
//===----------------------------------------------------------------------===//

std::unique_ptr<ModuleAST> Parser::parseModule() {
  lexer.getNextToken(); // prime the lexer

  std::vector<std::unique_ptr<RecordAST>> records;
  while (true) {
    std::unique_ptr<RecordAST> record;
    switch (lexer.getCurToken()) {
    case tok_eof:
      break;
    case tok_def:
      record = parseDefinition();
      break;
    case tok_struct:
      record = parseStruct();
      break;
    default:
      return parseError<ModuleAST>("'def' or 'struct'",
                                   "when parsing top level module records");
    }
    if (!record)
      break;
    records.push_back(std::move(record));
  }

  // Stopping anywhere but the end of the file means a production above failed
  // and already reported why.
  //
  // A failed record that consumed the input up to EOF therefore yields a
  // *successful* empty module even though a diagnostic was printed. A function
  // whose closing brace is missing, for instance, reports "expected '}'" and
  // still dumps `Module:`. That is upstream Ch7's behavior, which the
  // equivalence sweep in tests/compat pins, so do not "fix" it here. The driver
  // decides what to do about a module that came with complaints.
  //
  // Upstream Ch1 additionally reports "expected 'def' in prototype" for a file
  // holding nothing but comments, because its parseModule loops on
  // parseDefinition instead of switching on the token. Ch7 accepts that file
  // silently, and its own lit test asserts CHECK-NOT: Parse error. This is the
  // Ch7 grammar, so silence is correct here.
  if (lexer.getCurToken() != tok_eof)
    return parseError<ModuleAST>("nothing", "at end of module");

  return std::make_unique<ModuleAST>(std::move(records));
}

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

int Parser::getTokPrecedence() {
  if (!isascii(lexer.getCurToken()))
    return -1;

  // 1 is the lowest precedence. '.' binds tightest so that `value.a * x`
  // groups as `(value.a) * x`.
  switch (static_cast<char>(lexer.getCurToken())) {
  case '-':
    return 20;
  case '+':
    return 20;
  case '*':
    return 40;
  case '.':
    return 60;
  default:
    return -1;
  }
}

} // namespace toy
