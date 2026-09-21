#!/usr/bin/env python3
"""Generate random *valid* Toy programs for the differential sweep.

The ~15 programs under reference/tests/ are far too few to support a claim of
output equivalence with the upstream tutorial. This produces as many as asked
for, reproducibly, so compare-upstream.sh can treat toyc-ch7 as a golden
reference and diff against it the way a hardware flow diffs against a reference
model.

"Valid" is the whole difficulty. Toy has no type checker worth the name: a
program with mismatched shapes is accepted by the parser, survives MLIRGen, and
then either fails in shape inference or reads out of bounds at run time. Such a
program is not an interesting differential input -- both compilers would agree
on the failure -- so the generator tracks shapes as it builds an expression and
only ever emits shape-correct code, rather than generating freely and repairing
afterwards.

The rules it respects, all learned from what upstream actually accepts:
  * values are f64 tensors of rank 1 or 2, nothing else;
  * `+` and `*` are element-wise and need identical operand shapes;
  * transpose reverses a shape, and is only applied to rank 2 here;
  * a declaration with an explicit shape reshapes, so the element count of the
    initializer must match the declared shape exactly;
  * user functions take unranked parameters, so every call site of one function
    must pass the same shape, or shape inference has nothing to conclude;
  * struct members are unranked in the struct type and only become concrete
    because struct_access(struct_constant) folds -- so a struct is always
    initialized with a literal and accessed directly, never passed around;
  * print() takes a tensor, never a struct;
  * main() takes no arguments and returns nothing (the affine lowering rejects
    anything else).

Every candidate is then compiled by upstream before being kept, so a mistake in
the rules above shows up as a high skip rate rather than as a corpus that
quietly tests nothing.

Usage:
  gen-programs.py --out DIR [--count 200] [--seed 20260921] [--upstream PATH]
"""

import argparse
import os
import random
import shutil
import subprocess
import sys

# Small shapes only: the sweep compares printed output, and a 4x4 tensor already
# produces 16 lines of floats per print.
RANK1_SIZES = [2, 3, 4]
RANK2_DIMS = [1, 2, 3]


def shape_count(shape):
    n = 1
    for d in shape:
        n *= d
    return n


def reverse_shape(shape):
    return list(reversed(shape))


