# 0015. The front end's shape: one concrete lexer, a split parser, two dump formats

Status: accepted
Provenance: recorded in the implementing session

## Context

Three smaller decisions about the front end, recorded together because each is the
same kind of judgement: upstream's shape exists for a reason that does not apply
here.

## The lexer

Upstream's `Lexer` is abstract, with a virtual `readNextLine()` and a `LexerBuffer`
subclass that reads from a memory buffer. The indirection exists so a REPL could
supply lines from a terminal, and Toy never grew a REPL.

| Option | Trade-off |
| --- | --- |
| Keep the abstract base and its subclass | Diffs against upstream line for line. Carries an extension point nothing uses, and a virtual call per line |
| One concrete class over a `StringRef` | A plain object a unit test can construct directly. Anyone wanting a REPL would have to reintroduce the seam |

Chose the concrete class. The unit tests drive it with no MLIR linked, which is the
property [0002](0002-four-layered-static-libraries.md) is about.

## The parser

Upstream keeps the entire grammar in `Parser.h`, roughly 680 lines of
implementation in a header.

| Option | Trade-off |
| --- | --- |
| Keep it in the header | Matches upstream exactly. Every consumer recompiles the grammar, and the productions are buried in their own bodies |
| Split declaration from implementation | `Parser.h` reads as a list of productions with their grammar comments. Two files instead of one, and a reader diffing against upstream has to look in both |

Chose the split. What it cost: two helpers that the header cannot express cleanly,
`parseCallTail` and `parseTypedDeclarationTail`, exist as file-local statics taking
a `function_ref`, because one token of lookahead cannot separate a call from a
typed declaration and both tails are needed from `parseBlock`. Making them private
members would be cleaner and is deferred until after the equivalence sweep can
prove a behavior-preserving refactor preserved behavior.

## The dumper

| Option | Trade-off |
| --- | --- |
| Upstream's format only | One format, byte-identical, nothing to choose. No way to read a Toy tree beside another compiler's |
| Add a second format | A Toy AST and the Kaleidoscope compiler's AST can be read side by side. Two formats to keep working, and one of them has no upstream to be checked against |

Chose both, with `Style::Toy` byte-identical to `toyc-ch7 -emit=ast` and
`Style::Kaleidoscope` as an alternative rendering. Three formatting oddities in the
upstream format are reproduced deliberately, including a trailing space and an
unlabelled number line, and are marked in the code so nobody tidies them and
breaks the comparison.

## Consequences

The front end is testable without MLIR, the grammar is readable as a list, and the
AST can be printed in either format. The costs are the two file-local helpers, a
second format with no reference to check it against, and three deliberate oddities
that look like defects until the comment beside them is read.

## Evidence

- One concrete lexer over a buffer, `include/toy/Lexer.h`, with the deviations from
  upstream listed in its header comment.
- Productions as declarations, `include/toy/Parser.h`, implementation in
  `src/Parser.cpp`.
- Both styles and the reasoning for the oddities, `src/ASTDumper.cpp` header
  comment; the style enum is in `include/toy/ASTDumper.h`.
- `Style::Toy` equivalence is covered by the `-emit=ast` stage of the sweep in
  [0008](0008-differential-equivalence-as-the-gate.md), where the only permitted
  difference is the binary-operator column.
