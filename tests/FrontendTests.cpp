//===- FrontendTests.cpp - Unit tests for the Toy front end ---------------===//
//
// Part of toy-mlir.
//
//===----------------------------------------------------------------------===//
//
// Tests the lexer and parser on their own, with no MLIR anywhere.
//
// That is the point of the test as much as the assertions are: this executable
// links ToyFrontend and LLVMSupport and nothing else, which is what keeps
// AST.h honest about not depending on the IR. `ldd` on the result shows no
// MLIR library.
//
// No gtest, for the same reason 06_llvm_tutorial has none: a hand-rolled
// harness is a dozen lines, needs no dependency, and prints the one thing that
// matters when a check fails -- what was expected and what was produced.
//
//===----------------------------------------------------------------------===//

#include "toy/AST.h"
#include "toy/Lexer.h"
#include "toy/Parser.h"

#include "llvm/Support/Casting.h"

#include <cstdio>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace toy;

//===----------------------------------------------------------------------===//
// Harness
//===----------------------------------------------------------------------===//

namespace {

unsigned numChecks = 0;
unsigned numFailures = 0;
const char *currentTest = "<none>";

void fail(const std::string &what) {
  ++numFailures;
  std::cerr << "FAIL [" << currentTest << "] " << what << "\n";
}

/// Asserts a condition, naming it in the failure message.
void check(bool cond, const std::string &what) {
  ++numChecks;
  if (!cond)
    fail(what);
}

/// Asserts equality and shows both sides when they differ, which is the only
/// thing that makes a failing test cheap to diagnose.
template <typename A, typename B>
void checkEq(const A &actual, const B &expected, const std::string &what) {
  ++numChecks;
  if (!(actual == expected)) {
    std::ostringstream os;
    os << what << ": expected '" << expected << "', got '" << actual << "'";
    fail(os.str());
  }
}

/// Runs `body` with fd 2 redirected into a temporary file and returns what was
/// written. The parser reports through llvm::errs(), which writes to fd 2
/// unbuffered, so redirecting the descriptor is enough to capture diagnostics
/// without giving the parser a stream parameter it does not have.
std::string captureStderr(const std::function<void()> &body) {
  char path[] = "/tmp/toy-frontend-test-XXXXXX";
  int fd = mkstemp(path);
  if (fd < 0) {
    fail("mkstemp failed");
    body();
    return {};
  }

  fflush(stderr);
  int saved = dup(2);
  dup2(fd, 2);

  body();

  fflush(stderr);
  dup2(saved, 2);
  close(saved);

  lseek(fd, 0, SEEK_SET);
  std::string captured;
  char buf[4096];
  ssize_t n;
  while ((n = read(fd, buf, sizeof(buf))) > 0)
    captured.append(buf, static_cast<size_t>(n));
  close(fd);
  unlink(path);
  return captured;
}

/// Lexes `source` completely and returns the token kinds in order, so a test
/// can state the expected token stream as a list.
std::vector<int> lexAll(const std::string &source) {
  Lexer lexer(source, "test.toy");
  std::vector<int> tokens;
  while (lexer.getNextToken() != tok_eof)
    tokens.push_back(lexer.getCurToken());
  return tokens;
}

/// Parses `source`, returning null on failure. Diagnostics go to stderr.
std::unique_ptr<ModuleAST> parse(const std::string &source) {
  Lexer lexer(source, "test.toy");
  Parser parser(lexer);
  return parser.parseModule();
}

/// The first function in a parsed module, or null if there is none.
FunctionAST *firstFunction(ModuleAST &module) {
  for (auto &record : module)
    if (auto *func = llvm::dyn_cast<FunctionAST>(record.get()))
      return func;
  return nullptr;
}

void test(const char *name, const std::function<void()> &body) {
  currentTest = name;
  body();
  currentTest = "<none>";
}

} // namespace

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

