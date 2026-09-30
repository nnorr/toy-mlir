# 0007. Textual output goes to stderr, matching upstream

Status: accepted
Provenance: recorded in the implementing session

## Context

The specification handed to the driver's implementer said that with no `-o`, the
textual stages print to stdout, which is what a Unix tool is expected to do.

The implementer checked upstream instead of assuming, and reported that
`toyc-ch7` prints every textual stage to stderr: `-emit=ast`, `mlir`,
`mlir-affine`, `mlir-llvm` and `llvm` all produce nothing on stdout. Upstream
reaches that through `module->dump()` and `llvm::errs() << *llvmModule`, neither
of which touches stdout.

That turns a style question into a compatibility question, because
[0008](0008-differential-equivalence-as-the-gate.md) requires byte-identical
output and upstream's own lit tests capture with `2>&1`.

## Options considered

| Option | Trade-off |
| --- | --- |
| Match upstream and print to stderr | Byte-equivalence holds, and upstream's lit tests transfer unchanged. Surprises anyone who redirects with `>` |
| Print to stdout and record it as a deviation | Conventional behavior. Adds a deviation that shows up on every stage of every comparison, drowning the sweep in noise |
| stdout for the artifact, stderr for diagnostics | What a careful tool would do. Same noise problem, plus it splits streams the tests interleave |

## Decision

Match upstream. `-o` writes to a file, and with no `-o` the text goes to stderr.
The option's help string says so, rather than leaving it to be discovered.

## Consequences

`toyc prog.toy -emit=mlir > out.mlir` produces an empty file, which is a real trap.
The `-o` flag exists for that case, and `docs/11-driver-and-pipeline.md` states the
behavior.

Every sweep comparison merges the two streams, which is why a diagnostic and the
IR can be compared together, and why the `-emit=jit` case is compared twice: once
merged, once on stdout alone, where the program's own printed values appear.

## Evidence

- The help text, `src/main.cpp:119`, "Write output to `<file>` instead of stderr".
- The reasoning in `TextOutput`'s comment, `src/main.cpp:167-171`.
- Behavior check, from the repo root:
  `build/bin/toyc docs/examples/ex.toy -emit=mlir 1>/dev/null` still prints the
  module, and `2>/dev/null` silences it.
- Stream handling in the sweep is described in `tests/compat/EXPECTED-DIFFS.md`,
  under "The sweep".
