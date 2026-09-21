#!/usr/bin/env python3
"""Check that the repo's markdown still matches the code it documents.

Two checks, because documentation goes stale in two different ways.

The console check runs every command in a ```console block from the repo root
and compares the output with what the document claims. What it catches is text
that drifts silently: source locations that embed a file path, column numbers
that move when a line is added, diagnostics that change wording.

The quoted-source check looks at code blocks that quote this project's own C++
or TableGen, and verifies each distinctive line still exists in the source. What
it catches is an edit to a comment leaving a document quoting text that is
nowhere in the repo, which is exactly what happened when a prose pass rewrote
comments in Lexer.cpp, Parser.cpp and Ops.cpp that three documents quoted.

A block is only checked when the prose above it cites a file, so an illustrative
snippet, a quote of upstream under reference/, and generated code under build/
are left alone. Lines are matched individually rather than as a block, since an
excerpt legitimately skips lines, reindents, and stops mid-function.

Usage:
    tests/check-docs.py [options] [FILE ...]

With no FILE arguments it checks README.md, ARCHITECTURE.md and docs/*.md.

Options, each with an environment fallback:
    --root DIR             repo root                 TOY_ROOT
    --toyc PATH            the toyc under test       TOYC
    --upstream-toyc PATH   upstream toyc-ch7         TOYC_UPSTREAM
    --frontend-tests PATH  the unit-test binary      FRONTEND_TESTS
    -v                     also list matched commands

The defaults assume a build tree at <root>/build, so a different build directory
only needs --toyc. Documents spell the binary as `build/bin/toyc`; that literal
is rewritten to whatever --toyc gives, which is what makes the check work from
any build directory.

Not every quoted command can be verified, and a skipped command must never be
mistaken for a checked one: each skip is counted and reported with its reason,
the way tests/compat prints its NOT COMPARED row.

Exit status is 0 when nothing mismatched.
"""

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys

# Commands that cannot be verified, and why. Order matters: the first rule that
# matches wins, so the more specific reasons come first.
SKIP_RULES = [
    ("pager", lambda c: "| less" in c or c.endswith(" less")),
    ("placeholder, not a real invocation",
     lambda c: "<file>" in c or re.match(r"^toyc(-ch7)? ", c)),
    ("build step", lambda c: re.match(r"^(ninja|cmake|ctest|make) ", c)),
    ("long-running suite, has its own ctest entry",
     lambda c: "compare-upstream.sh" in c or c.startswith("time ")),
    ("scratch path outside the repo, not reproducible from a checkout",
     lambda c: "/tmp/" in c),
]

# Tools a command may invoke that are not part of this project. A command naming
# one that is absent is skipped rather than failed, so the suite still runs on a
# machine with only MLIR.
OPTIONAL_TOOLS = ["llvm-dwarfdump", "readelf", "llvm-readelf", "nm", "clang",
                  "gcc", "cc", "ldd", "objdump"]


def parse_blocks(text):
    """Yields (command, expected_lines) for each `$ ` line in a console block."""
    lines = text.split("\n")
    in_block = False
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("```"):
            in_block = line.startswith("```console")
            i += 1
            continue
        if in_block and line.startswith("$ "):
            command = line[2:]
            j = i + 1
            # A trailing backslash continues the command on the next line.
            while command.rstrip().endswith("\\") and j < len(lines):
                command = command.rstrip()[:-1] + lines[j].strip()
                j += 1
            expected = []
            while (j < len(lines) and not lines[j].startswith("$ ")
                   and not lines[j].startswith("```")):
                expected.append(lines[j])
                j += 1
            while expected and expected[-1] == "":
                expected.pop()
            yield command, expected
            i = j
            continue
        i += 1


def is_elision(line):
    """True for a line standing in for output the author left out.

    Either a bare `...` or a described gap such as `... six constants ...`.
    """
    stripped = line.strip()
    return stripped == "..." or (stripped.startswith("...")
                                 and stripped.endswith("...")
                                 and len(stripped) > 3)


