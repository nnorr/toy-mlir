# 03. Parser

Files: `include/toy/Parser.h` (131), `src/Parser.cpp` (671)

## What it does

The parser is the only component that builds AST nodes. It is recursive descent with precedence climbing for binary operators, one function per production, and no error recovery: a production that fails reports why and returns null, and its caller propagates that.

No semantic checking happens here (`src/Parser.cpp:11`). An undeclared variable, a call to a function that does not exist, and a reshape that changes the element count all parse cleanly. `MLIRGen` and the verifiers catch them later.

## The grammar as implemented

```
module            ::= (definition | struct-definition)*
definition        ::= prototype block
prototype         ::= 'def' identifier '(' ((identifier | identifier identifier) (',' ...)*)? ')'
struct-definition ::= 'struct' identifier '{' (declaration ';')+ '}'
block             ::= '{' (statement ';')* '}'
statement         ::= declaration | 'return' expression? | expression
declaration       ::= 'var' identifier type? '=' expression
                    | identifier identifier ('=' expression)?
type              ::= '<' number (',' number)* '>'
expression        ::= primary (binop primary)*
primary           ::= identifier | identifier '(' args ')' | number
                    | '(' expression ')' | tensor-literal | struct-literal
tensor-literal    ::= '[' (tensor-literal | number) (',' ...)* ']'
struct-literal    ::= '{' (struct-literal | tensor-literal | number) (',' ...)* '}'
```

`include/toy/Parser.h` lists the productions in this order, and `src/Parser.cpp` defines them in the same order.

## Precedence climbing

Four operators, with `.` binding tightest (`src/Parser.cpp:651`):

```c++
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
```

`-` has a precedence but no code generation, so `a - b` parses and then fails in `MLIRGen`. That is upstream's behavior too:

```console
$ build/bin/toyc docs/examples/minus.toy -emit=mlir 2>&1 | head -1
loc("docs/examples/minus.toy":4:13): error: invalid binary operator '-'
```

The loop in `parseBinOpRHS` is the standard one (`src/Parser.cpp:307`): stop when the next operator binds less tightly than the caller's, and recurse when it binds more tightly.

```c++
    // If the operator after rhs binds more tightly, rhs belongs to it.
    int nextPrec = getTokPrecedence();
    if (tokPrec < nextPrec) {
      rhs = parseBinOpRHS(tokPrec + 1, std::move(rhs));
```

## Tensor literals and shape recovery

A literal's shape is not declared. It is recovered from the nesting (`src/Parser.cpp:212`):

```c++
  // The shape is recovered from the nesting rather than declared: this level
  // contributes its own length first.
  dims.push_back(values.size());
```

After that, if any element is itself a literal then all of them must be, and their dimensions must agree. That uniformity check is what makes the shape well defined, and it produces the error you see on a ragged literal:

```console
$ build/bin/toyc docs/examples/ragged.toy -emit=ast 2>&1 | head -2
Parse error (3, 22): expected 'uniform well-nested dimensions' inside literal expression but has Token 59 ';'
Parse error (3, 22): expected 'expression' as the initializer of a variable declaration but has Token 59 ';'
```

`LiteralExprAST` keeps the values nested and the dims flat, and `MLIRGen` flattens the values later when it builds the constant attribute.

## Declarations need two tokens of context

Two forms of declaration exist, and the leading token does not distinguish them from a call. Inside a block, an identifier can start either a call (`transpose(a);`) or a typed declaration (`Struct value = ...;`), and one token of lookahead is not enough. `parseBlock` consumes the identifier and then decides (`src/Parser.cpp:452`):

