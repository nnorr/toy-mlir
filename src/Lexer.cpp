//===- Lexer.cpp - Lexer for the Toy language -----------------------------===//
//
// Adapted from mlir/examples/toy/Ch7/include/toy/Lexer.h in the LLVM Project,
// under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See include/toy/Lexer.h for the shape of the class and why it differs from
// upstream's.
//
//===----------------------------------------------------------------------===//

#include "toy/Lexer.h"

#include "llvm/Support/raw_ostream.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

namespace toy {

std::string getTokenName(int tok) {
  switch (tok) {
  case tok_eof:
    return "eof";
  case tok_return:
    return "return";
  case tok_var:
    return "var";
  case tok_def:
    return "def";
  case tok_struct:
    return "struct";
  case tok_identifier:
    return "identifier";
  case tok_number:
    return "number";
  default:
    break;
  }

  // Everything else is a single-character token that kept its ASCII value.
  if (tok >= 0 && isprint(tok))
    return std::string(1, static_cast<char>(tok));
  return "<unknown token " + std::to_string(tok) + ">";
}

Lexer::Lexer(llvm::StringRef buffer, std::string filename)
    : buffer(buffer),
      lastLocation({std::make_shared<std::string>(std::move(filename)), 0, 0}) {
}

int Lexer::getNextChar() {
  if (buffer.empty())
    return EOF;

  // curCol is the column of the character being returned, so it is bumped
  // before the character is handed out and reset by the newline that ends the
  // line, which makes the first character of the next line column 1.
  ++curCol;
  // Through unsigned char: a plain char sign-extends bytes >= 0x80, and 0xFF
  // would then compare equal to EOF and end the file early.
  int nextChar = static_cast<unsigned char>(buffer.front());
  buffer = buffer.drop_front();
  if (nextChar == '\n') {
    ++curLineNum;
    curCol = 0;
  }
  return nextChar;
}

void Lexer::emitError(llvm::StringRef message) {
  errorState = true;
  llvm::errs() << *lastLocation.file << ':' << lastLocation.line << ':'
               << lastLocation.col << ": error: " << message << '\n';
}

Token Lexer::lexToken() {
  // lastChar always holds a character that has been read but not yet claimed
  // by a token, so skipping whitespace here leaves the first character of the
  // next token in it, and leaves curLineNum/curCol pointing at that character.
  // That is what makes the location below the token's own.
  while (lastChar != EOF && isspace(lastChar))
    lastChar = getNextChar();

  lastLocation.line = curLineNum;
  lastLocation.col = curCol;

  // Identifier: [a-zA-Z][a-zA-Z0-9_]*
  if (isalpha(lastChar)) {
    identifierStr = static_cast<char>(lastChar);
    lastChar = getNextChar();
    while (lastChar != EOF && (isalnum(lastChar) || lastChar == '_')) {
      identifierStr += static_cast<char>(lastChar);
      lastChar = getNextChar();
    }

    if (identifierStr == "return")
      return tok_return;
    if (identifierStr == "def")
      return tok_def;
    if (identifierStr == "struct")
      return tok_struct;
    if (identifierStr == "var")
      return tok_var;
    return tok_identifier;
  }

  // Number: [0-9] ([0-9.])*
  if (isdigit(lastChar)) {
    std::string numStr;
    int dotCount = 0;
    do {
      if (lastChar == '.')
        ++dotCount;
      numStr += static_cast<char>(lastChar);
      lastChar = getNextChar();
    } while (lastChar != EOF && (isdigit(lastChar) || lastChar == '.'));

    // strtod stops at the second '.', so without this `1.23.45` would quietly
    // become 1.23 and the rest of the text would vanish.
    if (dotCount > 1)
      emitError("malformed number '" + numStr + "'");

    numVal = strtod(numStr.c_str(), nullptr);
    return tok_number;
  }

  // Comment: '#' to the end of the line.
  if (lastChar == '#') {
    do {
      lastChar = getNextChar();
    } while (lastChar != EOF && lastChar != '\n' && lastChar != '\r');

    // The comment was a gap between tokens, not a token: start over, which
    // also re-records the location so it points at the real token.
    if (lastChar != EOF)
      return lexToken();
  }

  // Don't consume the EOF: every later call has to keep reporting it.
  if (lastChar == EOF)
    return tok_eof;

  // Anything else is a token in its own right, spelled by its ASCII value.
  Token thisChar = Token(lastChar);
  lastChar = getNextChar();
  return thisChar;
}

} // namespace toy