def line_matches(expected, actual):
    """Compares one line, treating `...` inside it as a wildcard.

    Documents abbreviate long lines, as in `dense<[[1.000000e+00, ...]]>` or
    `inline{...}`. Everything the author did write still has to match exactly,
    which is what keeps a moved column or a changed path detectable.
    """
    if expected == actual:
        return True
    if "..." in expected:
        pattern = ".*".join(re.escape(part) for part in expected.split("..."))
        if re.fullmatch(pattern, actual) is not None:
            return True

    # Listings are sometimes annotated, as docs/08 does with
    # `affine.for %arg0 = 0 to 3 {        // the transpose`. Dropping a comment
    # that the author aligned to the right is the last thing tried, and only
    # helps when everything to its left already matches exactly, so an
    # annotation cannot hide a changed line.
    stripped = re.match(r"^(.*?\S)\s{2,}//.*$", expected)
    if stripped:
        return line_matches(stripped.group(1), actual)
    return False


def outputs_match(expected, actual):
    """True when the quoted output appears in what the command printed.

    Quoted blocks are often excerpts: they may start part way into the output
    and may skip a run of lines with an elision. So each run of consecutive
    quoted lines has to appear contiguously and in order somewhere in the
    actual output, with elisions allowed between runs.
    """
    segments = [[]]
    for line in expected:
        if is_elision(line):
            segments.append([])
        else:
            segments[-1].append(line)

    pos = 0
    for segment in segments:
        if not segment:
            continue
        found = -1
        for start in range(pos, len(actual) - len(segment) + 1):
            if all(line_matches(segment[i], actual[start + i])
                   for i in range(len(segment))):
                found = start
                break
        if found < 0 and len(segment) > 1:
            # A single very long output line is sometimes wrapped across
            # several indented lines to stay readable, as docs/11 does with
            # --print-pipeline. Joining the segment back up is the only way to
            # compare it, since the wrap points exist only in the document.
            joined = "".join(line.strip() for line in segment)
            for start in range(pos, len(actual)):
                if line_matches(joined, actual[start]):
                    found = start
                    pos = start + 1
                    break
            if found >= 0:
                continue
        if found < 0:
            return False
        pos = found + len(segment)
    return True


# Where a code block's citation has to point for the block to be treated as a
# quote of this project's own source.
PROJECT_SOURCE_DIRS = ["src", "include", "tests"]

# Languages whose blocks may quote project source. An `mlir` or `plaintext`
# block is IR or prose, not source, and is left alone.
SOURCE_LANGUAGES = ["c++", "cpp", "cc", "tablegen", "td", ""]

CITATION = re.compile(
    r"\b((?:src|include|tests|test|reference|build|docs)/[\w./+-]+"
    r"\.(?:cpp|h|td|inc|py|sh|cmake))(?::(\d+))?")


def parse_code_blocks(text):
    """Yields (language, first_line_number, lines, citation) per fenced block.

    The citation is the last source path mentioned in the few lines above the
    fence, which is how the documents introduce a quote:

        `toy.constant` shows it (`include/toy/Ops.td:120`):

    A block with no such introduction is not treated as a quote at all.
    """
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        if lines[i].startswith("```"):
            language = lines[i][3:].strip().lower()
            body = []
            start = i + 1
            j = start
            while j < len(lines) and not lines[j].startswith("```"):
                body.append(lines[j])
                j += 1
            # Look back over the sentences introducing the block for a source
            # path, stopping at the previous fence so a citation belonging to an
            # earlier block is never borrowed by this one.
            citation = None
            examined = 0
            for back in range(i - 1, -1, -1):
                if lines[back].startswith("```"):
                    break
                if not lines[back].strip():
                    continue
                found = CITATION.findall(lines[back])
                if found:
                    citation = found[-1][0]
                    break
                examined += 1
                if examined >= 4:
                    break
            yield language, start + 1, body, citation
            i = j + 1
            continue
        i += 1


def is_anchor(line):
    """True for a quoted line distinctive enough to look for in the source.

    Comments and whole statements are the useful anchors. Short or punctuation
    only lines appear everywhere, and a line carrying an elision was edited by
    the author, so neither can be attributed to one place in the source.
    """
    stripped = line.strip()
    if len(stripped) < 12 or "..." in stripped:
        return False
    if stripped.startswith(("//", "///", "#")):
        return True
    return stripped.endswith((";", "{"))


def collect_source_lines(root):
    """Every stripped line of this project's own C++ and TableGen."""
    seen = {}
    for directory in PROJECT_SOURCE_DIRS:
        base = root / directory
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if path.suffix not in (".cpp", ".h", ".td"):
                continue
            for number, line in enumerate(path.read_text().split("\n"), 1):
                seen.setdefault(line.strip(), (path.relative_to(root), number))
    return seen


