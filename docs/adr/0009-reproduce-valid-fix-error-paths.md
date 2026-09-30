# 0009. Reproduce upstream where a valid program can see it, fix the error paths

Status: accepted
Provenance: recorded in the implementing session for the policy and D1 to D8;
reconstructed from commits for D9 to D13

## Context

Once equivalence is the gate ([0008](0008-differential-equivalence-as-the-gate.md)),
every difference from upstream is either a failure or a decision. Upstream also
has real defects: a malformed number is silently truncated, a one-token typo
aborts the compiler through an assertion, a module with no `main` core-dumps on an
`llvm::Error` it checked but never consumed.

So the two goals pull against each other. Matching upstream byte for byte means
inheriting its bugs. Fixing its bugs means breaking the gate.

## Options considered

| Option | Trade-off |
| --- | --- |
| Match upstream exactly, bugs included | The gate is absolute and every difference is a failure. Ships a compiler that crashes on a missing `=` |
| Fix whatever looks wrong | A better compiler. No gate left: every fix is an unexplained difference, and real regressions hide among them |
| Split by reachability: reproduce what a valid program can observe, fix the error paths, and number every difference | Keeps the strong claim where it can be checked over a large corpus, and still fixes the crashes. Requires the deviation list to be maintained by hand and honestly |

## Decision

The third. The rule is that a valid program sees byte-identical output, error
paths may differ, and every difference carries a number, a reason and a fixture.
The sweep fails on anything unlisted.

One upstream defect is kept deliberately: its own diagnostics read
`error: error: unknown variable 'x'`, because the message text repeats the prefix
`emitError` already prints. Those strings are compared byte for byte, so the
ported messages keep the doubling. New messages of ours do not, and the code says
why at the one place where the two conventions meet.

## Consequences

The deviation list is the repo's memory. `ARCHITECTURE.md` holds the table, and
`tests/compat/EXPECTED-DIFFS.md` records how each one is admitted by the sweep and
what measurement supports it.

Fixtures carry what the sweep cannot: an error path a valid program never reaches
is checked against a recorded diff, so a change in our own diagnostics fails a test
instead of being accepted silently.

A cautionary case worth recording. During the work I relayed a "fix" for
comments-only input, claiming upstream reports a parse error there. The implementer
refused it with evidence: that is `toyc-ch1`'s behavior, and the target is
`toyc-ch7`, whose own test asserts `CHECK-NOT: Parse error`. Applying it would have
broken the gate and upstream's test at once. The lesson is that "upstream does X"
has to name which upstream binary, because the seven differ.

## Provenance split

D1 through D8, and the policy above, were decided in the implementing session.

D9 through D13 came out of a later code review, in commits `cb230e7` and `ceef9c7`,
whose deliberation is not available to this record. What the code shows: verifiers
were added for a reshape that changes the element count, for element-wise operands
of differing shapes and for a call with the wrong argument count; a byte at or
above `0x80` is no longer read as signed, so a `0xFF` no longer ends the file; a
one-element constant reshaped larger is broadcast rather than aborting inside
`DenseElementsAttr::reshape`; a failed `print` fails the compile; and calling a
function that returns nothing is a diagnostic rather than an out-of-bounds read.
Those are described as observable behavior, not as choices, because the reasoning
behind them is not recorded here.

The commit message for `cb230e7` says seven bugs and its diff contains eight
distinct fixes. The provenance table on the `upstream-diff` branch notes the
discrepancy rather than inventing a grouping that makes it seven.

## Evidence

- The complete deviation table, `ARCHITECTURE.md`, section "Deviations from
  upstream". Not duplicated here.
- How each is admitted, and the measured sweep result,
  `tests/compat/EXPECTED-DIFFS.md`.
- The kept doubling, and the reason the new message does not copy it,
  `src/MLIRGen.cpp:160-165`.
- Fixtures and their recorded diffs, `tests/compat/fixtures/` and
  `tests/compat/expected/`, checked by the `compat_fixtures` entry at
  `tests/CMakeLists.txt:170`.
