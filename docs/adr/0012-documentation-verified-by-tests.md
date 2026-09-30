# 0012. Documentation is verified by tests, not by review

Status: accepted
Provenance: recorded in the implementing session

## Context

The documentation quotes the compiler heavily: commands with their real output,
excerpts of source, measured counts. All of that goes stale silently. Two
instances happened during the work rather than in theory.

A prose pass rewrote comments in `Lexer.cpp`, `Parser.cpp` and `Ops.cpp`. Three
documents quoted those comments, and began quoting text that existed nowhere in
the repository. Nothing noticed.

Separately, the suite count in the README and in `docs/13` sat stale by two
suites, because the `ctest` line that states it is classified as a build step and
skipped.

## Options considered

| Option | Trade-off |
| --- | --- |
| Review the docs when the code changes | No machinery. Depends on someone remembering, which is exactly what failed twice |
| Verify commands by executing them and diffing | Catches drifting output, locations and column numbers. Cannot see a quote of source that is not a command |
| Verify commands, and separately verify that quoted source still exists | Catches both failure modes seen. Two mechanisms to maintain, and a code change can now fail a documentation test |

## Decision

Both checks, in `tests/check-docs.py`, wired to the `docs` ctest entry. The console
check runs every command in a `console` block from the repo root and diffs the
output. The quoted-source check takes code blocks whose prose cites a file and
confirms the distinctive lines still appear in that file.

## Consequences

Editing a comment that a document quotes now fails a test, and so does changing a
diagnostic, a column number or a pass name that appears in quoted output. That is
the intent: the alternative is documentation that is wrong without anyone learning
it.

Skips are counted and reported with reasons rather than passing quietly, on the
same principle the equivalence sweep follows with its `NOT COMPARED` row. Three
categories cannot be verified: build steps, pagers, and blocks quoting generated
code under `build/` ([0011](0011-generated-inc-not-committed.md)).

Two numbers remain unverified as a direct consequence, the test count and the
timing in the `ctest` line that the build-step rule skips. `docs/13` says so in the
document itself rather than leaving a reader to assume everything there is checked.

The check was validated against deliberate breakage, both for commands and for
quoted source: mutating a quoted comment in each of the three affected files makes
it exit 1 and name the document and line.

## Consequences on other branches

The same discipline covers the IR dumps, through a `dumps` entry that regenerates
them and compares. That work is on the `example-dumps` branch and is not present
here, so on this branch `ctest` has eight entries rather than nine and
`docs/examples/dumps/` does not exist. See
[0013](0013-exclude-llvm-levels-from-dumps.md) and
[0014](0014-reinclude-llvm-levels-in-dumps.md).

## Evidence

- Both checks and the reason each exists, `tests/check-docs.py:1-20`.
- The `docs` ctest entry, `tests/CMakeLists.txt:131`, with the comment explaining
  the two passes at `:121-129`.
- The directories a quote may be attributed to, `tests/check-docs.py:187`.
- The unverified-number admission is in `docs/13-testing-and-equivalence.md`.
