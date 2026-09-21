// The verifier rejecting hand-written IR. Seeded from
// reference/tests/Ch7/invalid.mlir.
//
// Three things are wrong here: toy.print returns a value, takes no operand, and
// the block has no terminator. The first is what the verifier reports, and it
// reports it because ODS generated that check from `let results = (outs)` in
// Ops.td -- nobody wrote it by hand.
//
// RUN: not %toyc %s -emit=mlir 2>&1 | %filecheck %s

toy.func @main() {
  %0 = "toy.print"() : () -> tensor<2x3xf64>
}

// CHECK: error: 'toy.print' op requires zero results
