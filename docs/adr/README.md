# Architecture decision records

`ARCHITECTURE.md` says what the design is and gives a short reason for each part of
it. These records hold the deliberation behind those reasons: the options that were
considered, the trade-off that decided each one, and what the choice cost. Where a
decision was forced by a compile or link error rather than reached by reflection,
the record says so, because the error is the most useful part of the explanation.

Two fields on each record are worth reading before the prose.

`Status` is `accepted`, or `superseded by NNNN` when a later record replaced it.
A superseded record stays as written rather than being edited, so the reversal is
visible. [0013](0013-exclude-llvm-levels-from-dumps.md) and
[0014](0014-reinclude-llvm-levels-in-dumps.md) are the one such pair.

`Provenance` distinguishes what was recorded from what was reconstructed.
"Recorded in the implementing session" means the alternatives listed were actually
weighed at the time. "Reconstructed from code and commits" means the decision is
visible in the tree but the reasoning is not, so the record describes what the code
does and does not invent a motive. [0009](0009-reproduce-valid-fix-error-paths.md)
is split: its policy is recorded, and the deviations that came out of a later code
review are reconstructed.

| | Decision | Status |
| --- | --- | --- |
| [0001](0001-one-binary-stages-as-a-flag.md) | One binary, with pipeline depth as a flag | accepted |
| [0002](0002-four-layered-static-libraries.md) | Four layered static libraries | accepted |
| [0003](0003-shared-crtp-astvisitor.md) | One shared CRTP visitor over the AST | accepted |
| [0004](0004-canonicalization-patterns-in-the-dialect.md) | Canonicalization patterns belong to the dialect | accepted |
| [0005](0005-registration-hooks-in-ods.md) | Registration hooks declared in ODS | accepted |
| [0006](0006-interface-definitions-in-the-dialect.md) | Interface definitions compile into the dialect | accepted |
| [0007](0007-textual-output-to-stderr.md) | Textual output goes to stderr | accepted |
| [0008](0008-differential-equivalence-as-the-gate.md) | Equivalence against `toyc-ch7` is the gate | accepted |
| [0009](0009-reproduce-valid-fix-error-paths.md) | Reproduce upstream where a valid program sees it, fix the error paths | accepted |
| [0010](0010-vendor-upstream-into-reference.md) | Vendor upstream into `reference/` | accepted |
| [0011](0011-generated-inc-not-committed.md) | Generated `*.inc` files are not committed | accepted |
| [0012](0012-documentation-verified-by-tests.md) | Documentation is verified by tests | accepted |
| [0013](0013-exclude-llvm-levels-from-dumps.md) | Leave the LLVM levels out of the dumps | superseded by 0014 |
| [0014](0014-reinclude-llvm-levels-in-dumps.md) | Put the LLVM levels back | accepted |
| [0015](0015-front-end-shape.md) | The front end's shape | accepted |

## Two things to know while reading

Some of these describe work on branches that are not merged. The dumps in
[0013](0013-exclude-llvm-levels-from-dumps.md) and
[0014](0014-reinclude-llvm-levels-in-dumps.md) live on `example-dumps`, and the
provenance table cited by [0004](0004-canonicalization-patterns-in-the-dialect.md)
and [0010](0010-vendor-upstream-into-reference.md) lives on `upstream-diff`. Each
record names the branch where its subject can be found.

`tests/check-docs.py` does not reach this directory. It globs `docs/*.md`, which is
not recursive, so the commands quoted here are not checked by the `docs` test the
way the numbered walkthroughs are. They were run while these records were written,
and they can be checked by naming the files:

    python3 tests/check-docs.py docs/adr/*.md