class ProgramGenerator:
    """Builds one program. One instance per program, so names never collide."""

    def __init__(self, rng):
        self.rng = rng
        self.lines = []
        self.functions = []  # (name, arity, param_shape, result_shape, body)
        self.structs = []  # (name, [member names], [member shapes])

        # A small pool of shapes per program, rather than a fresh random shape
        # every time. Shapes then collide often enough that variables and call
        # results get reused instead of every operand being a fresh literal --
        # which is what puts shape inference under real load.
        pool = [self.fresh_rank2_shape() for _ in range(2)]
        pool.append([self.rng.choice(RANK1_SIZES)])
        pool.append(reverse_shape(pool[0]))
        self.shape_pool = pool

    #===------------------------------------------------------------------===#
    # Literals and shapes
    #===------------------------------------------------------------------===#

    def fresh_rank2_shape(self):
        return [self.rng.choice(RANK2_DIMS), self.rng.choice(RANK2_DIMS)]

    def random_shape(self):
        return list(self.rng.choice(self.shape_pool))

    def random_rank2_shape(self):
        rank2 = [s for s in self.shape_pool if len(s) == 2]
        return list(self.rng.choice(rank2))

    def random_value(self):
        # Integers mostly, halves sometimes: both print exactly in %f and in
        # MLIR's float attribute syntax, so neither compiler can differ on
        # rounding.
        if self.rng.random() < 0.25:
            return "%d.5" % self.rng.randint(0, 8)
        return str(self.rng.randint(1, 9))

    def literal(self, shape):
        """A nested literal of exactly `shape`."""
        if len(shape) == 1:
            return "[" + ", ".join(self.random_value() for _ in range(shape[0])) + "]"
        rows = []
        for _ in range(shape[0]):
            rows.append("[" + ", ".join(self.random_value()
                                        for _ in range(shape[1])) + "]")
        return "[" + ", ".join(rows) + "]"

    def flat_literal(self, count):
        """A rank-1 literal of `count` elements, for the reshape form."""
        return "[" + ", ".join(self.random_value() for _ in range(count)) + "]"

    #===------------------------------------------------------------------===#
    # Expressions
    #===------------------------------------------------------------------===#

    def expr(self, shape, scope, depth=0):
        """An expression of exactly `shape`.

        `scope` maps variable name -> shape. Only shape-preserving or
        shape-known constructs are used, so the result's shape is never in
        doubt.
        """
        candidates = ["literal"]
        same = [v for v, s in scope.items() if s == shape]
        if same:
            candidates += ["var", "var"]  # prefer reusing values
        if depth < 2:
            candidates += ["binop"]
            if len(shape) == 2:
                candidates += ["transpose"]
        if depth < 1:
            callable_fns = [f for f in self.functions if f[3] == shape]
            if callable_fns:
                candidates += ["call"]

        choice = self.rng.choice(candidates)

        if choice == "var":
            return self.rng.choice(same)
        if choice == "literal":
            return self.literal(shape)
        if choice == "binop":
            op = self.rng.choice(["+", "*"])
            lhs = self.expr(shape, scope, depth + 1)
            rhs = self.expr(shape, scope, depth + 1)
            return "%s %s %s" % (lhs, op, rhs)
        if choice == "transpose":
            # transpose flips the shape, so build the operand at the reverse.
            inner = self.expr(reverse_shape(shape), scope, depth + 1)
            return "transpose(%s)" % inner
        if choice == "call":
            fn = self.rng.choice([f for f in self.functions if f[3] == shape])
            name, arity, param_shape, _result, _body = fn
            args = [self.expr(param_shape, scope, depth + 1) for _ in range(arity)]
            return "%s(%s)" % (name, ", ".join(args))
        raise AssertionError("unreachable")

    #===------------------------------------------------------------------===#
    # Functions
    #===------------------------------------------------------------------===#

    def gen_function(self, index):
        """One function, whose shape rule is fixed at generation time.

        Parameters are unranked in the IR, so the shape they will have is a
        property of the call sites, not of the definition. Recording it here is
        what lets main() only ever call this with consistent arguments.
        """
        name = "fn%d" % index
        param_shape = self.random_rank2_shape()
        arity = self.rng.choice([1, 1, 2, 2, 3])
        params = ["p%d" % i for i in range(arity)]

        # Each template's result shape is known from param_shape alone.
        templates = [("return p0;", param_shape)]
        if arity >= 2:
            templates += [
                ("return p0 * p1;", param_shape),
                ("return p0 + p1;", param_shape),
                ("return transpose(p0) + transpose(p1);", reverse_shape(param_shape)),
            ]
        templates += [
            ("return transpose(p0);", reverse_shape(param_shape)),
            ("var t0 = transpose(p0);\n  return t0 * t0;",
             reverse_shape(param_shape)),
        ]
        if arity >= 3:
            templates += [("return p0 * p1 + p2;", param_shape)]

        body, result_shape = self.rng.choice(templates)
        text = "def %s(%s) {\n  %s\n}\n" % (name, ", ".join(params), body)
        self.functions.append((name, arity, param_shape, result_shape, text))
        return text

    #===------------------------------------------------------------------===#
    # Structs
    #===------------------------------------------------------------------===#

    def gen_struct(self, index):
        name = "St%d" % index
        count = self.rng.choice([2, 2, 3])
        members = ["m%d" % i for i in range(count)]
        shapes = [self.random_shape() for _ in range(count)]
        self.structs.append((name, members, shapes))
        body = "".join("  var %s;\n" % m for m in members)
        return "struct %s {\n%s}\n" % (name, body)

    #===------------------------------------------------------------------===#
    # main()
    #===------------------------------------------------------------------===#

    def gen_main(self):
        scope = {}
        body = []
        next_var = [0]

        def fresh():
            name = "v%d" % next_var[0]
            next_var[0] += 1
            return name

        # A couple of variables to build on, some of them declared with an
        # explicit shape so the reshape path is covered.
        for _ in range(self.rng.randint(2, 4)):
            shape = self.random_shape()
            name = fresh()
            form = self.rng.random()
            if form < 0.4:
                # Reshape form: flat initializer, element count must match.
                body.append("  var %s<%s> = %s;" %
                            (name, ", ".join(str(d) for d in shape),
                             self.flat_literal(shape_count(shape))))
            elif form < 0.7:
                # Declared shape matching a nested literal of the same shape.
                body.append("  var %s<%s> = %s;" %
                            (name, ", ".join(str(d) for d in shape),
                             self.literal(shape)))
            else:
                # Inferred shape.
                body.append("  var %s = %s;" % (name, self.literal(shape)))
            scope[name] = shape

        # A struct, initialized with literals and accessed directly: that is the
        # only form whose accesses fold away before shape inference runs.
        if self.structs:
            sname, members, shapes = self.structs[0]
            var = fresh()
            inits = ", ".join(self.literal(s) for s in shapes)
            body.append("  %s %s = {%s};" % (sname, var, inits))
            for member, shape in zip(members, shapes):
                if self.rng.random() < 0.6:
                    mvar = fresh()
                    body.append("  var %s = %s.%s;" % (mvar, var, member))
                    scope[mvar] = shape

        # Call every function that was defined, so none is left unused (an
        # uncalled function is deleted by the inliner, which would make the
        # definition dead weight rather than part of the test).
        for name, arity, param_shape, result_shape, _ in self.functions:
            args = []
            for _ in range(arity):
                same = [v for v, s in scope.items() if s == param_shape]
                if same and self.rng.random() < 0.7:
                    args.append(self.rng.choice(same))
                else:
                    args.append(self.literal(param_shape))
            var = fresh()
            body.append("  var %s = %s(%s);" % (var, name, ", ".join(args)))
            scope[var] = result_shape

        # Some derived values, then prints. Something must be printed or the
        # whole program is dead code and both compilers emit an empty main.
        for _ in range(self.rng.randint(0, 2)):
            shape = self.rng.choice(list(scope.values()))
            var = fresh()
            body.append("  var %s = %s;" % (var, self.expr(shape, scope)))
            scope[var] = shape

        printed = 0
        for _ in range(self.rng.randint(1, 3)):
            shape = self.rng.choice(list(scope.values()))
            body.append("  print(%s);" % self.expr(shape, scope))
            printed += 1
        assert printed

        return "def main() {\n%s\n}\n" % "\n".join(body)

    #===------------------------------------------------------------------===#

    def generate(self):
        parts = []
        for i in range(self.rng.choice([0, 0, 1, 1, 2])):
            parts.append(self.gen_struct(i))
        for i in range(self.rng.choice([0, 1, 1, 2, 3])):
            parts.append(self.gen_function(i))
        parts.append(self.gen_main())
        return "\n".join(parts)


