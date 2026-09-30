# 0004. Canonicalization patterns belong to the dialect

Status: accepted
Provenance: recorded in the implementing session

## Context

A link failure forced this decision, and the error is the clearest part of the
explanation.

The rewrite patterns were first placed with the passes, in `src/passes/`, on the
reasoning that a pattern is a transformation. `ToyPasses` already depended on
`ToyDialect`, because patterns match operations. But `hasCanonicalizer = 1` in ODS
declares `getCanonicalizationPatterns` as a method of the operation class, and the
generated registration in `libToyDialect.a` therefore referenced a symbol defined
in `libToyPasses.a`.

Two static archives each needing symbols from the other. The linker reported
exactly two undefined references, `ReshapeOp::getCanonicalizationPatterns` and
`TransposeOp::getCanonicalizationPatterns`, reached from
`RegisteredOperationName::Model<...>`.

## Options considered

| Option | Trade-off |
| --- | --- |
| Declare the cycle in CMake so the archives are searched repeatedly | Works, and CMake supports it. Encodes a cycle that should not exist, and the next reader has to work out why |
| Merge `ToyDialect` and `ToyPasses` into one library | Removes the cycle by removing the boundary, and with it the property that [0002](0002-four-layered-static-libraries.md) exists to get |
| Move the patterns into the dialect | Matches what ODS already declared. Puts files named after a transformation under `src/dialect/` |

## Decision

The patterns moved to `src/dialect/ToyCombine.cpp` and `src/dialect/ToyCombine.td`.
The dialect owns its canonicalization patterns; the canonicalizer *pass* that
applies them stays MLIR's generic one, which this repo does not write.

## Consequences

The dependency graph is acyclic again, and `ToyDialect` is self-contained: every
hook `Ops.td` promises is defined in the same library as the operations.

The cost is that `ToyCombine.{cpp,td}` sit beside the dialect rather than beside
the passes, which is not where upstream keeps them, so anyone diffing against
`reference/Ch7/mlir/` has to know that. `docs/06-patterns-and-folding.md` and the
provenance table on the `upstream-diff` branch both note the move.

The general rule this produced: a definition that ODS declares on an operation
belongs in the library that defines the operation, whatever the file is named
after.

## Evidence

- `dialect/ToyCombine.cpp` in the `ToyDialect` source list,
  `src/CMakeLists.txt:51`, with the DRR output generated at `:23` and named as a
  dependency of `ToyDialect` at `:55`.
- `hasCanonicalizer = 1` on `ReshapeOp` at `include/toy/Ops.td:370` and on
  `TransposeOp` at `:489`.
- Both hooks are now defined in the dialect library:
  `nm -C build/src/libToyDialect.a | grep ' T .*getCanonicalizationPatterns'`
  lists `ReshapeOp::getCanonicalizationPatterns` and
  `TransposeOp::getCanonicalizationPatterns`.