```c++
    if (lexer.getCurToken() == tok_identifier) {
      // An identifier here is either a call or a typed declaration, and one
      // token of lookahead cannot tell them apart: `transpose(a)` is a call,
      // `Struct value` a declaration. Consume it and decide from what follows.
      auto loc = lexer.getLastLocation();
      std::string id(lexer.getId());
      lexer.consume(tok_identifier);

      std::unique_ptr<ExprAST> expr;
      if (lexer.getCurToken() == '(')
        expr = parseCallTail(lexer, id, loc, ...);
      else
        expr = parseTypedDeclarationTail(lexer, id, /*requiresInitializer=*/true, loc, ...);
```

`parseCallTail` and `parseTypedDeclarationTail` are file-local helpers because both tails have two callers, and `Parser`'s interface lists only the grammar's own productions. They take the recursion back into `parseExpression` as a `llvm::function_ref`. `ARCHITECTURE.md` notes that making them private members is a cleaner alternative.

Parameters have the same ambiguity, resolved the same way (`src/Parser.cpp:525`): the first identifier is only known to be a type once a second one follows it.

`print` is recognised in `parseCallTail` and becomes its own node rather than a call (`src/Parser.cpp:91`), because it lowers to `toy.print` instead of to a call.

## Error format

One function emits every diagnostic, so the wording cannot drift (`src/Parser.cpp:37`):

```c++
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
```

The format is upstream's, and several lit tests match on it.

## Deviation D2: a binary operator's location is its own

Upstream reads the location after consuming the operator, so the recorded position is the start of the right-hand operand. This parser reads it first (`src/Parser.cpp:317`):

```c++
    int binOp = lexer.getCurToken();
    // The operator's own location, taken before it is consumed. Reading it
    // afterwards, as upstream does, records the column of the right-hand
    // operand instead, and that skew survives into MLIR locations and DWARF.
    auto loc = lexer.getLastLocation();
    lexer.consume(Token(binOp));
```

On line 3 of the running example, `return transpose(a) * transpose(b);`, the `*` is at column 23 and the second `transpose` starts at column 25:

```console
$ build/bin/toyc docs/examples/ex.toy -emit=ast 2>&1 | sed -n '7p'
          BinOp: * @docs/examples/ex.toy:3:23
$ ~/dev/08_mlir_toy/build/bin/toyc-ch7 docs/examples/ex.toy -emit=ast 2>&1 | sed -n '7p'
          BinOp: * @docs/examples/ex.toy:3:25
```

The fix is one line, and the consequence reaches the far end of the pipeline. `MLIRGen` copies the location onto `toy.mul`, the lowerings carry it through, and it lands in the LLVM IR as a `!DILocation`:

```console
$ build/bin/toyc docs/examples/ex.toy -emit=mlir -opt --mlir-print-debuginfo 2>&1 | sed -n '5p'
    %2 = toy.mul %1, %1 : tensor<3x2xf64> loc(callsite("docs/examples/ex.toy":3:23 at "docs/examples/ex.toy":9:11))
```

At `-emit=llvm` the same change shows up as an extra `!DILocation` node, because upstream's binop shared a location with its right operand and the two now differ. `tests/compat/EXPECTED-DIFFS.md` records the measurement.

## Deviation D3: a failed initializer is reported and propagated

Upstream builds a `VarDeclExprAST` with a null initializer when the expression fails to parse, and exits 0. This parser reports what was expected and returns null (`src/Parser.cpp:419`):

```c++
      expr = parseExpression();
      // As in the typed form: say what was expected and where.
      if (!expr)
        return parseError<VarDeclExprAST>(
            "expression", "as the initializer of a variable declaration");
```

```console
$ build/bin/toyc docs/examples/badinit.toy -emit=ast 2>&1        # var a = ;
Parse error (3, 11): expected 'expression' as the initializer of a variable declaration but has Token 59 ';'
Parse error (3, 11): expected 'nothing' at end of module but has Token 59 ';'
$ echo $?
1

$ ~/dev/08_mlir_toy/build/bin/toyc-ch7 docs/examples/badinit.toy -emit=ast 2>&1 | sed -n '6,7p'
        VarDecl a<> @docs/examples/badinit.toy:3:3
        Print [ @docs/examples/badinit.toy:4:3
$ echo $?
0
```

