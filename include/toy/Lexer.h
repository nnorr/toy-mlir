//===- Lexer.h - Lexer for the Toy language -------------------------------===//
//
// Adapted from mlir/examples/toy/Ch7 in the LLVM Project, under the Apache
// License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Character stream -> tokens, with the source location of each token.
//
// Two deviations from the upstream tutorial's lexer, both documented in
// ARCHITECTURE.md:
//
//  1. One concrete class over a buffer, instead of an abstract Lexer with a
//     virtual readNextLine() and a LexerBuffer subclass. That indirection
//     exists upstream to support a REPL that Toy never grew; without it the
//     lexer is a plain object that FrontendTests can drive directly.
//  2. Malformed numbers are reported instead of silently truncated. Upstream
//     turns `1.23.45` into 1.23 and says nothing.
//
// The implementation lives in src/Lexer.cpp; nothing here is inline except the
// trivial accessors the parser calls in a loop.
//
//===----------------------------------------------------------------------===//

#ifndef TOY_LEXER_H
#define TOY_LEXER_H

#include "llvm/ADT/StringRef.h"

#include <cassert>
#include <memory>
#include <string>

namespace toy {

/// A location in a source file. The filename is shared rather than copied per
/// token: every node of a parsed module points at the same string, and MLIRGen
/// turns this into an mlir::FileLineColLoc.
struct Location {
  std::shared_ptr<std::string> file; ///< filename.
  int line;                          ///< line number, 1-based.
  int col;                           ///< column number, 1-based.
};

/// The token kinds. Single-character tokens keep their ASCII value, so the
/// parser can compare against ';' or '(' directly; everything else is negative.
enum Token : int {
  tok_semicolon = ';',
  tok_parenthese_open = '(',
  tok_parenthese_close = ')',
  tok_bracket_open = '{',
  tok_bracket_close = '}',
  tok_sbracket_open = '[',
  tok_sbracket_close = ']',

  tok_eof = -1,

  // commands
  tok_return = -2,
  tok_var = -3,
  tok_def = -4,
  tok_struct = -5,

  // primary
  tok_identifier = -6,
  tok_number = -7,
};

/// Human-readable spelling of a token, for diagnostics.
std::string getTokenName(int tok);

/// Turns a buffer of Toy source into a token stream.
///
/// The lexer owns a one-token lookahead (`curTok`) plus a one-character
/// lookahead (`lastChar`): a token can only be known to have ended once the
/// character after it has been read, and that character cannot be pushed back
/// into the stream.
class Lexer {
public:
  /// `buffer` must outlive the lexer. `filename` is carried in every Location
  /// this lexer produces and is used only for diagnostics.
  Lexer(llvm::StringRef buffer, std::string filename);

  /// The current token, without advancing.
  Token getCurToken() const { return curTok; }

  /// Advance and return the new current token.
  Token getNextToken() { return curTok = lexToken(); }

  /// Advance, asserting that the current token is what the caller expected.
  /// Parser code that has already tested the token uses this to make the
  /// expectation explicit.
  void consume(Token tok) {
    assert(tok == curTok && "consume() Token mismatch expectation");
    getNextToken();
  }

  /// The identifier just lexed. Precondition: getCurToken() == tok_identifier.
  llvm::StringRef getId() const {
    assert(curTok == tok_identifier);
    return identifierStr;
  }

  /// The number just lexed. Precondition: getCurToken() == tok_number.
  double getValue() const {
    assert(curTok == tok_number);
    return numVal;
  }

  /// Location of the first character of the current token.
  Location getLastLocation() const { return lastLocation; }

  int getLine() const { return curLineNum; }
  int getCol() const { return curCol; }

  /// True if any token was malformed. A bad token still produces a usable
  /// value, so lexing continues and the user sees more than one error. The
  /// driver therefore has to ask before trusting the parse.
  bool hadError() const { return errorState; }

private:
  /// Next character from the buffer, or EOF, maintaining line/column.
  int getNextChar();

  /// Lex one token.
  Token lexToken();

  /// Report a malformed token at the current location and set errorState.
  void emitError(llvm::StringRef message);

  llvm::StringRef buffer;      ///< Remaining input.
  std::string identifierStr;   ///< Set when curTok == tok_identifier.
  double numVal = 0;           ///< Set when curTok == tok_number.

  Token curTok = tok_eof;      ///< Current token.
  Location lastLocation;       ///< Location of curTok.
  int lastChar = ' ';          ///< One-character lookahead.
  int curLineNum = 1;
  int curCol = 0;
  bool errorState = false;
};

} // namespace toy

#endif // TOY_LEXER_H
