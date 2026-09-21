//===- ObjectEmitter.cpp - llvm::Module -> native object file -------------===//
//
// Part of toy-mlir. The upstream Toy tutorial stops at the JIT, so there is no
// upstream code for this; it is written against LLVM's own codegen API.
//
//===----------------------------------------------------------------------===//
//
// Written from these headers in llvm-project/llvm/include/llvm:
//   MC/TargetRegistry.h      lookupTarget(), Target::createTargetMachine()
//   Target/TargetMachine.h   addPassesToEmitFile(), createDataLayout()
//   Target/TargetOptions.h   TargetOptions, and MCOptions.ABIName
//   IR/Module.h              setTargetTriple(Triple), setDataLayout()
//   IR/LegacyPassManager.h   legacy::PassManager
//   TargetParser/Host.h      getDefaultTargetTriple()
//   Support/CodeGen.h        CodeGenFileType
//   Support/TargetSelect.h   InitializeAll*()
//
// Two API details that shape the code below. Object emission still goes through
// the *legacy* pass manager: addPassesToEmitFile() takes a
// legacy::PassManagerBase, and no new-PM equivalent is exposed. And in this
// LLVM, triples are Triple objects rather than strings, and lookupTarget() and
// Module::setTargetTriple() both take one, so the string from the command line
// is parsed once, here.
//
//===----------------------------------------------------------------------===//

#include "toy/ObjectEmitter.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

namespace toy {

void ObjectEmitter::initializeTargets() {
  // Every backend this LLVM was built with, not just the native one, so that
  // --target= is limited by the LLVM build rather than by this function. The
  // JIT path needs only the native target, which is why it initializes less.
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllAsmParsers();
}

std::unique_ptr<llvm::TargetMachine>
ObjectEmitter::createTargetMachine(const std::string &triple,
                                   const std::string &features,
                                   const std::string &abi,
                                   std::string &error) {
  llvm::Triple targetTriple(triple.empty() ? llvm::sys::getDefaultTargetTriple()
                                           : triple);

  const llvm::Target *target =
      llvm::TargetRegistry::lookupTarget(targetTriple, error);
  if (!target)
    return nullptr;

  llvm::TargetOptions options;
  // Left empty, the ABI is derived from the feature string: on RISC-V, +d
  // implies lp64d. Setting it explicitly is what allows the two to disagree,
  // e.g. floating-point instructions with doubles passed in integer registers.
  options.MCOptions.ABIName = abi;

  // "generic" rather than a detected CPU: Toy emits no CPU-specific code, and
  // pinning the CPU would make the object file less portable than the triple
  // already implies. PIC because that is what a host toolchain expects to link
  // without complaint.
  return std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(
      targetTriple, /*CPU=*/"generic", features, options, llvm::Reloc::PIC_));
}

bool ObjectEmitter::emit(llvm::Module &module, llvm::TargetMachine &tm,
                         const std::string &filename, std::string &error) {
  // The module may have been tagged for the host during translation; retag it
  // for the machine we are actually emitting for. Mismatched data layouts are
  // accepted silently by the IR and produce wrong code, so this is not
  // redundant.
  module.setTargetTriple(tm.getTargetTriple());
  module.setDataLayout(tm.createDataLayout());

  std::error_code ec;
  llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);
  if (ec) {
    error = "could not open " + filename + ": " + ec.message();
    return false;
  }

  llvm::legacy::PassManager pass;
  if (tm.addPassesToEmitFile(pass, dest, /*DwoOut=*/nullptr,
                             llvm::CodeGenFileType::ObjectFile)) {
    error = "target machine cannot emit an object file";
    return false;
  }

  pass.run(module);
  dest.flush();
  return true;
}

} // namespace toy
