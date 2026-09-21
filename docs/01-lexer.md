# 01. Lexer

Files: `include/toy/Lexer.h`, `src/Lexer.cpp` (147 + 154 lines)

## What it does

The lexer turns a buffer of Toy source into a stream of tokens, and records where each one started. It is the only component that touches characters. Nothing above it has to know that source code is text.

It has no dependency on MLIR, and only a header dependency on LLVM (`StringRef`). That is what lets `tests/FrontendTests.cpp` link and drive it on its own.

## The token set

Single-character tokens keep their ASCII value, so the parser can compare a token against `';'` or `'('` directly. Everything else is negative (`include/toy/Lexer.h:49`):

```c++
enum Token : int {
  tok_semicolon = ';',
  tok_parenthese_open = '(',
  ...
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
```

Toy has four keywords. There is no token for `+`, `*` or `.`, because those arrive as their ASCII values and the parser looks up their precedence in a table (see [03-parser.md](03-parser.md)).

## Two lookaheads

The class holds two pieces of lookahead, and both are needed for different reasons (`include/toy/Lexer.h:137`):

```c++
  Token curTok = tok_eof;      ///< Current token.
  Location lastLocation;       ///< Location of curTok.
  int lastChar = ' ';          ///< One-character lookahead.
```

`lastChar` exists because a token's end is only known once the character after it has been read. Reading the `5` in `123 5` is how the lexer learns that `123` finished, and that character cannot be pushed back into a `StringRef`, so it is kept in the object until a later token claims it.

`curTok` exists for the parser. Recursive descent constantly asks "what is the current token" without wanting to consume it, so `getNextToken()` advances and caches, and `getCurToken()` answers from the cache.

`consume(tok)` is the third access pattern: advance, but assert that the caller's expectation held.

```c++
  void consume(Token tok) {
    assert(tok == curTok && "consume() Token mismatch expectation");
    getNextToken();
  }
```

That assertion is load bearing in an unfortunate way upstream. See deviation D7 in [03-parser.md](03-parser.md), where a missing `=` reaches `consume()` and aborts the compiler.

## Line and column tracking

`getNextChar()` maintains the position (`src/Lexer.cpp:58`):

```c++
int Lexer::getNextChar() {
  if (buffer.empty())
    return EOF;

  // curCol is the column of the character being returned, so it is bumped
  // before the character is handed out and reset by the newline that ends the
  // line, which makes the first character of the next line column 1.
  ++curCol;
  int nextChar = buffer.front();
  buffer = buffer.drop_front();
  if (nextChar == '\n') {
    ++curLineNum;
    curCol = 0;
  }
  return nextChar;
}
```

`lexToken()` then records the position after skipping whitespace and before reading the token's characters (`src/Lexer.cpp:86`):

```c++
  while (lastChar != EOF && isspace(lastChar))
    lastChar = getNextChar();

  lastLocation.line = curLineNum;
  lastLocation.col = curCol;
```

Because whitespace is skipped first, the recorded position is the first character of the token itself. Every AST node copies this `Location`, and `MLIRGen` turns it into an `mlir::FileLineColLoc` that survives to the DWARF line table. A column recorded in the wrong place here reappears in a debugger, which is exactly what deviation D2 is about.

`Location` shares the filename rather than copying it per token (`include/toy/Lexer.h:41`):

```c++
struct Location {
  std::shared_ptr<std::string> file; ///< filename.
  int line;                          ///< line number, 1-based.
  int col;                           ///< column number, 1-based.
};
```

## Identifiers, keywords, comments

Keyword recognition happens after an identifier has been lexed, by comparing the string (`src/Lexer.cpp:101`). There is no keyword table, and `return`, `def`, `struct` and `var` are the only four.

