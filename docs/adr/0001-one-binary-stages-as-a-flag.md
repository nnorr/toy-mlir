# 0001. One binary, with pipeline depth as a flag

Status: accepted
Provenance: recorded in the implementing session

## Context

The upstream tutorial ships seven executables, `toyc-ch1` through `toyc-ch7`,
each a copy of the previous chapter's source with one feature added. A rebuild
has to decide whether to keep that shape.

The cost of upstream's shape is visible in `reference/`: 21,637 lines of C++ and
ODS, in which `Lexer.h` has three distinct contents across seven copies. A change
to the lexer is a change to seven files.

## Options considered

| Option | Trade-off |
| --- | --- |
| One binary that unions the chapters | The code exists once. Loses the property that a reader can compile exactly what chapter 3 could do |
| `toyc-ch1..7` as separate targets, restructured into shared modules | Closest to reading the tutorial in order. Keeps most of the duplication, which is the thing worth removing |
| One binary plus `--stage=chN`, restricting the pipeline to a chapter's capability | Demonstrates chapter by chapter from one build. Adds a flag that has to be specified, maintained and tested, and that no user of the compiler wants |

## Decision

One binary. Pipeline depth became the `-emit` flag, which the tutorial already
had for its own stages, so no new concept was introduced.

## Consequences

`-emit` now carries two meanings at once: which artifact to print, and how far to
compile. `Stage` is an ordered enum and the pipeline is built by comparison
against it, so adding a level means adding an enum value in the right position
rather than editing a switch.

What was lost: there is no build that behaves like chapter 3. The mapping from
chapter to capability moved into a table in `ARCHITECTURE.md`, which is prose
rather than something the build enforces.

Reading the tutorial chapter by chapter now means reading `reference/Ch3` beside
this repo's `docs/03-parser.md`, not running a chapter 3 binary.

## Evidence

- Six `-emit` values, `src/main.cpp:105-113`.
- `Stage` as an ordered enum with seven values, `include/toy/Pipeline.h:34-42`.
- Comparisons rather than a switch: `src/main.cpp:292` (`stage < Stage::MLIRLLVM`),
  `:512` (`stage <= Stage::MLIRLLVM`).
- No `--stage` flag exists; `grep -n stage src/main.cpp` returns only the enum and
  its comparisons.
- The chapter-to-capability table is in `ARCHITECTURE.md`, section "What each
  chapter contributed".
