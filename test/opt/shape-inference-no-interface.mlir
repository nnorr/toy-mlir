// Shape inference fails on an op with an unranked result and no
// ShapeInferenceOpInterface. A recursive call survives inlining, so after the
// one call in main is inlined, a toy.generic_call with a ranked operand is left.
// Toy source cannot express recursion, so this needs MLIR input.
//
// RUN: not %toyc %s -emit=mlir -opt 2>&1 | %filecheck %s

toy.func private @f(%arg0: tensor<*xf64>) -> tensor<*xf64> {
  %0 = toy.generic_call @f(%arg0) : (tensor<*xf64>) -> tensor<*xf64>
  toy.return %0 : tensor<*xf64>
}

toy.func @main() {
  %0 = toy.constant dense<[1.0, 2.0]> : tensor<2xf64>
  %1 = toy.generic_call @f(%0) : (tensor<2xf64>) -> tensor<*xf64>
  toy.print %1 : tensor<*xf64>
  toy.return
}

// CHECK: shape-inference-no-interface.mlir":15:8): error: unable to infer shape of operation without shape inference interface
