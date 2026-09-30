# 14. What changed from upstream

`reference/Ch1` through `reference/Ch7` hold the upstream tutorial verbatim, so
every claim about how far this repo has moved can be measured instead of
asserted. This document is the map: which files were taken as they are, which
were restructured and why, which were written here, and where upstream is simply
wrong.

## Which document answers which question

Three different things get called "the difference from upstream", and they live
apart on purpose.

| Question | Where |
| --- | --- |
| How is the code organized differently, and which file came from where | here |
| Why is it organized that way | [ARCHITECTURE.md](../ARCHITECTURE.md), "Design decisions" |
| Where does the compiler *behave* differently | [ARCHITECTURE.md](../ARCHITECTURE.md), "Deviations from upstream", D1 to D13 |
| How does the equivalence sweep admit those behavior differences | [tests/compat/EXPECTED-DIFFS.md](../tests/compat/EXPECTED-DIFFS.md) |

So this document names a deviation and moves on rather than re-explaining it. A
code change that exists because of D9 says D9.

## How the measurement works

`tests/upstream-diff.sh` prints the table below. Both sides are normalized
before comparison: whole comment lines and blank lines dropped, runs of
whitespace collapsed, and the remaining lines sorted.

Sorting matters here. Moving a function from one file to another is not a change
in logic, and most of what this repo did to upstream is that kind of move.
Without sorting, `main.cpp` would look rewritten when the code was only
relocated.

Two consequences to keep in mind while reading the numbers:

- A modified line counts twice, once as removed and once as added. `Dialect.h`
  reports 6, and that is three changed lines.
- A comment on the end of a line of code is not stripped, so rewording one counts
  as a difference. Only whole comment lines are ignored.

A row is a group on both sides, because several of our files were split out of
one of theirs and a file-to-file table would hide that. Every file belongs to
exactly one row, so nothing is counted twice.

```console
$ tests/upstream-diff.sh
OURS                  UPSTREAM                    OURS    UP DIFFER
-------------------------------------------------------------------
Lexer.{h,cpp}         Lexer.h                      274   236    123
AST.h                 AST.h                        323   313      3
ASTVisitor.h          (no Toy counterpart)          81     -      -
Parser.{h,cpp}        Parser.h                     803   683    186
ASTDumper.{h,cpp}     AST.cpp                      521   274    435
MLIRGen.{h,cpp}       MLIRGen.{h,cpp}              742   726    228
Ops.td                Ops.td                       498   459     87
Dialect.h             Dialect.h                     94    82      6
dialect/*.cpp (6)     Dialect.cpp+ToyCombine.cpp  1047   754     83
ToyCombine.td         ToyCombine.td                 72    63      4
ShapeInference (3)    ShapeInference{Iface,Pass}   197   181     11
Passes.h              Passes.h                      43    35      4
LowerToAffine.cpp     LowerToAffineLoops.cpp       389   368      0
LowerToLLVM.cpp       LowerToLLVM.cpp              244   239      0
driver (7)            toyc.cpp                     953   333    424
ObjectEmitter.{h,cpp} (no Toy counterpart)         169     -      -
DebugInfo.{h,cpp}     (no Toy counterpart)         326     -      -
```

Upstream names drop their directory, which is unambiguous because no two files
under `reference/Ch7` share a name.

`tests/check-docs.py` runs that script and diffs its output against the block
above, so these numbers cannot go stale. Change a pass and the `docs` test fails
until someone reruns the script and reads what moved.

## The three passes that are upstream's logic

`LowerToAffine.cpp` and `LowerToLLVM.cpp` report zero. After normalization they
are character for character upstream's `LowerToAffineLoops.cpp` and
`LowerToLLVM.cpp`. Everything that differs is comments, and the file names.

That bounds what this repo can claim. The two lowerings are the densest code in
the tutorial, and they were moved and commented rather than improved.

`ShapeInference` reports 11 across three files, and all eleven are accountable:

- Six are three renamed include guards, `MLIR_TUTORIAL_TOY_SHAPEINFERENCEINTERFACE_H_`
  becoming `TOY_SHAPEINFERENCEINTERFACE_H`.
- Four are the interface's ODS `description`, rewritten.
- One is upstream's `#include "toy/ShapeInferenceOpInterfaces.cpp.inc"`, which
  moved to `src/dialect/Interfaces.cpp`. ARCHITECTURE.md explains why the
  interface belongs to the dialect.

The worklist algorithm itself is untouched.

## The small differences, substantiated

Four rows report single digits. Each one breaks down as follows.

| Row | Count | What it is |
| --- | --- | --- |
| `AST.h` | 3 | Two added includes, `<memory>` and `<string>`. One removed declaration, `void dump(ModuleAST &);`, because the dumper became its own module |
| `Passes.h` | 4 | Two changed lines: `std::unique_ptr<mlir::Pass>` written as `std::unique_ptr<Pass>` inside `namespace mlir` |
| `Dialect.h` | 6 | Three changed lines, all the include guard |
| `ToyCombine.td` | 4 | The `ReshapeConstant` native code call, which now broadcasts a one-element constant instead of asserting. That is D11 |

