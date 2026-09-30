# 0006. The ShapeInference interface definitions compile into the dialect

Status: accepted
Provenance: recorded in the implementing session

## Context

`ShapeInferenceInterface.td` generates two files. The declarations go into a
header every operation sees. The definitions, `ShapeInferenceOpInterfaces.cpp.inc`,
have to be compiled into exactly one translation unit.

Upstream compiles them into `mlir/ShapeInferencePass.cpp`, which works because
upstream links one executable and the question of which library owns what never
arises. Here it does: five operations declare the interface in `Ops.td`, so the
operation classes in `ToyDialect` need those symbols.

## Options considered

| Option | Trade-off |
| --- | --- |
| Follow upstream and compile them into the pass | Matches the reference file for file. `libToyDialect.a` would then not link without `libToyPasses.a`, inverting the intended dependency direction |
| Compile them into the dialect | The dialect is self-contained. Diverges from upstream's file layout |
| A separate small library for the interface | Honest about the layering. A library for thirteen lines of generated code is not worth the CMake |

## Decision

`src/dialect/Interfaces.cpp` includes the definitions, alongside
`ToyInlinerInterface`. An interface an operation declares is part of what the
dialect promises, so the dialect should carry it.

## Consequences

The dependency runs one way: `ToyPasses` needs `ToyDialect`, never the reverse.
`ShapeInference::inferShapes()` is defined in `libToyDialect.a` and appears as an
undefined symbol in `libToyPasses.a`, which is the direction that makes
`ToyDialect` linkable on its own.

Anyone comparing against `reference/Ch7/mlir/ShapeInferencePass.cpp` will find the
include missing there and should look in `Interfaces.cpp` instead. Both files
carry a comment saying so, because a missing `#include` of a generated file is a
confusing thing to discover from a linker error.

## Evidence

- The include and its reasoning, `src/dialect/Interfaces.cpp:46-52`.
- `src/passes/ShapeInference.cpp` does not include it; that is the one code-line
  difference against upstream's version of the file.
- Symbol direction, both commands run from the repo root:
  `nm -C build/src/libToyDialect.a | grep ' T .*ShapeInference::inferShapes'`
  reports the definition, and
  `nm -C build/src/libToyPasses.a | grep ' U .*ShapeInference::inferShapes'`
  reports the reference.
- The same library also carries the per-operation `Model<...>::inferShapes` thunks
  for `AddOp`, `CastOp`, `ConstantOp`, `MulOp` and `TransposeOp`.
