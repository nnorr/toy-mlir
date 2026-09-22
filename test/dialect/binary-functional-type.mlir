// toy.add and toy.mul print one type when operands and result agree, and a
// functional type when they do not. Both forms must parse back.
//
// RUN: %toyc %s -emit=mlir 2>&1 | %filecheck %s

toy.func @main(%arg0: tensor<*xf64>) {
  %0 = toy.constant dense<[1.0, 2.0]> : tensor<2xf64>
  %1 = toy.add %0, %arg0 : (tensor<2xf64>, tensor<*xf64>) -> tensor<*xf64>
  %2 = toy.mul %1, %1 : tensor<*xf64>
  toy.print %2 : tensor<*xf64>
  toy.return
}

// CHECK: toy.add %{{.*}}, %{{.*}} : (tensor<2xf64>, tensor<*xf64>) -> tensor<*xf64>
// CHECK: toy.mul %{{.*}}, %{{.*}} : tensor<*xf64>
