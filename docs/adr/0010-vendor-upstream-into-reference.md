# 0010. Vendor upstream Ch1 to Ch7 into `reference/`

Status: accepted
Provenance: recorded in the implementing session

## Context

The gate in [0008](0008-differential-equivalence-as-the-gate.md) diffs against
upstream's source and runs upstream's binary. The provenance table compares our
files against theirs. Both need upstream's tree to be reachable, and the question
is whether that reachability is a property of the repo or of the machine.

## Options considered

| Option | Trade-off |
| --- | --- |
| Vendor Ch1 to Ch7 and the Toy tests into `reference/` | The gate and the provenance table work from a bare clone. About 1.4 MB of someone else's code, which has to keep its license headers |
| Point at `~/dev/08_mlir_toy/llvm-project` | Nothing vendored. The repo then only works on a machine with that tree at that path, and the comparison silently has no baseline elsewhere |
| A git submodule pinned to a revision | Reproducible and not vendored. Pulls the whole LLVM monorepo for seven directories, and adds a clone step people forget |

## Decision

Vendor it. `reference/Ch1` through `reference/Ch7` plus `reference/tests` are
upstream's files unmodified, with `LICENSE.TXT` at the repo root.

## Consequences

The equivalence harness, the provenance table and the dumps all work from a clone
with no external tree, and a reader can diff our file against upstream's without
fetching anything.

The costs are honest ones. Most of the repository by volume is LLVM's code rather
than the author's: 1.4 MB under `reference/` against 344 KB of compiler source in
`include/` and `src/`. Every vendored file keeps its
Apache-2.0-with-LLVM-exception header, and
files adapted from them carry that header plus an "Adapted from" line, which is
the condition for redistributing them at all.

Upstream's binary is still external. `toyc-ch7` comes from the MLIR build, not from
`reference/`, so the sweep is guarded on finding it and degrades to skipped when it
is missing. Vendoring the sources does not make the gate machine-independent, only
the diffs.

## Evidence

- `du -sh reference` reports 1.4 M.
- `LICENSE.TXT` at the repo root.
- Provenance and header conventions are described on the `upstream-diff` branch in
  `docs/14-upstream-diff.md`.
- The sweep's guard on the upstream binary, `tests/CMakeLists.txt:154` and the
  message at `:163`.