`ToyCombine.td`'s four lines are the only case here where a tiny diff carries a
behavior change. The other three are naming and includes.

## Lexer: one class instead of two

Upstream's `Lexer.h` declares an abstract `Lexer` with a pure virtual
`readNextLine()`, and a `LexerBuffer` subclass that supplies lines from memory.
The indirection exists so a REPL could feed lines from a terminal, and Toy never
grew one.

Here `include/toy/Lexer.h` declares one concrete class over a `StringRef` and
`src/Lexer.cpp` implements it. A lexer you can construct with a string is
testable on its own, which is what `tests/FrontendTests.cpp` does.

The 123 differing lines are that restructuring plus D1, D9, and the removal of a
`getTokenName()` that nothing called.

## Parser: a header split into a grammar

Upstream keeps the entire parser, declarations and bodies, in `Parser.h`. Here
`include/toy/Parser.h` is the production list and `src/Parser.cpp` is the
implementation, which makes the grammar readable as a list of rules.

Two functions are file-local statics in `src/Parser.cpp` rather than members,
`parseCallTail` and `parseTypedDeclarationTail`. They exist because `parseBlock`
needs both tails and one token of lookahead cannot tell `transpose(a)` from
`Struct value`: the parser has to consume the identifier before it knows which it
is reading, and the two tails pick up from there.

The rest of the 186 lines are D2, D3 and D7.

The parser is a port even though `src/Parser.cpp` has no upstream file of that
name, so reading the table by filename alone would mislead.

## ASTDumper: free functions into a visitor, plus a second format

Upstream's `parser/AST.cpp` is a set of free functions with their own switch over
node kinds. Here the switch is `ASTVisitor` and the dumper is one of its two
consumers, which ARCHITECTURE.md covers under "One tree walk".

The row reports 435 of 521 lines, the largest proportion in the table, for two
reasons. The visitor rewrote the dispatch, and `Style::Kaleidoscope` roughly
doubles the file by adding a second rendering of every node.

`Style::Toy` has to stay byte-identical to `toyc-ch7 -emit=ast`, which the
equivalence sweep checks. Three oddities are therefore reproduced on purpose, each
marked in `src/ASTDumper.cpp`:

- The trailing space in `"Function \n"`.
- A number printed with no label, where every other node has one.
- `"Struct Literal: "` printed without a newline, so its first child lands on the
  same line.

The first is a literal `os << "Function \n";` with a comment beside it explaining
the space, so a later reader does not tidy it away.

## MLIRGen: the largest real change

228 of 742 lines. Four things account for them.

The nine-case switch over expression kinds is gone, replaced by
`ASTVisitor<MLIRGenImpl, mlir::FailureOr<mlir::Value>>`. Upstream's `mlirGen`
overload set became distinct names, `mlirGenStruct`, `mlirGenPrototype`,
`mlirGenFunction` and `mlirGenBlock`, because overloading on AST type no longer
disambiguates once the expression cases are fixed hook names.

`mlirGenExpr` is new and holds upstream's `default:` rejection of a var
declaration, return or print appearing where an expression was expected. The
parser can nest a `print` inside an expression, so the path is reachable.

D4 added a diagnostic where upstream returns failure silently.

Then the code review found two genuine bugs, both inherited from upstream, listed
in the next section as D12 and D13.

## Ops.td and the dialect

`Ops.td` reports 87. Most of that is rewritten `description` blocks, which become
the generated dialect documentation. The changes that are not prose:

- `let extraClassDeclaration` adding `registerTypes()` and `registerInterfaces()`.
- Three `let hasVerifier = 1` lines, on `AddOp`, `MulOp` and `ReshapeOp`, for D10.
- `DeclareOpInterfaceMethods<SymbolUserOpInterface>` on `GenericCallOp`, also D10.

The `dialect/*.cpp` row compares our six files against upstream's `Dialect.cpp`
and `ToyCombine.cpp` together, because the split crosses both: the folders live in
upstream's `ToyCombine.cpp` and in our `Folders.cpp`, and the canonicalization
patterns moved from beside the passes into the dialect. ARCHITECTURE.md explains
why under "The dialect owns its canonicalization patterns"; the short version is
that leaving them with the passes made two static archives mutually dependent and
the link failed.

83 differing lines across 1047 is the lowest ratio of any restructured row, which
fits: the dialect was split up rather than rewritten, and sorting makes a pure
move invisible. The verifiers added for D10 are most of what remains.

## The driver

Upstream's `toyc.cpp` is 333 lines doing argument parsing, pipeline construction,
translation to LLVM IR, the JIT, and output. Here that is seven files totalling
953 lines: `main.cpp` keeps the options and the stage dispatch, and
`Pipeline`, `Translate` and `Jit` each take one job, with headers.

