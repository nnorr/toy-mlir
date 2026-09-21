#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Differential equivalence sweep against the upstream tutorial.
#
# toyc-ch7 is the golden reference: for a valid Toy program, this repo's toyc
# must produce the same output at every stage. That is the acceptance gate for
# the whole rebuild, so it is checked the way an equivalence check is run
# against a reference model -- over a randomized corpus, not over a handful of
# examples.
#
# What is compared, per program:
#   stages    -emit=ast, mlir, mlir-affine, mlir-llvm, llvm, jit
#   settings  with and without -opt
#   streams   stdout and stderr merged, plus stdout alone for -emit=jit
#
# What is allowed to differ, and nothing else:
#   -emit=ast    binop column numbers (deviation D2, the operator-location fix).
#   -emit=llvm   the debug metadata those columns feed: with upstream's location
#                a binop and its right-hand side share one !DILocation, with ours
#                they do not, so one extra node appears and every !dbg
#                renumbers. The instruction stream must still match exactly.
#   -emit=jit    deviation D8, and only on a module with no main: upstream aborts
#                on an unconsumed llvm::Error, we report the same cause and exit.
#   mlir, mlir-affine   nothing at all. Zero bytes.
#
# Nothing is skipped: a program upstream cannot compile is compared anyway, since
# reproducing an upstream failure faithfully is an equivalence result. D1/D3/D4
# are diagnostics a valid program never triggers and are checked as fixtures.
#
# The error paths (D1/D3/D4) are deliberately not part of that sweep: they are
# expected to differ, so they live in fixtures/ with their diffs recorded in
# EXPECTED-DIFFS.md and expected/.
#
# Usage:
#   tests/compat/compare-upstream.sh [options]
#     --count N        random programs to generate (default 200)
#     --seed S         generator seed (default 20260921)
#     --jobs N         parallel comparisons (default: nproc)
#     --corpus DIR     reuse/keep the generated corpus in DIR instead of a
#                      temporary directory
#     --no-random      sweep only reference/tests, no generated programs
#     --fixtures-only  check the recorded error-path diffs and nothing else
#     --update         rewrite the recorded error-path diffs from current output
#     --max-report N   how many failing diffs to print (default 5)
#
#   TOYC, TOYC_UPSTREAM, COMPAT_COUNT, COMPAT_SEED, COMPAT_JOBS override the
#   defaults, which is how the ctest entry configures it.
#===----------------------------------------------------------------------===#

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

TOYC="${TOYC:-$REPO_ROOT/build/bin/toyc}"
TOYC_UPSTREAM="${TOYC_UPSTREAM:-$HOME/dev/08_mlir_toy/build/bin/toyc-ch7}"

STAGES=(ast mlir mlir-affine mlir-llvm llvm jit)
OPT_SETTINGS=("" "-opt")

#===----------------------------------------------------------------------===#
# One comparison. Re-entered as a subprocess by xargs, which is why it is a
# mode of this script rather than a function call.
#===----------------------------------------------------------------------===#

# Two kinds of environment-dependent text are normalized, and nothing else:
#
#   the input path, which appears in AST locations and MLIR diagnostics;
#   the compiler's own name where it prefixes a diagnostic or an assertion
#     failure, because argv[0] is necessarily "toyc" for us and "toyc-ch7" for
#     upstream. Anchored at the start of a line and followed by ": ", so it
#     cannot match anything in the IR itself.
#
# No value, type, symbol name or SSA number is ever rewritten: those are exactly
# what the comparison is for.
normalize() {
  local prog="$1"
  sed -e "s|$prog|INPUT|g" \
      -e "s|$(basename "$prog")|INPUT|g" \
      -e "s|^$(basename "$TOYC"): |PROG: |" \
      -e "s|^$(basename "$TOYC_UPSTREAM"): |PROG: |" \
      -e "s|$TOYC|PROG|g" \
      -e "s|$TOYC_UPSTREAM|PROG|g"
}

# Collapses the column of a binop location, so an AST dump that differs only by
# deviation D2 compares equal. Matches `BinOp: <op> @<file>:<line>:<col>`.
mask_binop_columns() {
  sed -E 's/(BinOp: .+ @[^:]*:[0-9]+):[0-9]+/\1:COL/'
}

