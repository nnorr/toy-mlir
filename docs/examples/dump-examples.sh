#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Regenerate the IR dumps under docs/examples/dumps/.
#
# The dumps exist so the abstraction levels can be read, and diffed, without
# building anything: one file per program per level, plus the diff that -opt
# makes at each level. They are generated rather than pasted, and the `dumps`
# ctest entry regenerates them into the build tree and compares, so a change in
# a pass shows up as a failing test instead of as documentation that quietly
# stopped being true.
#
# Levels stop at the LLVM dialect on purpose. -emit=llvm is past the translation
# out of MLIR, so it belongs to LLVM's own code generation rather than to this
# compiler's abstraction levels, and docs/09-lowering-to-llvm.md covers it.
#
# Every dump here is independent of the working directory and of the absolute
# path of the input: MLIR does not print locations unless asked, and the debug
# metadata that does embed a filename only appears after translation, at a level
# this script does not capture. That is what makes the committed files stable
# enough to diff in CI.
#
# Usage:
#   docs/examples/dump-examples.sh              regenerate in place
#   docs/examples/dump-examples.sh --check      regenerate elsewhere and compare
#   TOYC=<path> docs/examples/dump-examples.sh  use a toyc from another build
#===----------------------------------------------------------------------===#

set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
toyc=${TOYC:-$root/build/bin/toyc}
check=0
[[ ${1:-} == --check ]] && check=1

if [[ ! -x $toyc ]]; then
  echo "dump-examples: no toyc at $toyc (build it, or set TOYC)" >&2
  exit 2
fi

# Each program earns its place by showing something the others do not.
programs=(ex struct transpose)

# -opt changes nothing in the AST, so that level is captured once.
levels_both=(mlir mlir-affine mlir-llvm)

dumps=$root/docs/examples/dumps
out=$dumps
if (( check )); then
  out=$(mktemp -d)
  trap 'rm -rf "$out"' EXIT
fi
mkdir -p "$out"

# Relative paths, run from the repo root, so nothing absolute reaches a dump.
cd "$root"

for prog in "${programs[@]}"; do
  src=docs/examples/$prog.toy

  "$toyc" "$src" -emit=ast > "$out/$prog.ast.txt" 2>&1

  for level in "${levels_both[@]}"; do
    "$toyc" "$src" -emit="$level"      > "$out/$prog.$level.txt"     2>&1
    "$toyc" "$src" -emit="$level" -opt > "$out/$prog.$level.opt.txt" 2>&1

    # Labels rather than filenames: a unified diff header would otherwise carry
    # a temporary path and a timestamp, and the file would never compare equal.
    diff -u --label "$prog.$level.txt (no -opt)" \
            --label "$prog.$level.opt.txt (-opt)" \
            "$out/$prog.$level.txt" "$out/$prog.$level.opt.txt" \
      > "$out/$prog.$level.opt.diff" || true
  done
done

if (( check )); then
  if diff -r -q "$dumps" "$out" --exclude=README.md; then
    echo "dumps: up to date"
  else
    echo
    echo "dumps: out of date. Regenerate with:" >&2
    echo "    docs/examples/dump-examples.sh" >&2
    exit 1
  fi
else
  printf 'dumps: wrote %d files to docs/examples/dumps/\n' \
    "$(find "$out" -type f -not -name README.md | wc -l)"
fi
