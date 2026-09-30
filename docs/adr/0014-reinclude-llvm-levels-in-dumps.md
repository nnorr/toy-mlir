# 0014. Put the LLVM levels back into the dumps

Status: accepted
Provenance: recorded in the implementing session

## Context

[0013](0013-exclude-llvm-levels-from-dumps.md) stopped the committed dumps at the
LLVM dialect, on the grounds that `-emit=llvm` is LLVM's code generation rather
than one of this compiler's abstraction levels, and that its debug-metadata
numbering churns between LLVM versions.

Then the dumps acquired a second audience. They were extended to cover everything a
talk needs, and for that audience the LLVM level is not an appendix: the `-O3`
result is the evidence that the lowering was real.

## What changed the balance

Measured on `reference/tests/Ch2/codegen.toy`: 186 lines of LLVM IR with 18
branches and 2 `printf` calls at `-O0`, against 44 lines with no branches, 6
`printf` and 4 `putchar` at `-O3`. The loop nests the affine level built are gone,
folded into a handful of calls.

Nothing about the version-churn argument became false. It was outweighed, which is
a different thing from being wrong, and this record keeps [0013](0013-exclude-llvm-levels-from-dumps.md)
rather than editing it.

## Options considered

| Option | Trade-off |
| --- | --- |
| Keep the exclusion and show the O3 result in slides only | The dumps stay stable. The most convincing artifact then lives outside the repo, where nothing regenerates or checks it |
| Include the LLVM levels in the compared set | Everything the talk needs comes from one command, and the test catches drift. An LLVM update will fail the `dumps` test until someone reruns and reads the diff |
| Include them but exempt them from comparison | Stable build, available dumps. Same objection as before: an unchecked dump goes wrong quietly |

## Decision

Include them, compared like everything else, and document the version sensitivity
where a reader will meet it. An LLVM bump failing the `dumps` test is the correct
outcome: regenerating is one command, and the diff is worth reading.

## Consequences

One run now produces 82 dumps across 520 KB, up from 30 and 196 KB, covering the
AST through LLVM IR plus the generic form, locations, the pipeline listings, the
per-pass trace, pass statistics, object symbols and the optional scf and cf steps.

The `dumps` test is now sensitive to the LLVM version in a way it was not. That is
stated in `docs/examples/dumps/README.md` rather than left to be discovered on the
next upgrade.

Determinism was checked rather than assumed, since a nondeterministic dump would
break the build for everyone: two independently generated trees are byte-identical,
`--check` passes twice in a row, and appending a line to a dump still fails. No
timings, process ids or absolute paths reach a committed file, which is why `ctest`
output is excluded and the object file is written under `build/` and discarded with
only `nm` output kept.

## Evidence

- Both commits are on the `example-dumps` branch: `381c809` established the dumps
  under [0013](0013-exclude-llvm-levels-from-dumps.md), `c28aafd` reversed it.
- 83 tracked paths under `docs/examples/dumps/` on that branch, which is 82 dumps
  plus the README:
  `git ls-tree -r --name-only example-dumps -- docs/examples/dumps | wc -l`.
- The measured `-O0` against `-O3` comparison is recorded in
  `docs/examples/dumps/README.md` on that branch.
- Neither this ADR's subject nor [0013](0013-exclude-llvm-levels-from-dumps.md)'s is
  present on `main` yet. Both branches are unmerged.
