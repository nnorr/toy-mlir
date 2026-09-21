//===- ShapeInferenceInterface.h - Shape inference op interface -----------===//
//
// Adapted from mlir/examples/toy/Ch7 in the LLVM Project, under the Apache
// License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Wrapper around the code generated from ShapeInferenceInterface.td. The
// generated header declares the `ShapeInference` interface class, whose
// implementation is the Concept/Model pair described in
// docs/07-interfaces.md.
//
//===----------------------------------------------------------------------===//

#ifndef TOY_SHAPEINFERENCEINTERFACE_H
#define TOY_SHAPEINFERENCEINTERFACE_H

#include "mlir/IR/OpDefinition.h"

namespace mlir {
namespace toy {

/// Include the auto-generated declarations.
#include "toy/ShapeInferenceOpInterfaces.h.inc"

} // namespace toy
} // namespace mlir

#endif // TOY_SHAPEINFERENCEINTERFACE_H