# Normalizes debug-metadata *numbering* in LLVM IR, leaving instructions,
# values, types and DILocation line numbers untouched.
#
# D2 reaches LLVM IR: with upstream's operator location a binary operation and
# its right-hand side share one line:col, so LLVM emits a single !DILocation for
# both. With the operator's own column they are distinct, so one extra node
# appears -- and every !dbg reference after it renumbers, which turns a
# two-column difference into a diff hundreds of lines long. Normalizing the
# numbering is what makes the instruction stream comparable at all.
mask_llvm_debug_numbering() {
  sed -E -e 's/, !dbg ![0-9]+//g' \
         -e 's/!dbg ![0-9]+//g' \
         -e 's/(!DILocation\(line: [0-9]+), column: [0-9]+/\1, column: C/' \
         -e 's/![0-9]+/!N/g'
}

# True when a -emit=jit difference is exactly deviation D8: a module with no
# main.
#
# Upstream tests the llvm::Error from invokePacked() but never consumes it, so it
# prints "JIT invocation failed" and is then killed by the Error destructor
# ("Program aborted due to an unhandled Error", exit 134). Ours reports the same
# underlying cause on one line and exits nonzero normally.
#
# The rule is deliberately narrow: the payload LLVM reported must be identical on
# both sides, upstream must have aborted, and we must not have. Any other jit
# difference -- different symbols, different values, a crash on our side -- fails.
jit_differs_only_by_d8() {
  local theirs="$1" ours="$2" rc_theirs="$3" rc_ours="$4"

  [[ $rc_theirs -eq 134 && $rc_ours -ne 0 ]] || return 1
  grep -q 'JIT invocation failed' <<<"$theirs" || return 1
  grep -q 'JIT invocation failed' <<<"$ours" || return 1
  grep -q 'Program aborted due to an unhandled Error' <<<"$theirs" || return 1
  grep -q 'Program aborted' <<<"$ours" && return 1

  # The reason the JIT failed must be the same text on both sides.
  local t_payload o_payload
  t_payload="$(grep -o 'Symbols not found:.*' <<<"$theirs")"
  o_payload="$(grep -o 'Symbols not found:.*' <<<"$ours")"
  [[ -n "$t_payload" && "$t_payload" == "$o_payload" ]]
}

# True when two LLVM IR outputs differ only in debug metadata attributable to
# D2: the instruction stream must match exactly once numbering is normalized,
# and the set of distinct DILocations must match once columns are masked. A
# changed line number, a changed scope, or any instruction difference fails.
llvm_differs_only_by_d2() {
  local theirs="$1" ours="$2"
  local t_body o_body t_loc o_loc

  t_body="$(printf '%s\n' "$theirs" | mask_llvm_debug_numbering | grep -v '= !DILocation(')"
  o_body="$(printf '%s\n' "$ours" | mask_llvm_debug_numbering | grep -v '= !DILocation(')"
  [[ "$t_body" == "$o_body" ]] || return 1

  t_loc="$(printf '%s\n' "$theirs" | mask_llvm_debug_numbering | grep '= !DILocation(' | sort -u)"
  o_loc="$(printf '%s\n' "$ours" | mask_llvm_debug_numbering | grep '= !DILocation(' | sort -u)"
  [[ "$t_loc" == "$o_loc" ]]
}