def upstream_accepts(upstream, path):
    """True if upstream compiles and runs `path` cleanly.

    -emit=jit -opt exercises everything: MLIRGen, inlining, shape inference,
    both lowerings, translation and execution. If that succeeds the program is
    a usable differential input.
    """
    for flags in (["-emit=jit"], ["-emit=jit", "-opt"]):
        try:
            proc = subprocess.run([upstream, path] + flags, capture_output=True,
                                  text=True, timeout=30)
        except subprocess.TimeoutExpired:
            return False
        if proc.returncode != 0 or not proc.stdout.strip():
            return False
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", required=True, help="output directory")
    parser.add_argument("--count", type=int, default=200)
    parser.add_argument("--seed", type=int, default=20260921)
    parser.add_argument("--upstream",
                        default=os.path.expanduser(
                            "~/dev/08_mlir_toy/build/bin/toyc-ch7"),
                        help="upstream toyc used to validate each candidate")
    parser.add_argument("--no-validate", action="store_true",
                        help="skip upstream validation (faster, unsafe)")
    args = parser.parse_args()

    if os.path.isdir(args.out):
        shutil.rmtree(args.out)
    os.makedirs(args.out)

    rng = random.Random(args.seed)
    kept = 0
    skipped = 0
    attempts = 0
    # A generous ceiling: a correct generator needs barely more attempts than
    # programs, and a broken one should fail loudly instead of spinning.
    max_attempts = args.count * 10

    while kept < args.count and attempts < max_attempts:
        attempts += 1
        text = ProgramGenerator(rng).generate()
        path = os.path.join(args.out, "prog%04d.toy" % kept)
        with open(path, "w") as f:
            f.write("# Generated by gen-programs.py --seed %d (program %d)\n"
                    % (args.seed, kept))
            f.write(text)

        if args.no_validate or upstream_accepts(args.upstream, path):
            kept += 1
        else:
            os.unlink(path)
            skipped += 1

    with open(os.path.join(args.out, "MANIFEST"), "w") as f:
        f.write("seed %d\ncount %d\nskipped %d\nattempts %d\n"
                % (args.seed, kept, skipped, attempts))

    rate = (100.0 * skipped / attempts) if attempts else 0.0
    print("generated %d programs in %s (seed %d): %d rejected by upstream "
          "(%.1f%% of %d attempts)" %
          (kept, args.out, args.seed, skipped, rate, attempts))
    if kept < args.count:
        print("ERROR: only %d of %d programs generated; the generator is "
              "producing invalid Toy" % (kept, args.count), file=sys.stderr)
        return 1
    if rate > 20.0:
        print("WARNING: high rejection rate (%.1f%%); the generator's shape "
              "rules are probably wrong" % rate, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
