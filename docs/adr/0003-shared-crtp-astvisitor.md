# 0003. One shared CRTP visitor over the AST

Status: accepted
Provenance: recorded in the implementing session

## Context

Upstream walks the AST twice with the same exhaustive switch over the nine
`ExprAST` kinds: once in `parser/AST.cpp` to print the tree, once in
`mlir/MLIRGen.cpp` to emit IR. Adding a node kind means editing both, and nothing
makes the compiler complain if only one is edited.

The two consumers do not agree on a return type. The dumper returns nothing; the
generator returns a value, or a failure.

## Options considered

| Option | Trade-off |
| --- | --- |
| Keep both switches, as upstream does | No new abstraction. The duplication and the silent-omission risk stay |
| A virtual `accept()` on each node | The classic visitor. Forces one return type for all consumers and puts knowledge of its consumers into the AST |
| A CRTP template with the return type as a parameter | Per-consumer return types, no virtual dispatch, AST stays ignorant of consumers. The dispatch switch is a template, so it is less obvious to read |
| `std::variant` plus `std::visit` | Exhaustiveness checked by the type system. Means abandoning LLVM-style RTTI, which the rest of the tree and all of LLVM use |

## Decision

A CRTP `ASTVisitor<Derived, RetTy>` with one switch, in
`include/toy/ASTVisitor.h`. `ASTDumper` instantiates it with `void`, `MLIRGenImpl`
with `mlir::FailureOr<mlir::Value>`.

The return type was specified as bare `mlir::Value` first and changed during
implementation. Statements, meaning a variable declaration, a return, or a print,
have to report success while producing no value, and a null `Value` cannot
distinguish that from failure.

## Consequences

The switch has no `default:`, so adding an `ExprASTKind` makes `-Wswitch` fire in
exactly one file, at the `llvm_unreachable` that closes `visit`. Every visitor
then fails to compile until it handles the new case, which is the intended
behavior rather than an inconvenience.

The cost is the Expression Problem from the closed side. Adding a consumer is
free, which is how the second dump format arrived. Adding a node kind touches
every consumer. MLIR makes the opposite trade: its operation set is open, and a
pass has to cope with operations it has never seen. Anyone reading both halves of
this repo meets both trades within a few hundred lines of each other.

## Evidence

- The template and its single switch, `include/toy/ASTVisitor.h:47-51`.
- Both instantiations named in the header's own comment, `:14-16`.
- `MLIRGenImpl`'s declaration and the `friend` that keeps the hooks private,
  `src/MLIRGen.cpp:81-84`.
- The Expression Problem trade is discussed in `include/toy/ASTVisitor.h` and in
  `docs/02-ast.md`.
