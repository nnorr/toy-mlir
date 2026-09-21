//===- Passes.h - Toy transformation passes -------------------------------===//
//
// Adapted from mlir/examples/toy/Ch7 in the LLVM Project, under the Apache
// License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The three passes Toy writes for itself. Everything else in the pipeline
// (inliner, canonicalizer, CSE, loop fusion) is a generic MLIR pass that works
// on Toy IR only because the dialect implements the right interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef TOY_PASSES_H
#define TOY_PASSES_H

#include <memory>

namespace mlir {
class Pass;

namespace toy {

/// Propagates shapes forward inside one function, using each operation's
/// ShapeInference interface. Runs after inlining, when every call has been
/// replaced by its body and the function's operand types are known.
std::unique_ptr<Pass> createShapeInferencePass();

/// Partial lowering: the computational Toy operations become affine loop nests
/// over memrefs, while toy.print survives to be handled later. This is where
/// tensors (values) become memrefs (buffers).
std::unique_ptr<Pass> createLowerToAffinePass();

/// Full lowering: affine, arith, func, memref, scf and the surviving toy.print
/// all become the LLVM dialect.
std::unique_ptr<Pass> createLowerToLLVMPass();

} // namespace toy
} // namespace mlir

#endif // TOY_PASSES_H
