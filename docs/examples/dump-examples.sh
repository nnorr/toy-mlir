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
# Beyond the levels, the primary program also gets the views that explain how the
# pipeline works rather than what it produced: the generic form beside the pretty
# one, locations, the pass pipeline as a string, the IR after every pass, pass
# statistics, and the symbols of a real object file.
#
# Stability is the constraint on everything here, since every file is compared
# byte for byte. Dumps are produced from the repo root with relative input paths,
# so no absolute path reaches a dump; nothing timed is captured, which is why
# `ctest` output appears nowhere; and the object file itself is thrown away
# because only its symbol table is worth reading. The `llvm` and `mlir-llvm`
# levels embed LLVM's own spelling and its debug metadata numbering: stable on
# one machine, and expected to move when LLVM is upgraded.
#
# Usage:
#   docs/examples/dump-examples.sh              regenerate in place
#   docs/examples/dump-examples.sh --check      regenerate elsewhere and compare
#   TOYC=<path> docs/examples/dump-examples.sh  use a toyc from another build
#===----------------------------------------------------------------------===#

set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
toyc=${TOYC:-$root/build/bin/toyc}
# Only needed for the optional scf/cf section, which is skipped without it.
mlir_opt=${MLIR_OPT:-$HOME/dev/08_mlir_toy/build/bin/mlir-opt}
check=0
[[ ${1:-} == --check ]] && check=1

if [[ ! -x $toyc ]]; then
  echo "dump-examples: no toyc at $toyc (build it, or set TOYC)" >&2
  exit 2
fi

# Each program earns its place by showing something the others do not. `codegen`
# is upstream's own running example and is read from reference/ rather than
# copied, so its source locations match the tutorial's exactly.
programs=(codegen ex struct transpose trivial_reshape)
src_codegen=reference/tests/Ch2/codegen.toy
src_ex=docs/examples/ex.toy
src_struct=docs/examples/struct.toy
src_transpose=docs/examples/transpose.toy
src_trivial_reshape=reference/tests/Ch3/trivial_reshape.toy

# The program the documents cite when they need one.
primary=codegen

# -opt changes nothing in the AST, so that level is captured once.
levels_both=(mlir mlir-affine mlir-llvm llvm)

# A dump is named for the level it holds, and its extension says what format that
# is, so an editor and a file browser both know what they are looking at: .mlir
# for MLIR, .ll for LLVM IR, .txt only for output that is neither. The -emit flag
# that produces each one is in dumps/README.md, since the flag names would give
# files like `codegen.mlir.mlir` and `codegen.mlir-llvm.txt`, both misleading.
level_stem() {
  case $1 in
    mlir)        echo toy-dialect ;;
    mlir-affine) echo affine ;;
    mlir-llvm)   echo llvm-dialect ;;
    llvm)        echo llvm-ir ;;
    *)           echo "$1" ;;
  esac
}
level_ext() {
  case $1 in
    llvm) echo ll ;;   # LLVM IR, not MLIR
    *)    echo mlir ;;
  esac
}

dumps=$root/docs/examples/dumps
out=$dumps
scratch=$root/build/dump-scratch
if (( check )); then
  out=$(mktemp -d)
  trap 'rm -rf "$out" "$scratch"' EXIT
else
  trap 'rm -rf "$scratch"' EXIT
fi
mkdir -p "$out" "$scratch"

# Relative paths, run from the repo root, so nothing absolute reaches a dump.
cd "$root"

# A unified diff header would otherwise carry a temporary path and a timestamp,
# so both sides are labelled and the file compares equal on every run.
emit_diff() { # left right label_left label_right target
  diff -u --label "$3" --label "$4" "$1" "$2" > "$5" || true
}

for prog in "${programs[@]}"; do
  eval "src=\$src_$prog"

  "$toyc" "$src" -emit=ast > "$out/$prog.ast.txt" 2>&1

  for level in "${levels_both[@]}"; do
    stem=$prog.$(level_stem "$level")
    ext=$(level_ext "$level")
    "$toyc" "$src" -emit="$level"      > "$out/$stem.$ext"     2>&1
    "$toyc" "$src" -emit="$level" -opt > "$out/$stem.opt.$ext" 2>&1
    emit_diff "$out/$stem.$ext" "$out/$stem.opt.$ext" \
              "$stem.$ext (no -opt)" "$stem.opt.$ext (-opt)" \
              "$out/$stem.opt.diff"
  done

  # What the program prints when it is actually run, which is the only dump here
  # that is an answer rather than a representation.
  "$toyc" "$src" -emit=jit -opt > "$out/$prog.jit.txt" 2>&1