An identifier is `[a-zA-Z][a-zA-Z0-9_]*`, so `_` is allowed after the first character. (Toy's own lexer permits this; the Kaleidoscope compiler in `06_llvm_tutorial` does not, which is worth knowing if you compare the two.)

Comments run from `#` to the end of the line, and the lexer restarts itself rather than returning something (`src/Lexer.cpp:133`):

```c++
  if (lastChar == '#') {
    do {
      lastChar = getNextChar();
    } while (lastChar != EOF && lastChar != '\n' && lastChar != '\r');

    // The comment was a gap between tokens, not a token: start over, which
    // also re-records the location so it points at the real token.
    if (lastChar != EOF)
      return lexToken();
  }
```

The recursive call matters for locations. Restarting re-runs the whitespace skip and re-records `lastLocation`, so a token preceded by a comment still reports its own position.

EOF is never consumed (`src/Lexer.cpp:145`). Every later call keeps returning `tok_eof`, which is what lets the parser's loops test for it repeatedly.

## Deviation D1: malformed numbers are reported

A Toy number is lexed as `[0-9]([0-9.])*`, which accepts text that is not a number. `strtod` then stops at the second `.`, so `1.23.45` silently becomes `1.23` and the rest disappears.

This lexer counts the dots and reports the problem (`src/Lexer.cpp:113`):

```c++
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
```

The token still gets a usable value, so lexing continues and the user can see more than one error per run. The driver asks `hadError()` before it trusts the parse, which is why the error is fatal to the compilation without being fatal to the lexer.

Side by side, on `docs/examples/badnum.toy`, whose third line is `var a = 1.23.45;`:

```console
$ build/bin/toyc docs/examples/badnum.toy -emit=ast
docs/examples/badnum.toy:3:11: error: malformed number '1.23.45'
$ echo $?
8

$ ~/dev/08_mlir_toy/build/bin/toyc-ch7 docs/examples/badnum.toy -emit=ast
  Module:
    Function 
      Proto 'main' @docs/examples/badnum.toy:2:1
      Params: []
      Block {
        VarDecl a<> @docs/examples/badnum.toy:3:3
          1.230000e+00 @docs/examples/badnum.toy:3:11
        Print [ @docs/examples/badnum.toy:4:3
...
$ echo $?
0
```

Upstream compiles the file successfully and silently uses `1.23`. The number line carries no label, which is one of the three formatting oddities [02-ast.md](02-ast.md) lists.

## Design change from upstream

Upstream splits the lexer in two: an abstract `Lexer` with a pure virtual `readNextLine()`, and a `LexerBuffer` subclass that serves one line at a time from a memory buffer. That indirection exists so a REPL could feed lines interactively, and Toy never grew one.

This version is one concrete class over a `llvm::StringRef` (`include/toy/Lexer.h:80`). The virtual call and the line-at-a-time buffering are gone, and the class can be constructed directly in a unit test. `ARCHITECTURE.md` records the decision.

Keeping the observable line and column numbers identical took some care, because upstream starts with `curLineNum = 0` and a fake `"\n"` line buffer whose newline increments the counter to 1, while this version starts at `curLineNum = 1` with the real input. The two schemes agree on every token position, which the differential sweep in `tests/compat` checks over all 40 upstream test inputs.

## Try it

The lexer has no driver of its own. Its behavior is visible through the AST dump, where every node carries the location the lexer recorded:

```console
$ build/bin/toyc docs/examples/ex.toy -emit=ast 2>&1 | head -8
  Module:
    Function 
      Proto 'multiply_transpose' @docs/examples/ex.toy:2:1
      Params: [a, b]
      Block {
        Return
          BinOp: * @docs/examples/ex.toy:3:23
            Call 'transpose' [ @docs/examples/ex.toy:3:10
```

The unit tests drive it directly, including the token kinds, the position arithmetic and D1:

```console
$ ninja -C build FrontendTests && build/tests/FrontendTests
```

## Pitfalls

Textual output goes to stderr, following upstream, so `2>&1` is needed to capture it. A bare `build/bin/toyc file.toy -emit=ast > out.txt` produces an empty file.

`getId()` and `getValue()` assert on the current token kind. They are only valid immediately after the matching token was lexed, and the assertions fire in a debug build if the parser asks at the wrong moment.

`hadError()` is sticky and never resets. One malformed number makes the whole compilation fail, which is the intent.