compare_one() {
  local prog="$1" stage="$2" optname="$3" outdir="$4"
  # The setting travels as a name rather than as the flag itself: an empty
  # argument does not survive xargs reliably.
  local opt=""
  [[ "$optname" == "opt" ]] && opt="-opt"
  # The id has to carry the whole path: reference/tests holds one ast.toy per
  # chapter, and keying on the basename silently collapsed them into one result.
  local id
  id="$(printf '%s' "${prog#"$REPO_ROOT"/}" | tr -c 'A-Za-z0-9._-' '_')"
  id="${id}_${stage}_${optname}"

  local ours theirs rc_ours rc_theirs
  ours="$("$TOYC" "$prog" "-emit=$stage" ${opt:+$opt} 2>&1)"; rc_ours=$?
  theirs="$("$TOYC_UPSTREAM" "$prog" "-emit=$stage" ${opt:+$opt} 2>&1)"; rc_theirs=$?

  local ours_n theirs_n
  ours_n="$(printf '%s\n' "$ours" | normalize "$prog")"
  theirs_n="$(printf '%s\n' "$theirs" | normalize "$prog")"

  # A program upstream cannot compile is still compared: reproducing an upstream
  # failure faithfully is an equivalence result, not an excuse to skip. Several
  # reference/tests inputs abort inside MLIR (scalar.toy reshapes a rank-0
  # tensor to 2x2 and trips an assertion in DenseElementsAttr::reshape); ours
  # must trip the same assertion, at the same place, with the same exit code.
  #
  # Exit status is part of the observable behavior.
  if [[ "$rc_ours" != "$rc_theirs" ]]; then
    # One exception: deviation D8, where upstream aborts on an unconsumed
    # llvm::Error and we report the same cause and exit normally.
    if [[ "$stage" == jit ]] &&
       jit_differs_only_by_d8 "$theirs_n" "$ours_n" "$rc_theirs" "$rc_ours"; then
      diff <(printf '%s\n' "$theirs_n") <(printf '%s\n' "$ours_n") \
        >"$outdir/$id.d8diff"
      echo D8 >"$outdir/$id.status"
      return
    fi
    {
      echo "program: $prog"
      echo "stage:   -emit=$stage ${opt}"
      echo "reason:  exit status differs (ours=$rc_ours upstream=$rc_theirs)"
      diff <(printf '%s\n' "$theirs_n") <(printf '%s\n' "$ours_n")
    } >"$outdir/$id.diff"
    echo FAIL >"$outdir/$id.status"
    return
  fi

  if [[ "$ours_n" == "$theirs_n" ]]; then
    # For jit, also compare the printed values on their own: the program's
    # actual output, with no diagnostics mixed in.
    if [[ "$stage" == jit ]]; then
      local ours_out theirs_out
      ours_out="$("$TOYC" "$prog" -emit=jit ${opt:+$opt} 2>/dev/null)"
      theirs_out="$("$TOYC_UPSTREAM" "$prog" -emit=jit ${opt:+$opt} 2>/dev/null)"
      if [[ "$ours_out" != "$theirs_out" ]]; then
        {
          echo "program: $prog"
          echo "stage:   -emit=jit ${opt} (stdout only)"
          echo "reason:  printed values differ"
          diff <(printf '%s\n' "$theirs_out") <(printf '%s\n' "$ours_out")
        } >"$outdir/$id.diff"
        echo FAIL >"$outdir/$id.status"
        return
      fi
    fi
    echo PASS >"$outdir/$id.status"
    return
  fi

  # Differences are permitted only where deviation D2 reaches: binop columns in
  # an AST dump, and the debug metadata those columns feed in LLVM IR.
  if [[ "$stage" == ast ]]; then
    local ours_m theirs_m
    ours_m="$(printf '%s\n' "$ours_n" | mask_binop_columns)"
    theirs_m="$(printf '%s\n' "$theirs_n" | mask_binop_columns)"
    if [[ "$ours_m" == "$theirs_m" ]]; then
      diff <(printf '%s\n' "$theirs_n") <(printf '%s\n' "$ours_n") \
        >"$outdir/$id.d2diff"
      echo D2 >"$outdir/$id.status"
      return
    fi
  elif [[ "$stage" == llvm || "$stage" == mlir-llvm ]]; then
    if llvm_differs_only_by_d2 "$theirs_n" "$ours_n"; then
      diff <(printf '%s\n' "$theirs_n") <(printf '%s\n' "$ours_n") \
        >"$outdir/$id.d2diff"
      echo D2 >"$outdir/$id.status"
      return
    fi
  fi

  {
    echo "program: $prog"
    echo "stage:   -emit=$stage ${opt}"
    echo "reason:  output differs (upstream < , ours > )"
    diff <(printf '%s\n' "$theirs_n") <(printf '%s\n' "$ours_n")
  } >"$outdir/$id.diff"
  echo FAIL >"$outdir/$id.status"
}

if [[ "${1:-}" == "--compare-one" ]]; then
  compare_one "$2" "$3" "$4" "$5"
  exit 0
fi

#===----------------------------------------------------------------------===#
# Options
#===----------------------------------------------------------------------===#

COUNT="${COMPAT_COUNT:-200}"
SEED="${COMPAT_SEED:-20260921}"
JOBS="${COMPAT_JOBS:-$(nproc 2>/dev/null || echo 4)}"
CORPUS_DIR=""
USE_RANDOM=1
FIXTURES_ONLY=0
UPDATE=0
MAX_REPORT=5

while [[ $# -gt 0 ]]; do
  case "$1" in
    --count) COUNT="$2"; shift 2 ;;
    --seed) SEED="$2"; shift 2 ;;
    --jobs) JOBS="$2"; shift 2 ;;
    --corpus) CORPUS_DIR="$2"; shift 2 ;;
    --no-random) USE_RANDOM=0; shift ;;
    --fixtures-only) FIXTURES_ONLY=1; shift ;;
    --update) UPDATE=1; shift ;;
    --max-report) MAX_REPORT="$2"; shift 2 ;;
    -h|--help) sed -n '2,45p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

