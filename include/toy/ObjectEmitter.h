//===- ObjectEmitter.h - llvm::Module -> native object file ---------------===//
//
// Part of toy-mlir. Not present in the upstream tutorial, which stops at the
// JIT. Written against LLVM's TargetMachine API; the idea of factoring object
// emission into its own module comes from 06_llvm_tutorial, the code does not.
//
//===----------------------------------------------------------------------===//
//
// Why bother, when -emit=jit already runs the program: an object file is the
// output you can inspect and link. `toyc prog.toy -c -o prog.o` followed by
// `clang prog.o -o prog` produces a native executable whose printf output
// matches -emit=jit, which is the end-to-end check that the lowering is real
// and not an artifact of the JIT's environment. It is also what makes the
// DWARF from -g inspectable with readelf.
//
//===----------------------------------------------------------------------===//

#ifndef TOY_OBJECTEMITTER_H
#define TOY_OBJECTEMITTER_H

#include <memory>
#include <string>

namespace llvm {
class Module;
class TargetMachine;
} // namespace llvm

namespace toy {

/// Emits object files through LLVM's TargetMachine.
///
/// Static functions rather than an object: there is no state worth carrying
/// between the three steps, and the driver needs the TargetMachine itself in
/// order to tag the module with the right data layout before translation.
class ObjectEmitter {
public:
  /// Registers every backend this LLVM was built with, so --target= accepts any
  /// triple rather than a hardcoded list.
  static void initializeTargets();

  /// Builds a TargetMachine for `triple`, or the host if `triple` is empty.
  ///
  /// `features` is an LLVM feature string ("+m,+f,+d"), `abi` a target ABI name
  /// ("lp64d"); both may be empty for the target's defaults. Returns null and
  /// sets `error` on failure.
  static std::unique_ptr<llvm::TargetMachine>
  createTargetMachine(const std::string &triple, const std::string &features,
                      const std::string &abi, std::string &error);

  /// Writes `module` to `filename` as a relocatable object file. Sets the
  /// module's triple and data layout from `tm` first, since a module tagged for
  /// one target cannot be emitted for another.
  static bool emit(llvm::Module &module, llvm::TargetMachine &tm,
                   const std::string &filename, std::string &error);
};

} // namespace toy

#endif // TOY_OBJECTEMITTER_H
