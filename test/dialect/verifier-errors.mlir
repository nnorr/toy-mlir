// Hand-written IR the dialect rejects, one module per RUN line read from stdin.
// MLIR stops at the first failure in a module, so each case needs its own
// input. The generic "toy.op"() form is used where the custom syntax cannot
// express the mistake: toy.constant's custom form takes its type from the
// attribute, so the two can only disagree when written generically.

// RUN: echo 'toy.func @main() { "toy.constant"() {value = dense<[1.0, 2.0]> : tensor<2xf64>} : () -> tensor<3xf64> toy.return }' | not %toyc - -x mlir -emit=mlir 2>&1 | %filecheck %s --check-prefix=CONST-SHAPE
// RUN: echo 'toy.func @main() { "toy.constant"() {value = dense<[1.0, 2.0]> : tensor<2xf64>} : () -> tensor<1x2xf64> toy.return }' | not %toyc - -x mlir -emit=mlir 2>&1 | %filecheck %s --check-prefix=CONST-RANK
// RUN: echo 'toy.func @main() { %0 = toy.constant 1.0 toy.return }' | not %toyc - -x mlir -emit=mlir 2>&1 | %filecheck %s --check-prefix=CONST-SYNTAX
// RUN: echo 'toy.func @main() { "toy.struct_constant"() {value = [dense<1> : tensor<i32>]} : () -> !toy.struct<tensor<*xf64>> toy.return }' | not %toyc - -x mlir -emit=mlir 2>&1 | %filecheck %s --check-prefix=STRUCT-ELEM-KIND
// RUN: echo 'toy.func @main() { "toy.struct_constant"() {value = [dense<1.0> : tensor<f64>]} : () -> !toy.struct<tensor<*xf64>, tensor<*xf64>> toy.return }' | not %toyc - -x mlir -emit=mlir 2>&1 | %filecheck %s --check-prefix=STRUCT-COUNT
// RUN: echo 'toy.func @main() { "toy.struct_constant"() {value = [[dense<1.0> : tensor<2xf64>]]} : () -> !toy.struct<!toy.struct<tensor<3xf64>>> toy.return }' | not %toyc - -x mlir -emit=mlir 2>&1 | %filecheck %s --check-prefix=STRUCT-NESTED
// RUN: echo 'toy.func @main(%arg0: !toy.struct<tensor<*xf64>>) { "toy.struct_access"(%arg0) {index = 1 : i64} : (!toy.struct<tensor<*xf64>>) -> tensor<*xf64> toy.return }' | not %toyc - -x mlir -emit=mlir 2>&1 | %filecheck %s --check-prefix=ACCESS-INDEX
// RUN: echo 'toy.func @main(%arg0: !toy.struct<tensor<*xf64>>) { "toy.struct_access"(%arg0) {index = 0 : i64} : (!toy.struct<tensor<*xf64>>) -> tensor<2xf64> toy.return }' | not %toyc - -x mlir -emit=mlir 2>&1 | %filecheck %s --check-prefix=ACCESS-TYPE
// RUN: echo 'toy.func @main(%arg0: tensor<*xf64>) -> tensor<*xf64> { toy.return %arg0, %arg0 : tensor<*xf64>, tensor<*xf64> }' | not %toyc - -x mlir -emit=mlir 2>&1 | %filecheck %s --check-prefix=RETURN-COUNT
// RUN: echo 'toy.func @main(%arg0: tensor<3xf64>) -> tensor<2xf64> { toy.return %arg0 : tensor<3xf64> }' | not %toyc - -x mlir -emit=mlir 2>&1 | %filecheck %s --check-prefix=RETURN-TYPE
// RUN: echo 'toy.func @main() { %0 = toy.constant dense<1.0> : tensor<f64> %1 = toy.generic_call @nope(%0) : (tensor<f64>) -> tensor<*xf64> toy.return }' | not %toyc - -x mlir -emit=mlir 2>&1 | %filecheck %s --check-prefix=CALL-CALLEE
// RUN: echo 'toy.func @main(%arg0: tensor<2xf64>) { %0 = toy.cast %arg0 : tensor<2xf64> to tensor<3xf64> toy.return }' | not %toyc - -x mlir -emit=mlir 2>&1 | %filecheck %s --check-prefix=CAST
// RUN: echo 'toy.func @main(%arg0: tensor<*xf64>) { %0 = toy.add %arg0 : tensor<*xf64> toy.return }' | not %toyc - -x mlir -emit=mlir 2>&1 | %filecheck %s --check-prefix=BINARY-SYNTAX

// CONST-SHAPE: 'toy.constant' op return type shape mismatches its attribute at dimension 0: 2 != 3
// CONST-RANK: 'toy.constant' op return type must match the one of the attached value attribute: 1 != 2
// CONST-SYNTAX: custom op 'toy.constant' invalid kind of attribute specified
// STRUCT-ELEM-KIND: constant of TensorType must be initialized by a DenseFPElementsAttr, got dense<1> : tensor<i32>
// STRUCT-COUNT: constant of StructType must be initialized by an ArrayAttr with the same number of elements
// STRUCT-NESTED: 'toy.struct_constant' op return type shape mismatches its attribute at dimension 0: 2 != 3
// ACCESS-INDEX: 'toy.struct_access' op index should be within the range of the input struct type
// ACCESS-TYPE: 'toy.struct_access' op must have the same result type as the struct element referred to by the index
// RETURN-COUNT: 'toy.return' op expects at most 1 return operand
// RETURN-TYPE: type of return operand ('tensor<3xf64>') doesn't match function result type ('tensor<2xf64>')
// CALL-CALLEE: 'toy.generic_call' op 'nope' does not reference a toy.func
// CAST: 'toy.cast' op operand type 'tensor<2xf64>' and result type 'tensor<3xf64>' are cast incompatible
// BINARY-SYNTAX: custom op 'toy.add' expected 2 operands
