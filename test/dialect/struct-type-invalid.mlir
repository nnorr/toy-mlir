// !toy.struct holds tensors or other structs. Anything else is rejected by the
// type parser, before a verifier ever sees it.
//
// RUN: not %toyc %s -emit=mlir 2>&1 | %filecheck %s

toy.func @main(%arg0: !toy.struct<i32>) {
  toy.return
}

// CHECK: error: element type for a struct must either be a TensorType or a StructType, got: 'i32'
// CHECK: Error can't load file