done

#===----------------------------------------------------------------------===#
# Views of the primary program
#===----------------------------------------------------------------------===#

eval "src=\$src_$primary"

# The pretty format is sugar that ODS defines per operation; the generic form is
# the structure every MLIR tool actually sees. The diff is the argument.
"$toyc" "$src" -emit=mlir -opt --mlir-print-op-generic \
  > "$out/$primary.generic.mlir" 2>&1
emit_diff "$out/$primary.toy-dialect.opt.mlir" "$out/$primary.generic.mlir" \
          "$primary.toy-dialect.opt.mlir (custom format)" \
          "$primary.generic.mlir (generic format)" \
          "$out/$primary.generic.diff"

# Locations ride on every operation from MLIRGen onward. They are not printed
# unless asked, which is why every other dump here is path-independent.
"$toyc" "$src" -emit=mlir --mlir-print-debuginfo \
  > "$out/$primary.locations.mlir" 2>&1

# The same driver builds three different pipelines depending on how far it is
# asked to go. Printed as a string, without compiling anything.
for stage in mlir mlir-affine mlir-llvm; do
  "$toyc" "$src" -emit="$stage" -opt --print-pipeline \
    > "$out/$primary.pipeline.$stage.txt" 2>&1
done

# The most useful dump in the set: the IR after every pass, kept whole. Read it
# with docs/examples/dumps/README.md, which says what each banner changed.
"$toyc" "$src" -emit=mlir-affine -opt --mlir-print-ir-after-all \
  > "$out/$primary.after-each-pass.mlir" 2>&1

# Counters only, no timings, so this one is safe to commit.
"$toyc" "$src" -emit=mlir-affine -opt --mlir-pass-statistics \
  > "$out/$primary.pass-statistics.txt" 2>&1

# The object file proves the lowering reaches a linkable artifact; its symbol
# table is the part worth reading, so the object itself is discarded.
"$toyc" "$src" -c -o "$scratch/$primary.o" > "$scratch/c.log" 2>&1
nm -g "$scratch/$primary.o" > "$out/$primary.object-symbols.txt"

#===----------------------------------------------------------------------===#
# Optional: the scf and cf steps that the full conversion hides
#===----------------------------------------------------------------------===#
# toyc goes from affine to the LLVM dialect in one pass, but the conversion runs
# through scf and cf on the way. Driving mlir-opt by hand is the only way to see
# those intermediate forms. Needs the MLIR tools, so it is skipped when they are
# absent, the way the differential sweep skips without toyc-ch7.

scf_files=("$primary.affine-generic.mlir" "$primary.scf-from-affine.mlir"
           "$primary.cf-from-scf.mlir")
skipped_scf=0
if [[ -x $mlir_opt ]]; then
  # Generic form, because mlir-opt has never heard of the toy dialect and parses
  # toy.print only as an unregistered operation.
  "$toyc" "$src" -emit=mlir-affine -opt --mlir-print-op-generic \
    > "$out/$primary.affine-generic.mlir" 2>&1
  "$mlir_opt" --allow-unregistered-dialect --lower-affine \
    "$out/$primary.affine-generic.mlir" \
    > "$out/$primary.scf-from-affine.mlir" 2>&1
  "$mlir_opt" --allow-unregistered-dialect --convert-scf-to-cf \
    "$out/$primary.scf-from-affine.mlir" \
    > "$out/$primary.cf-from-scf.mlir" 2>&1
else
  skipped_scf=1
fi

#===----------------------------------------------------------------------===#

if (( check )); then
  excludes=(--exclude=README.md)
  if (( skipped_scf )); then
    for f in "${scf_files[@]}"; do excludes+=("--exclude=$f"); done
  fi
  if diff -r -q "${excludes[@]}" "$dumps" "$out"; then
    if (( skipped_scf )); then
      echo "dumps: up to date (scf/cf section not compared: no mlir-opt)"
    else
      echo "dumps: up to date"
    fi
  else
    echo
    echo "dumps: out of date. Regenerate with:" >&2
    echo "    docs/examples/dump-examples.sh" >&2
    exit 1
  fi
else
  printf 'dumps: wrote %d files to docs/examples/dumps/ (%s)\n' \
    "$(find "$out" -type f -not -name README.md | wc -l)" \
    "$(du -sh "$out" | cut -f1)"
  # An `if` rather than `&&`: as the script's last statement, a false (( )) would
  # become the exit status and fail the ctest entry on a successful run.
  if (( skipped_scf )); then
    echo "dumps: skipped the scf/cf section, no mlir-opt at $mlir_opt"
  fi
fi