def check_quoted_source(files, root):
    """Checks that quoted project source still exists in the source.

    The failure this catches: an edit to a comment leaves a document quoting
    text that is nowhere in the repo. Blocks are matched line by line rather
    than as a whole, because an excerpt legitimately skips lines, reindents and
    stops mid-function.
    """
    source_lines = collect_source_lines(root)
    blocks = anchors = missing = 0
    skips = {}
    failures = []

    for path in files:
        rel = path.relative_to(root) if path.is_absolute() else path
        text = path.read_text()
        for language, first_line, body, citation in parse_code_blocks(text):
            if language == "console":
                continue
            if language not in SOURCE_LANGUAGES:
                skips.setdefault(f"not source ({language or 'no language'})",
                                 []).append(rel)
                continue
            if not citation:
                skips.setdefault("no file cited above the block",
                                 []).append(rel)
                continue
            if citation.startswith("reference/"):
                skips.setdefault("quotes upstream under reference/",
                                 []).append(rel)
                continue
            if citation.startswith("build/"):
                skips.setdefault("quotes generated code under build/",
                                 []).append(rel)
                continue
            if not citation.split(":")[0].endswith((".cpp", ".h", ".td")):
                skips.setdefault("cited file is not C++ or TableGen",
                                 []).append(rel)
                continue

            blocks += 1
            for offset, line in enumerate(body):
                if not is_anchor(line):
                    continue
                anchors += 1
                if line.strip() not in source_lines:
                    missing += 1
                    failures.append((rel, first_line + offset, citation,
                                     line.strip()))

    return blocks, anchors, missing, skips, failures


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("files", nargs="*")
    ap.add_argument("--root", default=os.environ.get("TOY_ROOT"))
    ap.add_argument("--toyc", default=os.environ.get("TOYC"))
    ap.add_argument("--upstream-toyc", default=os.environ.get("TOYC_UPSTREAM"))
    ap.add_argument("--frontend-tests", default=os.environ.get("FRONTEND_TESTS"))
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    root = pathlib.Path(args.root) if args.root else \
        pathlib.Path(__file__).resolve().parent.parent
    toyc = pathlib.Path(args.toyc) if args.toyc else root / "build/bin/toyc"
    frontend_tests = pathlib.Path(args.frontend_tests) if args.frontend_tests \
        else root / "build/tests/FrontendTests"

    # The documents name upstream by an absolute path, since it lives in the
    # LLVM build tree rather than in this project.
    doc_upstream = "~/dev/08_mlir_toy/build/bin/toyc-ch7"
    upstream = args.upstream_toyc or os.path.expanduser(doc_upstream)

    if not toyc.exists():
        print(f"SKIPPED: toyc not found at {toyc}")
        print("Build it first, or pass --toyc. Nothing was checked.")
        return 0

    if args.files:
        files = [pathlib.Path(f) for f in args.files]
    else:
        files = [root / "README.md", root / "ARCHITECTURE.md"]
        files += sorted((root / "docs").glob("*.md"))
    files = [f for f in files if f.exists()]

    checked = matched = mismatched = ran_only = 0
    skips = {}
    failures = []

    for path in files:
        rel = path.relative_to(root) if path.is_absolute() else path
        last_rc = None
        for command, expected in parse_blocks(path.read_text()):
            # `echo $?` reports the previous command's status, so it is checked
            # against that rather than run on its own.
            if command.strip() == "echo $?":
                if last_rc is None:
                    skips.setdefault("exit code with no preceding command",
                                     []).append((rel, command))
                    continue
                checked += 1
                if expected == [str(last_rc)]:
                    matched += 1
                    if args.verbose:
                        print(f"  ok   {rel}: $ {command}")
                else:
                    mismatched += 1
                    failures.append((rel, command, expected, [str(last_rc)]))
                continue

            reason = next((r for r, test in SKIP_RULES if test(command)), None)

            if reason is None and doc_upstream in command \
                    and not pathlib.Path(upstream).exists():
                reason = "upstream toyc-ch7 not available"
            if reason is None and "build/tests/FrontendTests" in command \
                    and not frontend_tests.exists():
                reason = "FrontendTests not built"
            if reason is None:
                tool = next((t for t in OPTIONAL_TOOLS
                             if re.search(rf"(^|[|&;] *){t} ", command)
                             and not shutil.which(t)), None)
                if tool:
                    reason = f"tool not installed: {tool}"

            if reason:
                skips.setdefault(reason, []).append((rel, command))
                continue

            # Upstream first: its path ends in `build/bin/toyc-ch7`, so
            # rewriting `build/bin/toyc` before it would corrupt the longer
            # path. The negative lookahead keeps any later `-ch7` safe too.
            runnable = command.replace(doc_upstream, str(upstream))
            runnable = re.sub(r"(?<![\w./-])build/bin/toyc(?!-)", str(toyc),
                              runnable)
            runnable = runnable.replace("build/tests/FrontendTests",
                                        str(frontend_tests))

            # One stream, so the two are interleaved the way a terminal shows
            # them. Capturing them separately reorders a diagnostic written to
            # stderr against anything the same command printed to stdout.
            proc = subprocess.run(["bash", "-c", runnable], cwd=root,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True)
            # A process killed by a signal comes back as -N here, where a shell
            # reports 128+N. The documents quote what a shell prints.
            last_rc = proc.returncode
            if last_rc < 0:
                last_rc = 128 - last_rc
            actual = proc.stdout.split("\n")
            while actual and actual[-1] == "":
                actual.pop()

            checked += 1
            if not expected:
                # No output is quoted, either because the document discusses it
                # instead of pasting it, or because the command genuinely prints
                # nothing and the point is made by the `echo $?` that follows.
                # Those cannot be told apart from the markdown, so the only
                # check is that the command ran at all: 127 means the binary is
                # not where the document says it is.
                if last_rc == 127:
                    mismatched += 1
                    failures.append((rel, command, ["(a command that exists)"],
                                     [f"(exit status 127)"] + actual[:4]))
                else:
                    ran_only += 1
                    matched += 1
                continue

            if outputs_match(expected, actual):
                matched += 1
                if args.verbose:
                    print(f"  ok   {rel}: $ {command}")
            else:
                mismatched += 1
                failures.append((rel, command, expected, actual))

    for rel, command, expected, actual in failures:
        print(f"\nMISMATCH {rel}")
        print(f"  $ {command}")
        print("  documented:")
        for line in expected[:15]:
            print(f"    |{line}")
        print("  actual:")
        for line in actual[:15]:
            print(f"    |{line}")

    # Second check: quoted source, which the console check says nothing about.
    blocks, anchors, missing, src_skips, src_failures = \
        check_quoted_source(files, root)

    for rel, line_number, citation, text in src_failures:
        print(f"\nQUOTED SOURCE NOT FOUND {rel}:{line_number}")
        print(f"  block cites {citation}")
        print(f"  quoted: {text}")
        print("  this text is in no .cpp, .h or .td file under "
              f"{', '.join(PROJECT_SOURCE_DIRS)}/")

    total_skipped = sum(len(v) for v in skips.values())
    print("\n" + "=" * 70)
    print(f"files={len(files)} checked={checked} matched={matched} "
          f"mismatched={mismatched} SKIPPED={total_skipped}")
    print(f"quoted source: blocks={blocks} lines={anchors} "
          f"NOT_FOUND={missing} SKIPPED={sum(len(v) for v in src_skips.values())}")
    if ran_only:
        print(f"of those matched, {ran_only} ran without error but had no "
              f"quoted output to compare")
    if skips:
        print("\nskipped, by reason:")
        for reason in sorted(skips, key=lambda r: -len(skips[r])):
            entries = skips[reason]
            print(f"  {len(entries):3d}  {reason}")
            seen = []
            for rel, _ in entries:
                if rel not in seen:
                    seen.append(rel)
            print(f"       in {', '.join(str(s) for s in seen)}")
    if src_skips:
        print("\nquoted-source blocks skipped, by reason:")
        for reason in sorted(src_skips, key=lambda r: -len(src_skips[r])):
            entries = src_skips[reason]
            seen = []
            for rel in entries:
                if rel not in seen:
                    seen.append(rel)
            print(f"  {len(entries):3d}  {reason}")
            print(f"       in {', '.join(str(s) for s in seen)}")
    print("=" * 70)
    return 1 if (mismatched or missing) else 0


if __name__ == "__main__":
    sys.exit(main())