static void lexerTests() {
  test("lexer/tokens", [] {
    std::vector<int> expected = {tok_def,          tok_identifier,
                                '(',              ')',
                                '{',              '}'};
    checkEq(lexAll("def main() {}").size(), expected.size(), "token count");
    checkEq(lexAll("def main() {}") == expected, true, "token kinds");
  });

  test("lexer/keywords", [] {
    std::vector<int> expected = {tok_return, tok_var, tok_def, tok_struct};
    checkEq(lexAll("return var def struct") == expected, true, "keywords");
  });

  test("lexer/identifier-with-underscore", [] {
    // Toy allows '_' after the first character. The Kaleidoscope lexer in
    // 06_llvm_tutorial does not, and splits this into three tokens -- one of
    // the concrete differences between the two front ends.
    std::string source = "multiply_transpose";
    Lexer lexer(source, "test.toy");
    checkEq(lexer.getNextToken(), tok_identifier, "single identifier token");
    checkEq(lexer.getId().str(), std::string("multiply_transpose"), "spelling");
    checkEq(lexer.getNextToken(), tok_eof, "nothing after it");
  });

  test("lexer/comments", [] {
    std::string source = "# a comment\ndef main() {}\n";
    Lexer lexer(source, "test.toy");
    checkEq(lexer.getNextToken(), tok_def, "comment skipped");
    checkEq(lexer.getLastLocation().line, 2, "def is on line 2");
  });

  test("lexer/numbers", [] {
    std::string source = "1.5 42";
    Lexer lexer(source, "test.toy");
    checkEq(lexer.getNextToken(), tok_number, "first is a number");
    checkEq(lexer.getValue(), 1.5, "fractional value");
    checkEq(lexer.getNextToken(), tok_number, "second is a number");
    checkEq(lexer.getValue(), 42.0, "integer value");
    check(!lexer.hadError(), "well-formed numbers set no error");
  });

  test("lexer/locations", [] {
    // Columns are 1-based and point at the first character of the token.
    std::string source = "def f(a) {\n  return a;\n}\n";
    Lexer lexer(source, "test.toy");
    checkEq(lexer.getNextToken(), tok_def, "def");
    checkEq(lexer.getLastLocation().line, 1, "def line");
    checkEq(lexer.getLastLocation().col, 1, "def col");

    checkEq(lexer.getNextToken(), tok_identifier, "f");
    checkEq(lexer.getLastLocation().col, 5, "f col");

    while (lexer.getCurToken() != tok_return && lexer.getCurToken() != tok_eof)
      lexer.getNextToken();
    checkEq(lexer.getCurToken(), tok_return, "found return");
    checkEq(lexer.getLastLocation().line, 2, "return line");
    checkEq(lexer.getLastLocation().col, 3, "return col");

    checkEq(*lexer.getLastLocation().file, std::string("test.toy"), "filename");
  });

  test("lexer/malformed-number-D1", [] {
    // Deviation D1: upstream lexes `1.23.45` as one token, hands it to strtod
    // and silently keeps 1.23. Here it is reported.
    std::string source = "1.23.45";
    std::string diagnostic = captureStderr([&] {
      Lexer lexer(source, "test.toy");
      checkEq(lexer.getNextToken(), tok_number, "still yields a number token");
      check(lexer.hadError(), "hadError() is set");
    });
    check(diagnostic.find("malformed number") != std::string::npos,
          "diagnostic mentions a malformed number, got: " + diagnostic);
    check(diagnostic.find("test.toy") != std::string::npos,
          "diagnostic names the file, got: " + diagnostic);
  });
}

//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//

