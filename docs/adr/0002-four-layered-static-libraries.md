# 0002. Four layered static libraries

Status: accepted
Provenance: recorded in the implementing session

## Context

Once the code is not organized by chapter, it needs some other organization. The
claim this repo makes about its front end is that it carries no MLIR dependency
at all, and a claim like that is worth making checkable rather than asserting it
in a comment.

## Options considered

| Option | Trade-off |
| --- | --- |
| One library for everything | Simplest CMake. The layering exists only as a convention, and nothing stops `Lexer.cpp` from including an MLIR header |
| Four layered libraries | A real link boundary, so the front end can be linked alone and inspected. More CMake, and archives can form cycles |
| Header-only, with the driver as the only translation unit | No link boundaries at all, and compile times grow with every consumer |

## Decision

Four static libraries: `ToyFrontend`, `ToyDialect`, `ToyPasses`, `ToyCodegen`,
plus the `toyc` executable. Each depends only on the ones below it.

## Consequences

`tests/FrontendTests` links `ToyFrontend` alone, so "the front end does not
depend on MLIR" is a link-time property. It can be inspected with `nm` and `ldd`
rather than taken on trust.

The cost arrived immediately: two of these libraries formed a cycle as soon as
the canonicalization patterns were placed with the passes, which is
[0004](0004-canonicalization-patterns-in-the-dialect.md). A single library would
not have had that failure mode, and would also not have had the property that
made the failure worth fixing properly.

Plain `add_library` was used rather than `add_mlir_library`, because the MLIR
helper also registers into MLIR's global library bookkeeping, which an
out-of-tree project does not need and which makes a broken link harder to read.

## Evidence

- The four libraries and their sources, `src/CMakeLists.txt:31`, `:45`, `:71`,
  `:105`, and the executable at `:152`.
- The reasoning for plain `add_library` is recorded in that file at `:13`.
- `nm -C build/src/libToyFrontend.a | grep -c 'mlir::'` returns 0.
