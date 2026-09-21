//===- Jit.h - Run a Toy module in this process ---------------------------===//
//
// Adapted from mlir/examples/toy/Ch7's runJit(), under the Apache License v2.0
// with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// mlir::ExecutionEngine wraps LLVM's ORC JIT and takes an MLIR module directly:
// it translates to LLVM IR, compiles, and resolves `printf` against this
// process. The object-file path differs very little from this one: both
// translate the same LLVM-dialect module, and only what happens to the result
// differs.
//
// This is not what a production runtime looks like. IREE, for instance, has the
// compiler produce target executables plus a VM/HAL program, and a separate
// runtime manages buffers, devices and scheduling. Toy's JIT is the simplest
// thing that can execute the module in-process.
//
//===----------------------------------------------------------------------===//

#ifndef TOY_JIT_H
#define TOY_JIT_H

namespace mlir {
class ModuleOp;
} // namespace mlir

namespace toy {

/// JIT-compiles `module` and invokes its `main`, returning a process exit code
/// (0 on success). With `enableOpt`, LLVM's -O3 pipeline runs inside the engine
/// before execution.
///
/// Deviation D8: a module with no `main` (compile an empty .toy file to see it)
/// reports why and exits nonzero. Upstream prints "JIT invocation failed" and
/// then aborts with "Program aborted due to an unhandled Error", because the
/// llvm::Error from invokePacked() is tested but never consumed.
int runJit(mlir::ModuleOp module, bool enableOpt);

} // namespace toy

#endif // TOY_JIT_H