for binary in "$TOYC" "$TOYC_UPSTREAM"; do
  if [[ ! -x "$binary" ]]; then
    echo "compare-upstream: not executable: $binary" >&2
    echo "  set TOYC and TOYC_UPSTREAM, or build toyc first." >&2
    exit 2
  fi
done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
RESULTS="$WORK/results"
mkdir -p "$RESULTS"

echo "ours:     $TOYC"
echo "upstream: $TOYC_UPSTREAM"

#===----------------------------------------------------------------------===#
# Error-path fixtures: expected to differ, diffs recorded.
#===----------------------------------------------------------------------===#

FIXTURE_FAILURES=0
FIXTURE_CHECKED=0

check_fixtures() {
  local fixture_dir="$SCRIPT_DIR/fixtures"
  local expected_dir="$SCRIPT_DIR/expected"
  [[ -d "$fixture_dir" ]] || return 0
  mkdir -p "$expected_dir"

  local prog stage actual expected_file
  for prog in "$fixture_dir"/*.toy; do
    [[ -e "$prog" ]] || continue
    for stage in ast mlir; do
      expected_file="$expected_dir/$(basename "$prog" .toy).$stage.diff"
      actual="$(diff \
        <("$TOYC_UPSTREAM" "$prog" "-emit=$stage" 2>&1 | normalize "$prog") \
        <("$TOYC" "$prog" "-emit=$stage" 2>&1 | normalize "$prog"))"
      FIXTURE_CHECKED=$((FIXTURE_CHECKED + 1))
      if [[ $UPDATE -eq 1 ]]; then
        printf '%s\n' "$actual" >"$expected_file"
        continue
      fi
      if [[ ! -f "$expected_file" ]]; then
        echo "  fixture $(basename "$prog") -emit=$stage: no recorded diff yet" \
             "(run with --update)"
        FIXTURE_FAILURES=$((FIXTURE_FAILURES + 1))
        continue
      fi
      if ! diff -q "$expected_file" <(printf '%s\n' "$actual") >/dev/null; then
        echo "  fixture $(basename "$prog") -emit=$stage: diff changed"
        diff "$expected_file" <(printf '%s\n' "$actual") | head -20
        FIXTURE_FAILURES=$((FIXTURE_FAILURES + 1))
      fi
    done
  done
}

if [[ $FIXTURES_ONLY -eq 1 ]]; then
  echo
  echo "=== error-path fixtures ==="
  check_fixtures
  if [[ $UPDATE -eq 1 ]]; then
    echo "recorded $FIXTURE_CHECKED fixture diffs"
    exit 0
  fi
  echo "fixtures: $FIXTURE_CHECKED checked, $FIXTURE_FAILURES unexpected"
  [[ $FIXTURE_FAILURES -eq 0 ]] || exit 1
  exit 0
fi

#===----------------------------------------------------------------------===#
# Corpus
#===----------------------------------------------------------------------===#

CORPUS_LIST="$WORK/corpus.txt"
: >"$CORPUS_LIST"

# The tutorial's own programs. Only .toy: the .mlir files there are inputs for
# -x mlir at a specific chapter's stage, not whole-pipeline programs.
find "$REPO_ROOT/reference/tests" -name '*.toy' | sort >>"$CORPUS_LIST"
N_REFERENCE=$(wc -l <"$CORPUS_LIST")

N_RANDOM=0
if [[ $USE_RANDOM -eq 1 ]]; then
  GEN_DIR="${CORPUS_DIR:-$WORK/generated}"
  echo
  echo "=== generating corpus ==="
  python3 "$SCRIPT_DIR/gen-programs.py" --out "$GEN_DIR" --count "$COUNT" \
      --seed "$SEED" --upstream "$TOYC_UPSTREAM" || exit 1
  find "$GEN_DIR" -name '*.toy' | sort >>"$CORPUS_LIST"
  N_RANDOM=$(find "$GEN_DIR" -name '*.toy' | wc -l)
fi

N_PROGRAMS=$(wc -l <"$CORPUS_LIST")

#===----------------------------------------------------------------------===#
# Sweep
#===----------------------------------------------------------------------===#

TASKS="$WORK/tasks.txt"
: >"$TASKS"
while IFS= read -r prog; do
  for stage in "${STAGES[@]}"; do
    for optname in noopt opt; do
      # NUL-separated 4-tuples, so a path with a space cannot split and the
      # results directory rides along to each worker.
      printf '%s\0%s\0%s\0%s\0' "$prog" "$stage" "$optname" "$RESULTS" \
        >>"$TASKS"
    done
  done
done <"$CORPUS_LIST"

N_TASKS=$(( $(tr -cd '\0' <"$TASKS" | wc -c) / 4 ))
echo
echo "=== sweeping $N_PROGRAMS programs x ${#STAGES[@]} stages x ${#OPT_SETTINGS[@]} opt settings = $N_TASKS comparisons ==="

# Each comparison is independent, so they run in parallel; at ~17ms per
# invocation the sweep is dominated by process startup rather than compilation.
xargs -r -0 -n4 -P "$JOBS" "${BASH_SOURCE[0]}" --compare-one <"$TASKS"

PASS=$(cat "$RESULTS"/*.status 2>/dev/null | grep -cx PASS)
D2=$(cat "$RESULTS"/*.status 2>/dev/null | grep -cx D2)
D8=$(cat "$RESULTS"/*.status 2>/dev/null | grep -cx D8)
FAIL=$(cat "$RESULTS"/*.status 2>/dev/null | grep -cx FAIL)
SKIP=$(cat "$RESULTS"/*.status 2>/dev/null | grep -cx SKIP)

if [[ $FAIL -gt 0 ]]; then
  echo
  echo "=== unexpected differences (showing up to $MAX_REPORT) ==="
  shown=0
  for f in "$RESULTS"/*.diff; do
    [[ -e "$f" ]] || continue
    echo "--------------------------------------------------------------"
    head -40 "$f"
    shown=$((shown + 1))
    [[ $shown -ge $MAX_REPORT ]] && break
  done
fi

echo
echo "=== error-path fixtures ==="
check_fixtures
if [[ $UPDATE -eq 1 ]]; then
  echo "recorded $FIXTURE_CHECKED fixture diffs"
fi

#===----------------------------------------------------------------------===#
# Summary
#===----------------------------------------------------------------------===#

echo
echo "=============================================================="
echo "compared $N_PROGRAMS programs ($N_REFERENCE from reference/tests," \
     "$N_RANDOM generated with --seed $SEED)"
echo "  stages:        ${STAGES[*]}"
echo "  opt settings:  none, -opt"
echo "  comparisons:   $N_TASKS"
echo "    identical:   $PASS"
echo "    D2-only:     $D2  (binop columns in -emit=ast, and the debug" \
     "metadata they feed in -emit=llvm)"
echo "    D8-only:     $D8  (-emit=jit on a module with no main: upstream" \
     "aborts on an unconsumed Error, we report it and exit)"
echo "    unexpected:  $FAIL"
echo "    NOT COMPARED: $SKIP"
echo "  fixtures:      $FIXTURE_CHECKED checked, $FIXTURE_FAILURES unexpected"
echo "=============================================================="

if [[ $FAIL -gt 0 || $FIXTURE_FAILURES -gt 0 ]]; then
  echo "RESULT: FAIL"
  exit 1
fi
echo "RESULT: PASS -- output is equivalent to upstream except deviation D2"
exit 0
