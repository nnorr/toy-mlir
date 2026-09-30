# 0011. Generated `*.inc` files are not committed

Status: accepted
Provenance: recorded in the implementing session

## Context

`mlir-tblgen` turns 608 lines of ODS into 6,714 lines of C++ across seven files: the operation
classes, their builders, verifiers, parsers, printers and adaptors, the interface
dispatch, and the DRR pattern matchers. Four documents discuss that generated code,
and `ARCHITECTURE.md` makes the ODS-to-C++ ratio one of its points.

None of it is in the repository. That was noticed as a possible oversight and
raised as a question, which is how it became a decision rather than an accident.

## Options considered

| Option | Trade-off |
| --- | --- |
| Commit them | The four documents become readable on GitHub without building. Stores a second copy of facts that `Ops.td` already states, and pins the repo to one version of MLIR's generator |
| Exclude them | One source of truth, and the build regenerates on every configure. Nobody browsing the repo can see the generated code those documents quote |
| Commit a curated subset: `ToyCombine.inc` at 174 lines and `ShapeInferenceOpInterfaces.h.inc` at 69 | The two the documents actually teach from, for 243 lines. Still two copies of the same facts, and an arbitrary line between what is committed and what is not |

## Decision

Exclude them. They are covered by the `build/` line in `.gitignore` rather than by
a rule of their own, and the build regenerates them through `ToyOpsIncGen`,
`ToyShapeInferenceInterfaceIncGen` and `ToyCombineIncGen`.

## Consequences

`Ops.td` is the only place the facts live, and a version bump changes the generated
code without any committed file going stale.

The cost is specific and worth stating rather than glossing: code blocks quoting
generated files are the one category `tests/check-docs.py` skips by rule, because
the file may not exist. So the excerpts in `docs/05-dialect.md`,
`docs/06-patterns-and-folding.md` and `docs/07-interfaces.md` are the only quoted
code in the repo that nothing verifies. They can drift when MLIR's generator
changes, and no test will say so.

A reader without a build can still see the generated code by running `mlir-tblgen`
directly, which `docs/05-dialect.md` shows.

## Evidence

- `.gitignore` covers `build/` at line 1; no `.inc` file is tracked, which
  `git ls-files | grep -c '\.inc$'` reports as 0.
- The three generator targets, `include/toy/CMakeLists.txt` and
  `src/CMakeLists.txt:23`.
- The skip rule that leaves those blocks unverified,
  `tests/check-docs.py:304`, reason string "quotes generated code under build/".
