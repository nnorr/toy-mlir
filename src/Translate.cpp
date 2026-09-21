//===- Translate.cpp - LLVM dialect -> llvm::Module -----------------------===//
//
// Adapted from dumpLLVMIR() in mlir/examples/toy/Ch7/toyc.cpp in the LLVM
// Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "toy/Translate.h"

#include "mlir/ExecutionEngine/ExecutionEngine.h"
#include "mlir/ExecutionEngine/OptUtils.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"

#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

namespace toy {

std::unique_ptr<llvm::Module>
translateToLLVMIR(mlir::ModuleOp module, llvm::LLVMContext &llvmContext,
                  bool enableOpt, llvm::TargetMachine *targetMachine) {
  // Translation is opt-in per dialect: each dialect that can become LLVM IR
  // registers an interface saying how. Without these two registrations the
  // translation below fails on the first operation it meets, even though the
  // module is already entirely LLVM dialect.
  mlir::registerBuiltinDialectTranslation(*module->getContext());
  mlir::registerLLVMDialectTranslation(*module->getContext());

  std::unique_ptr<llvm::Module> llvmModule =
      mlir::translateModuleToLLVMIR(module, llvmContext);
  if (!llvmModule) {
    llvm::errs() << "Failed to emit LLVM IR\n";
    return nullptr;
  }

  // Without a machine from the caller, tag the module for the host. Kept alive
  // until after the module has been tagged, since the data layout is copied out
  // of it.
  std::unique_ptr<llvm::TargetMachine> hostMachine;
  if (!targetMachine) {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    auto tmBuilderOrError = llvm::orc::JITTargetMachineBuilder::detectHost();
    if (!tmBuilderOrError) {
      llvm::consumeError(tmBuilderOrError.takeError());
      llvm::errs() << "Could not create JITTargetMachineBuilder\n";
      return nullptr;
    }

    auto tmOrError = tmBuilderOrError->createTargetMachine();
    if (!tmOrError) {
      llvm::consumeError(tmOrError.takeError());
      llvm::errs() << "Could not create TargetMachine\n";
      return nullptr;
    }

    hostMachine = std::move(tmOrError.get());
    targetMachine = hostMachine.get();
  }

  // An untagged module carries no data layout, and LLVM then computes struct
  // layouts and alignments from defaults that need not match the target the
  // object file is for.
  mlir::ExecutionEngine::setupTargetTripleAndDataLayout(llvmModule.get(),
                                                        targetMachine);

  // LLVM's own pipeline, which is a different thing from the MLIR passes that
  // ran earlier: at -O3 it constant-folds the tutorial's examples down to a
  // handful of printf calls, because by this point the loop nests are ordinary
  // scalar code with known bounds.
  auto optPipeline = mlir::makeOptimizingTransformer(
      /*optLevel=*/enableOpt ? 3 : 0, /*sizeLevel=*/0,
      /*targetMachine=*/nullptr);
  if (auto err = optPipeline(llvmModule.get())) {
    llvm::errs() << "Failed to optimize LLVM IR "
                 << llvm::toString(std::move(err)) << "\n";
    return nullptr;
  }

  return llvmModule;
}

} // namespace toy
