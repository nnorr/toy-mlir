//===- ShapeInferenceInterface.td - Shape Inference Interface -*- tablegen -==//
//
// Adapted from mlir/examples/toy/Ch7 in the LLVM Project, under the Apache
// License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The one op interface Toy defines for itself.
//
// An interface is a contract. The ShapeInference pass in
// passes/ShapeInference.cpp knows nothing about add, mul or transpose. It asks
// each op it meets whether it implements this interface, and if so tells it to
// set its own result shape.
// Adding a new shaped Toy op therefore means implementing inferShapes(), not
// editing the pass.
//
//===----------------------------------------------------------------------===//

#ifndef SHAPE_INFERENCE_INTERFACE
#define SHAPE_INFERENCE_INTERFACE

include "mlir/IR/OpBase.td"

def ShapeInferenceOpInterface : OpInterface<"ShapeInference"> {
  let description = [{
    Access to an operation's own shape rule: given operands whose types are
    already known, set the result types.
  }];

  let methods = [
    InterfaceMethod<"Infer and set the output shape for the current operation.",
                    "void", "inferShapes">
  ];
}

#endif // SHAPE_INFERENCE_INTERFACE
