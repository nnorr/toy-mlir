//===- Translate.h - LLVM dialect -> llvm::Module -------------------------===//
//
// Part of toy-mlir. Upstream does this inline in toyc.cpp's dumpLLVMIR().
//
//===----------------------------------------------------------------------===//
//
// The exit from MLIR. Everything before this point is a pass over MLIR IR
// ("MLIR in, MLIR out"); this is a translation, and the result is no longer
// inspectable by MLIR tooling.
//
//===----------------------------------------------------------------------===//

#ifndef TOY_TRANSLATE_H
#define TOY_TRANSLATE_H

#include <memory>

namespace llvm {
class LLVMContext;
class Module;
class TargetMachine;
} // namespace llvm

namespace mlir {
class ModuleOp;
} // namespace mlir

namespace toy {

/// Translates an LLVM-dialect module to LLVM IR, sets its triple and data
/// layout, and optionally optimizes it.
///
/// `targetMachine` may be null, in which case the host is detected. Passing one
/// matters for cross-compilation: the data layout the module is tagged with has
/// to be the one the object file will be emitted for.
///
/// With `enableOpt`, LLVM's own -O3 pipeline runs here. On the tutorial's
/// examples that is enough to constant-fold the whole program into a handful of
/// printf calls, which is worth showing next to the -O0 output.
///
/// Returns null after emitting a diagnostic on failure. The caller owns the
/// module, and must keep `llvmContext` alive at least as long.
std::unique_ptr<llvm::Module>
translateToLLVMIR(mlir::ModuleOp module, llvm::LLVMContext &llvmContext,
                  bool enableOpt, llvm::TargetMachine *targetMachine = nullptr);

} // namespace toy

#endif // TOY_TRANSLATE_H
