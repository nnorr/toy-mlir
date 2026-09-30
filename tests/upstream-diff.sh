#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Provenance map: how far each file has moved from the upstream tutorial.
#
# reference/Ch7 holds upstream verbatim, so the distance can be measured rather
# than asserted. Several of our files were split out of one of theirs, so a row
# is a group on both sides rather than a file pair, and every file belongs to
# exactly one row.
#
# The count is code lines that differ. Both sides are normalized first: whole
# comment lines and blank lines dropped, runs of whitespace collapsed, lines
# sorted. Sorting is deliberate. Moving a function is not a change in logic, and
# most of this repo's differences from upstream are exactly that kind of move.
# A comment appended to the end of a line of code is not stripped, so it does
# count.
#
# docs/14-upstream-diff.md quotes this output, and tests/check-docs.py runs the
# script and diffs it, so the numbers in that document cannot go stale.
#
# Usage: tests/upstream-diff.sh
#===----------------------------------------------------------------------===#

set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

if [[ ! -d reference/Ch7 ]]; then
  echo "upstream-diff: reference/Ch7 is missing" >&2
  exit 2
fi

# Whole-line comments and blank lines out, whitespace collapsed, sorted.
normalize() {
  cat "$@" | grep -vE '^[[:space:]]*(//|$)' |
    sed 's/[[:space:]]\+/ /g; s/^ //; s/ $//' | sort
}

count_lines() { cat "$@" | wc -l; }

# Every file on both sides belongs to exactly one row, so nothing is counted
# twice and the rows add up to the whole tree. Upstream paths drop their
# directory, which is unambiguous here: no two files under reference/Ch7 share a
# name.
#
# row <ours label> <upstream label> <our files> <upstream files>
row() {
  local ourLabel=$1 upLabel=$2 ours=$3 up=$4
  local -a ourFiles upFiles
  IFS=, read -r -a ourFiles <<< "$ours"
  IFS=, read -r -a upFiles <<< "$up"

  # A row with no upstream side was written here; there is nothing to compare.
  if [[ -z $up ]]; then
    printf '%-21s %-26s %5s %5s %6s\n' "$ourLabel" "$upLabel" \
      "$(count_lines "${ourFiles[@]}")" - -
    return
  fi

  local differ
  differ=$(diff <(normalize "${ourFiles[@]}") <(normalize "${upFiles[@]}") |
             grep -c '^[<>]' || true)

  printf '%-21s %-26s %5s %5s %6s\n' "$ourLabel" "$upLabel" \
    "$(count_lines "${ourFiles[@]}")" "$(count_lines "${upFiles[@]}")" "$differ"
}

R=reference/Ch7

printf '%-21s %-26s %5s %5s %6s\n' OURS UPSTREAM OURS UP DIFFER
printf '%.0s-' {1..67}; echo

row 'Lexer.{h,cpp}' 'Lexer.h' \
    include/toy/Lexer.h,src/Lexer.cpp $R/include/toy/Lexer.h
row 'AST.h' 'AST.h' \
    include/toy/AST.h $R/include/toy/AST.h
row 'ASTVisitor.h' '(no Toy counterpart)' \
    include/toy/ASTVisitor.h ''
row 'Parser.{h,cpp}' 'Parser.h' \
    include/toy/Parser.h,src/Parser.cpp $R/include/toy/Parser.h
row 'ASTDumper.{h,cpp}' 'AST.cpp' \
    include/toy/ASTDumper.h,src/ASTDumper.cpp $R/parser/AST.cpp
row 'MLIRGen.{h,cpp}' 'MLIRGen.{h,cpp}' \
    include/toy/MLIRGen.h,src/MLIRGen.cpp \
    $R/include/toy/MLIRGen.h,$R/mlir/MLIRGen.cpp
row 'Ops.td' 'Ops.td' \
    include/toy/Ops.td $R/include/toy/Ops.td
row 'Dialect.h' 'Dialect.h' \
    include/toy/Dialect.h $R/include/toy/Dialect.h
row 'dialect/*.cpp (6)' 'Dialect.cpp+ToyCombine.cpp' \
    src/dialect/ToyDialect.cpp,src/dialect/StructType.cpp,src/dialect/Ops.cpp,src/dialect/Folders.cpp,src/dialect/Interfaces.cpp,src/dialect/ToyCombine.cpp \
    $R/mlir/Dialect.cpp,$R/mlir/ToyCombine.cpp
row 'ToyCombine.td' 'ToyCombine.td' \
    src/dialect/ToyCombine.td $R/mlir/ToyCombine.td
row 'ShapeInference (3)' 'ShapeInference{Iface,Pass}' \
    include/toy/ShapeInferenceInterface.h,include/toy/ShapeInferenceInterface.td,src/passes/ShapeInference.cpp \
    $R/include/toy/ShapeInferenceInterface.h,$R/include/toy/ShapeInferenceInterface.td,$R/mlir/ShapeInferencePass.cpp
row 'Passes.h' 'Passes.h' \
    include/toy/Passes.h $R/include/toy/Passes.h
row 'LowerToAffine.cpp' 'LowerToAffineLoops.cpp' \
    src/passes/LowerToAffine.cpp $R/mlir/LowerToAffineLoops.cpp
row 'LowerToLLVM.cpp' 'LowerToLLVM.cpp' \
    src/passes/LowerToLLVM.cpp $R/mlir/LowerToLLVM.cpp
row 'driver (7)' 'toyc.cpp' \
    src/main.cpp,include/toy/Pipeline.h,src/Pipeline.cpp,include/toy/Translate.h,src/Translate.cpp,include/toy/Jit.h,src/Jit.cpp \
    $R/toyc.cpp
row 'ObjectEmitter.{h,cpp}' '(no Toy counterpart)' \
    include/toy/ObjectEmitter.h,src/ObjectEmitter.cpp ''
row 'DebugInfo.{h,cpp}' '(no Toy counterpart)' \
    include/toy/DebugInfo.h,src/DebugInfo.cpp ''