424 lines differ, the largest absolute count in the table, and the growth from 333
to 953 is mostly new capability rather than restructuring: `-c`, `-g`,
`--print-pipeline`, `--dump-ast-style`, the target flags, and a `TextOutput` class
that `-o` writes through. Those are D6.

## Written here

Three groups have no Toy counterpart at all.

`ASTVisitor.h` is the shared tree walk. It exists because upstream writes the same
switch twice.

`ObjectEmitter.{h,cpp}` emits a native object file. The tutorial stops at the JIT,
so there is no upstream code to adapt, and the file is written against LLVM's own
codegen API. Its header lists the LLVM headers it was written from.

`DebugInfo.{h,cpp}` has no Toy counterpart, and it is not original either. It is
adapted from `mlir/lib/Dialect/LLVMIR/Transforms/DIScopeForLLVMFuncOp.cpp` in the
LLVM Project, which is why it carries the Apache header while the other two do
not. It attaches a compile unit naming the `.toy` source, where upstream's
unmodified pass produces one named `MLIR`.

## Where upstream is wrong

Two commits changed behavior rather than structure. `cb230e7` fixed bugs found in
a code review, each a crash or a silently wrong compile inherited from upstream,
and each recorded as a deviation with a fixture and a lit test.

| Fix | Upstream | Here | Deviation |
| --- | --- | --- | --- |
| `def f(1)` | `getId()` asserts, process aborts, exit 134 | Parse error | D7 |
| A byte at or above 0x80 | Read as signed `char`, so 0xFF equals `EOF` and the rest of the file is silently dropped | Read through `unsigned char` | D9 |
| `toy.reshape` element count | No verifier; the fold asserts inside `DenseElementsAttr::reshape` under `-opt` | `ReshapeOp::verify` rejects a mismatch | D10 |
| `toy.add`, `toy.mul` operand shapes | No verifier; the lowering loops over the left operand's shape and reads past the end of a smaller right one | `verifyElementwiseOperands` | D10 |
| `toy.generic_call` argument count | No verifier; an arity mismatch surfaces after inlining as a shape inference failure that never mentions the call | `verifySymbolUses` | D10 |
| `var a<2, 2> = 5.5;` | Aborts in `DenseElementsAttr::reshape` | The one-element constant is broadcast with `resizeSplat` | D11 |
| A `print` that fails to generate | Returns success, so the compile exits 0 after printing a diagnostic | Returns failure | D12 |
| Calling a function that returns nothing | Indexes the callee's empty result list and aborts, exit 134 | Diagnostic naming the function | D13 |

Three of those are verifiers ODS cannot express, since each relates two things the
constraint system cannot see at once. The element-wise one is representative
(`src/dialect/Ops.cpp`), and note that it passes unranked operands, because shape
inference has not run yet when the verifier first fires:

```c++
static llvm::LogicalResult verifyElementwiseOperands(mlir::Operation *op) {
  Type lhsType = op->getOperand(0).getType();
  Type rhsType = op->getOperand(1).getType();
  if (!llvm::isa<RankedTensorType>(lhsType) ||
      !llvm::isa<RankedTensorType>(rhsType) || lhsType == rhsType)
    return mlir::success();
  return op->emitOpError("operand shapes must match, got ")
         << lhsType << " and " << rhsType;
}
```

That is eight entries under a commit titled "seven crash and miscompile bugs".
The count in the title is off by one, or it groups the reshape verifier and the
splat broadcast as a single change to reshape semantics, which is defensible since
both touch the same operation. The eight rows above are what the diff actually
contains.

One inherited defect is kept on purpose. Upstream's own diagnostics repeat the
prefix that `emitError` already prints, so `toyc-ch7` says `error: error: unknown
variable 'x'`. Those strings are compared byte for byte by the sweep, so they keep
the doubling; only D4's message, which is this repo's own, does not double it.
ARCHITECTURE.md records this under the deviations table.

## Dead code removed

`ceef9c7` removed two functions that coverage showed had no callers,
`getStageName()` and `getTokenName()`, and turned an unreachable check in
`MLIRGen` into an assertion: the parser never reads an initializer for a struct
member, so the diagnostic could not fire. Line coverage went from 81.2% to 85.2%.

Neither change affects output, so neither is a deviation.

## License and provenance

Counted by grepping the tree.

Twenty-nine files under `include/` and `src/` carry the Apache-2.0 with LLVM
Exceptions header, and every one of them also names its source: 28 with an
`Adapted from` line, and `src/ASTDumper.cpp` with "The Style::Toy half is adapted
from", which is more precise because only half of that file is a port.

Seven files carry no upstream header, and they are exactly the ones with no
upstream code in them: `ASTVisitor.h`, `ASTDumper.h`, `ObjectEmitter.{h,cpp}`,
`DebugInfo.h`, `Pipeline.h` and `Translate.h`. Note that `src/Pipeline.cpp` and
`src/Translate.cpp` do carry it, since their bodies come from `toyc.cpp` while
their interfaces are new.

`reference/` is upstream copied verbatim, unmodified, with `LICENSE.TXT` at the
repo root.
