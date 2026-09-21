//===- Jit.cpp - Run a Toy module in this process -------------------------===//
//
// Adapted from runJit() in mlir/examples/toy/Ch7/toyc.cpp in the LLVM Project,
// under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "toy/Jit.h"

#include "mlir/ExecutionEngine/ExecutionEngine.h"
#include "mlir/ExecutionEngine/OptUtils.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"

#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

namespace toy {

int runJit(mlir::ModuleOp module, bool enableOpt) {
  // Only the native target: this code is compiled for the machine it is running
  // on, unlike the object-file path, which may be asked for any triple.
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();

  // The engine translates the module itself, so the same translation interfaces
  // the object path needs must be registered here too.
  mlir::registerBuiltinDialectTranslation(*module->getContext());
  mlir::registerLLVMDialectTranslation(*module->getContext());

  auto optPipeline = mlir::makeOptimizingTransformer(
      /*optLevel=*/enableOpt ? 3 : 0, /*sizeLevel=*/0,
      /*targetMachine=*/nullptr);

  mlir::ExecutionEngineOptions engineOptions;
  engineOptions.transformer = optPipeline;

  // Creation is where compilation happens: the engine is eager, so a failure
  // here is a compile error rather than a run-time one.
  auto maybeEngine = mlir::ExecutionEngine::create(module, engineOptions);
  if (!maybeEngine) {
    // Upstream asserts instead. Reporting is friendlier and, on the success
    // path, indistinguishable.
    llvm::errs() << "Failed to construct an execution engine: "
                 << llvm::toString(maybeEngine.takeError()) << "\n";
    return -1;
  }
  auto &engine = maybeEngine.get();

  // invokePacked rather than a typed call: `main` takes no arguments and
  // returns nothing, and the packed form needs no signature at all.
  // Anything the program prints reaches stdout through the process's printf.
  //
  // The Error must be bound to a name and then consumed. Testing the temporary
  // directly, as `if (engine->invokePacked("main"))`, destroys it at the end of
  // the condition, and an llvm::Error that was checked but never consumed
  // aborts the process on destruction, so the diagnostic below would never be
  // reached. Upstream binds it to a variable, which is why its message prints,
  // and then still aborts at scope exit because it never consumes it; we
  // consume it with toString() and exit normally (deviation D8).
  if (llvm::Error err = engine->invokePacked("main")) {
    llvm::errs() << "JIT invocation failed: " << llvm::toString(std::move(err))
                 << "\n";
    return -1;
  }

  return 0;
}

} // namespace toy
