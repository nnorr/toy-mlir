// Shape inference fails when an op's operands never become ranked. Here the
// operand is main's own argument, which nothing ever resolves.
//
// RUN: not %toyc %s -emit=mlir -opt 2>&1 | %filecheck %s

toy.func @main(%arg0: tensor<*xf64>) {
  %0 = toy.add %arg0, %arg0 : tensor<*xf64>
  toy.print %0 : tensor<*xf64>
  toy.return
}

// CHECK: error: Shape inference failed, 1 operations couldn't be inferred
