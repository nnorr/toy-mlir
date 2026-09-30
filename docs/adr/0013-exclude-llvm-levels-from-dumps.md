# 0013. Leave the LLVM levels out of the committed dumps

Status: superseded by [0014](0014-reinclude-llvm-levels-in-dumps.md)
Provenance: recorded in the implementing session

## Context

The committed IR dumps exist so a reader can see one program at every abstraction
level without building the compiler. That raised the question of where the levels
stop.

`-emit=llvm` is past `translateModuleToLLVMIR`, so it is no longer MLIR at all. It
is LLVM's own code generation, and `docs/09-lowering-to-llvm.md` already covers it.

## Options considered

| Option | Trade-off |
| --- | --- |
| Dump every level including `llvm` | Complete. Adds the level most likely to churn, since LLVM IR debug-metadata numbering shifts when anything upstream of it changes |
| Stop at the LLVM dialect | The dumps stay within MLIR, which is what the repo is about, and the stable levels stay stable |
| Dump `llvm` but exclude it from the comparison test | Available for reading without breaking the build on an LLVM bump. A dump nothing checks is a dump that quietly goes wrong |

## Decision

Stop at the LLVM dialect. Four levels: AST, Toy dialect, affine over memrefs, LLVM
dialect.

The owner's reasoning was that the LLVM level belongs to codegen rather than to
this compiler's abstraction levels, and that `docs/09` already showed it. The
version-churn argument supported the same conclusion.

## Consequences

The committed set was 30 files and 196 KB, and the two most version-sensitive
levels were the ones left out, which made the `dumps` comparison test cheap to keep
green across an LLVM update.

What it cost: the most striking artifact in the set was not in it. At `-O3` LLVM
folds these programs down to a handful of calls, which is the clearest evidence
that the loop nests the affine level built describe real computation. That is what
later reversed the decision.

## Evidence

- The exclusion and its reasoning were recorded in the script header and in
  `docs/examples/dumps/README.md` on the `example-dumps` branch, at commit
  `381c809`.
- Superseded by [0014](0014-reinclude-llvm-levels-in-dumps.md) at commit `c28aafd`
  on the same branch.
