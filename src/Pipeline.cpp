//===- Pipeline.cpp - The Toy compilation pipeline ------------------------===//
//
// Adapted from the pass construction inlined in
// mlir/examples/toy/Ch7/toyc.cpp in the LLVM Project, under the Apache
// License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "toy/Pipeline.h"

#include "toy/DebugInfo.h"
#include "toy/Dialect.h"
#include "toy/Passes.h"

#include "mlir/Dialect/Affine/Transforms/Passes.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/Transforms/Passes.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"

#include "llvm/Support/ErrorHandling.h"

namespace toy {

llvm::StringRef getStageName(Stage stage) {
  switch (stage) {
  case Stage::AST:
    return "ast";
  case Stage::MLIR:
    return "mlir";
  case Stage::MLIRAffine:
    return "mlir-affine";
  case Stage::MLIRLLVM:
    return "mlir-llvm";
  case Stage::LLVMIR:
    return "llvm";
  case Stage::Object:
    return "obj";
  case Stage::JIT:
    return "jit";
  }
  llvm_unreachable("unknown Stage");
}

mlir::LogicalResult buildPipeline(mlir::PassManager &pm,
                                  const PipelineOptions &opts) {
  // Both flags are comparisons rather than equality tests because every stage
  // is a superset of the one before it: asking for an object file or for the
  // JIT means running the affine lowering too.
  const bool isLoweringToAffine = opts.stage >= Stage::MLIRAffine;
  const bool isLoweringToLLVM = opts.stage >= Stage::MLIRLLVM;

  // The Toy-level work. Requested explicitly with -opt, and implied by any
  // lowering: the lowering patterns need static shapes, and shapes only become
  // static once calls have been inlined and inference has run.
  if (opts.enableOpt || isLoweringToAffine) {
    // Inlining first, and at module scope, because it makes the remaining
    // work intraprocedural: afterwards there is one function whose
    // argument types are known, rather than N generic ones.
    pm.addPass(mlir::createInlinerPass());

    // Nested under toy.func: these run on one function at a time, which lets
    // the pass manager schedule them in parallel and keeps each pass from
    // having to walk the module itself.
    mlir::OpPassManager &optPM = pm.nest<mlir::toy::FuncOp>();
    // Canonicalize before inference so that the reshape folding has already
    // removed the ops whose shapes would otherwise have to be inferred.
    optPM.addPass(mlir::createCanonicalizerPass());
    optPM.addPass(mlir::toy::createShapeInferencePass());
    optPM.addPass(mlir::createCanonicalizerPass());
    // CSE collapses the two identical transposes of the tutorial's example
    // into the single `mul %1, %1` the docs show.
    optPM.addPass(mlir::createCSEPass());
  }

  if (isLoweringToAffine) {
    pm.addPass(mlir::toy::createLowerToAffinePass());

    // Cleanups over the loop nests the lowering just produced, now anchored on
    // func.func: toy.func no longer exists at this point.
    mlir::OpPassManager &optPM = pm.nest<mlir::func::FuncOp>();
    optPM.addPass(mlir::createCanonicalizerPass());
    optPM.addPass(mlir::createCSEPass());

    if (opts.enableOpt) {
      // The passes that justify lowering through affine at all: fusion needs
      // the affine dialect's guarantee that every access is an affine function
      // of the loop indices, which is exactly what scf loses.
      optPM.addPass(mlir::affine::createLoopFusionPass());
      optPM.addPass(mlir::affine::createAffineScalarReplacementPass());
    }
  }

  if (isLoweringToLLVM) {
    pm.addPass(mlir::toy::createLowerToLLVMPass());

    // Debug scopes. Upstream appends DIScopeForLLVMFuncOpPass unconditionally,
    // so that is what runs without -g, keeping -emit=mlir-llvm byte-identical
    // to upstream's. With -g our own pass takes its place, attaching a compile
    // unit that names the .toy file and carries our producer string instead of
    // one named "MLIR". Either way the scopes exist only so that the locations
    // the front end attached can be emitted as a DWARF line table.
    if (opts.debugInfo)
      pm.addPass(createAttachDebugInfoPass());
    else
      pm.addPass(mlir::LLVM::createDIScopeForLLVMFuncOpPass());
  }

  return mlir::success();
}

} // namespace toy
