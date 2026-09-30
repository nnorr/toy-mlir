# 0008. Equivalence against `toyc-ch7` is the acceptance gate

Status: accepted
Provenance: recorded in the implementing session

## Context

The rebuild changes the organization of the code while keeping the language
unchanged. That makes a strong claim available: our compiler should produce the
same output as the tutorial's compiler, at every stage, for every program.

The owner framed the whole rebuild that way, as an equivalence check against a
golden reference rather than as a test suite. That framing chose the gate.

## Options considered

| Option | Trade-off |
| --- | --- |
| lit tests only, as upstream ships | Cheap, and the expectations are readable. Only checks the programs someone thought to write, and checks them against expectations someone wrote by hand |
| Golden files of our own output | Catches regressions against ourselves. Proves self-consistency, and says nothing about matching upstream |
| Differential sweep over upstream's ~15 test programs | Real equivalence, in the direction that matters. Fifteen programs is a thin corpus for a claim that broad |
| That plus a generated corpus | Same claim over 1,500 programs. Requires writing a generator that emits only valid Toy, which is itself work that can be wrong |

## Decision

The differential sweep over both corpora is the gate. `tests/compat/gen-programs.py`
generates valid random programs, `compare-upstream.sh` runs both compilers over
every stage with and without `-opt`, and any difference not attributable to a
numbered deviation fails.

## Consequences

Three properties were treated as load-bearing rather than incidental.

The generator must produce only programs upstream accepts, so shapes are tracked
as the program is built rather than fixed up afterwards. A high rejection rate
would mean the generator is wrong, not that the language is strict, so the script
reports the rate and warns when it climbs.

Every normalization weakens the claim, so the set is small and named on screen:
the input path, and the compiler's own name where it prefixes a diagnostic, since
`argv[0]` is necessarily different for the two binaries. At the LLVM levels,
debug-metadata numbering is normalized and `column:` masked, but the instruction
stream must still match exactly. No value, type, symbol, SSA number or diagnostic
text is ever rewritten.

A gate that cannot fail proves nothing, so it was validated by mutation: a wrapper
that rewrites `toy.mul` to `toy.BOGUS` in our output must fail the sweep, and does.
`docs/13-testing-and-equivalence.md` gives the command that builds that mutant.

The cost is runtime and one more external dependency. The deep sweep is about 80
seconds, and the whole gate is skipped when `toyc-ch7` is absent, which means a
machine without the upstream build gets a weaker check and the CMake output says
so.

## Evidence

- The sweep's scope, streams and normalization policy, `tests/compat/EXPECTED-DIFFS.md`
  under "The sweep".
- Rejection-rate reporting and its warning, `tests/compat/gen-programs.py:371`
  and `:379`.
- The mutation check with its exact commands,
  `docs/13-testing-and-equivalence.md:253-265`.
- `ctest` entries `compat` and `compat_fixtures`, `tests/CMakeLists.txt:154`
  and `:170`, both guarded on the upstream binary being found.