Upstream's dump shows the declaration with nothing under it, and the compilation succeeds.

## Deviation D7: a missing `=` no longer aborts the compiler

Upstream hands the `=` straight to `Lexer::consume()`, whose assertion aborts the process when the token is something else. `var a;` therefore takes down the compiler. This parser tests for it first (`src/Parser.cpp:414`):

```c++
      // Test for the '=' rather than handing it straight to consume(), whose
      // assertion aborts the process. Upstream does the latter, so `var a;`
      // takes down the compiler over a one-token typo. The wording matches the
      // typed form above, since both mean "this declaration needs a value".
      if (lexer.getCurToken() != '=')
        return parseError<VarDeclExprAST>("initializer",
                                          "in variable declaration");
```

```console
$ build/bin/toyc docs/examples/noinit.toy -emit=ast 2>&1          # var a;
Parse error (3, 8): expected 'initializer' in variable declaration but has Token 59 ';'
Parse error (3, 8): expected 'nothing' at end of module but has Token 59 ';'
$ echo $?
1

$ ~/dev/08_mlir_toy/build/bin/toyc-ch7 docs/examples/noinit.toy -emit=ast
toyc-ch7: /data/home/sslee/dev/08_mlir_toy/llvm-project/mlir/examples/toy/Ch7/include/toy/Lexer.h:78: void toy::Lexer::consume(Token): Assertion `tok == curTok && "consume Token mismatch expectation"' failed.
$ echo $?
134
```

The shell adds `Aborted (core dumped)` after that line. The absolute path is this checkout's; yours will differ.

The message was chosen to match the typed form, and upstream emits exactly that text for the typed case:

```console
$ ~/dev/08_mlir_toy/build/bin/toyc-ch7 docs/examples/typednoinit.toy -emit=ast 2>&1 | head -2   # S s;
Parse error (6, 6): expected 'initializer' in variable declaration but has Token 59 ';'
Parse error (6, 6): expected 'nothing' at end of module but has Token 59 ';'
```

So D7 gives `var a;` the diagnostic upstream already uses for the same mistake in the other declaration form, including the trailing end-of-module line.

## One upstream quirk kept on purpose

A record that fails after consuming the input up to EOF yields a successful empty module even though a diagnostic was printed. A function missing its closing brace reports `expected '}'` and still dumps `Module:` with exit 0. The comment at `src/Parser.cpp:626` marks this as upstream Ch7 behavior that `tests/compat` pins, so it must not be "fixed" here.

A related trap: upstream Ch1 reports `expected 'def' in prototype` for a file containing only comments, because its `parseModule` loops on `parseDefinition` instead of switching on the token. Ch7 accepts that file silently, and its own lit test asserts `CHECK-NOT: Parse error`. This is the Ch7 grammar, so silence is correct.

## Try it

```console
$ build/bin/toyc <file>.toy -emit=ast                  # see the tree
$ build/bin/toyc <file>.toy -emit=mlir                 # see what MLIRGen made of it
```

The parser's own tests are in `tests/FrontendTests.cpp`, covering precedence, shapes, struct declarations, the `.` accessor, D2's location, D3 and the error text.

## Pitfalls

`parsePrimary` returns null for `;` and `}` without reporting anything (`src/Parser.cpp:300`), which is how an empty statement and an empty block terminate. A caller that treats null as "error already reported" is therefore wrong in those two cases, which is what made D3's diagnostic necessary.

`parseType` accepts `<>` and any number of dimensions. Toy is documented as rank 2 or less, and nothing here enforces it; a rank-3 declaration reaches the dialect and fails there.

Multiple diagnostics for one mistake are normal. The specific failure prints first, then the context, then the end-of-module complaint, because each caller adds its own line as it unwinds.