static void parserTests() {
  test("parser/declared-shape", [] {
    auto module = parse("def main() { var a<2, 3> = [1, 2, 3, 4, 5, 6]; }");
    check(module != nullptr, "parses");
    if (!module)
      return;
    auto *func = firstFunction(*module);
    check(func != nullptr, "has a function");
    if (!func)
      return;
    auto *decl = llvm::dyn_cast<VarDeclExprAST>(func->getBody()->front().get());
    check(decl != nullptr, "first statement is a var decl");
    if (!decl)
      return;
    checkEq(decl->getName().str(), std::string("a"), "name");
    checkEq(decl->getType().shape.size(), size_t(2), "shape rank");
    if (decl->getType().shape.size() == 2) {
      checkEq(decl->getType().shape[0], int64_t(2), "shape[0]");
      checkEq(decl->getType().shape[1], int64_t(3), "shape[1]");
    }
  });

  test("parser/literal-dims", [] {
    auto module = parse("def main() { var a = [[1, 2, 3], [4, 5, 6]]; }");
    check(module != nullptr, "parses");
    if (!module)
      return;
    auto *func = firstFunction(*module);
    if (!func)
      return;
    auto *decl = llvm::dyn_cast<VarDeclExprAST>(func->getBody()->front().get());
    if (!decl)
      return;
    auto *literal = llvm::dyn_cast_if_present<LiteralExprAST>(decl->getInitVal());
    check(literal != nullptr, "initializer is a literal");
    if (!literal)
      return;
    // The shape is recovered from the nesting, not declared.
    checkEq(literal->getDims().size(), size_t(2), "dims rank");
    if (literal->getDims().size() == 2) {
      checkEq(literal->getDims()[0], int64_t(2), "dims[0]");
      checkEq(literal->getDims()[1], int64_t(3), "dims[1]");
    }
    checkEq(literal->getValues().size(), size_t(2), "two nested rows");
  });

  test("parser/struct-definition", [] {
    auto module = parse("struct S {\n  var a;\n  var b;\n}\n"
                        "def main() { print(1); }");
    check(module != nullptr, "parses");
    if (!module)
      return;
    StructAST *str = nullptr;
    for (auto &record : *module)
      if (auto *s = llvm::dyn_cast<StructAST>(record.get()))
        str = s;
    check(str != nullptr, "module holds a struct record");
    if (!str)
      return;
    checkEq(str->getName().str(), std::string("S"), "struct name");
    checkEq(str->getVariables().size(), size_t(2), "member count");
  });

  test("parser/struct-typed-parameter", [] {
    auto module = parse("struct S { var a; }\n"
                        "def f(S s) { return s.a; }\n"
                        "def main() { print(1); }");
    check(module != nullptr, "parses");
    if (!module)
      return;
    FunctionAST *f = nullptr;
    for (auto &record : *module)
      if (auto *func = llvm::dyn_cast<FunctionAST>(record.get()))
        if (func->getProto()->getName() == "f")
          f = func;
    check(f != nullptr, "found f");
    if (!f)
      return;
    // A struct-typed parameter carries the struct's name in VarType::name,
    // which is how MLIRGen later resolves it to a !toy.struct type.
    checkEq(f->getProto()->getArgs().size(), size_t(1), "one parameter");
    if (f->getProto()->getArgs().empty())
      return;
    checkEq(f->getProto()->getArgs()[0]->getType().name, std::string("S"),
            "parameter type name");
  });

  test("parser/member-access", [] {
    auto module = parse("struct S { var a; }\n"
                        "def f(S s) { return s.a; }\n"
                        "def main() { print(1); }");
    if (!module)
      return;
    FunctionAST *f = nullptr;
    for (auto &record : *module)
      if (auto *func = llvm::dyn_cast<FunctionAST>(record.get()))
        if (func->getProto()->getName() == "f")
          f = func;
    if (!f)
      return;
    auto *ret = llvm::dyn_cast<ReturnExprAST>(f->getBody()->front().get());
    check(ret != nullptr, "body starts with return");
    if (!ret || !ret->getExpr().has_value())
      return;
    auto *binop = llvm::dyn_cast<BinaryExprAST>(*ret->getExpr());
    check(binop != nullptr, "'.' parses as a binary operator");
    if (binop)
      checkEq(binop->getOp(), '.', "operator");
  });

  test("parser/precedence", [] {
    // a + b * c must group as a + (b * c).
    auto module = parse("def f(a, b, c) { return a + b * c; }");
    check(module != nullptr, "parses");
    if (!module)
      return;
    auto *func = firstFunction(*module);
    if (!func)
      return;
    auto *ret = llvm::dyn_cast<ReturnExprAST>(func->getBody()->front().get());
    if (!ret || !ret->getExpr().has_value())
      return;
    auto *add = llvm::dyn_cast<BinaryExprAST>(*ret->getExpr());
    check(add != nullptr, "top level is a binary op");
    if (!add)
      return;
    checkEq(add->getOp(), '+', "top operator is +");
    auto *mul = llvm::dyn_cast<BinaryExprAST>(add->getRHS());
    check(mul != nullptr, "right operand is a binary op");
    if (mul)
      checkEq(mul->getOp(), '*', "nested operator is *");
  });

  test("parser/binop-location-D2", [] {
    // Deviation D2. In "  return a + b;" the '+' is at column 12 and the
    // right-hand side starts at column 14; upstream reports 14 because it
    // reads the location after consuming the operator.
    auto module = parse("def f(a, b) {\n  return a + b;\n}\n");
    check(module != nullptr, "parses");
    if (!module)
      return;
    auto *func = firstFunction(*module);
    if (!func)
      return;
    auto *ret = llvm::dyn_cast<ReturnExprAST>(func->getBody()->front().get());
    if (!ret || !ret->getExpr().has_value())
      return;
    auto *binop = llvm::dyn_cast<BinaryExprAST>(*ret->getExpr());
    check(binop != nullptr, "is a binary op");
    if (!binop)
      return;
    checkEq(binop->loc().line, 2, "operator line");
    checkEq(binop->loc().col, 12, "operator column (upstream reports 14)");
  });

  test("parser/missing-initializer-D3", [] {
    // Deviation D3: upstream accepts this, stores a null initializer, and only
    // fails later in MLIRGen (toyc-ch1's AST dumper asserts on it outright).
    std::string diagnostic;
    std::unique_ptr<ModuleAST> module;
    diagnostic = captureStderr([&] { module = parse("def main() { var a = ; }"); });
    check(module == nullptr, "the parse fails instead of succeeding");
    check(!diagnostic.empty(), "a diagnostic is reported");
  });

  test("parser/error-text", [] {
    // The wording is upstream's, so that expectations written against the
    // tutorial still hold.
    std::string diagnostic;
    diagnostic = captureStderr([&] { parse("foo\n"); });
    check(diagnostic.find("Parse error (1, 1)") != std::string::npos,
          "error names line and column, got: " + diagnostic);
    check(diagnostic.find("expected ''def' or 'struct''") != std::string::npos,
          "error states what was expected, got: " + diagnostic);
  });
}

//===----------------------------------------------------------------------===//

int main() {
  lexerTests();
  parserTests();

  std::cout << (numFailures ? "FAILED" : "PASSED") << ": " << numChecks
            << " checks, " << numFailures << " failures\n";
  return numFailures ? 1 : 0;
}
