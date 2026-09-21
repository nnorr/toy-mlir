//===- DebugInfo.cpp - Debug metadata for the LLVM dialect ----------------===//
//
// Adapted from mlir/lib/Dialect/LLVMIR/Transforms/DIScopeForLLVMFuncOp.cpp in
// the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// What this pass does, and why it is so small: the locations are already there.
//
// MLIRGen attached a FileLineColLoc to every operation it built, and those
// locations survived inlining, shape inference, the affine lowering and the
// LLVM lowering. Translation to LLVM IR turns a location into a DILocation,
// but a DILocation is only emitted when it has a scope to belong to. So all
// that is missing at the end of the pipeline is the scope tree: one compile
// unit per module, one subprogram per function.
//
// The difference from upstream's DIScopeForLLVMFuncOpPass, which is what runs
// without -g: that pass names the compile unit "MLIR" and takes its file from
// the module's own location, which for Toy is unknown. This one names the
// compile unit after the .toy file the functions came from and records our own
// producer string, so the DWARF in the object file points back at Toy source.
//
// The emission kind is LineTablesOnly, which is an honest description of what
// exists: there is no DILocalVariable anywhere, because Toy variables do not
// survive as variables; they are SSA values, then memref slots. A debugger
// can step by Toy line and cannot print a Toy variable.
//
//===----------------------------------------------------------------------===//

#include "toy/DebugInfo.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Support/TypeID.h"

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/Path.h"

using namespace mlir;

namespace {

/// Finds a file location inside `loc`, looking through the wrappers a location
/// picks up on the way down the pipeline (names, fusions, call sites).
FileLineColLoc extractFileLoc(Location loc) {
  if (auto fileLoc = dyn_cast<FileLineColLoc>(loc))
    return fileLoc;
  if (auto nameLoc = dyn_cast<NameLoc>(loc))
    return extractFileLoc(nameLoc.getChildLoc());
  if (auto opaqueLoc = dyn_cast<OpaqueLoc>(loc))
    return extractFileLoc(opaqueLoc.getFallbackLocation());
  if (auto fusedLoc = dyn_cast<FusedLoc>(loc)) {
    for (Location nested : fusedLoc.getLocations())
      if (auto fileLoc = extractFileLoc(nested))
        return fileLoc;
  }
  if (auto callerLoc = dyn_cast<CallSiteLoc>(loc))
    return extractFileLoc(callerLoc.getCaller());
  return FileLineColLoc();
}

/// Splits a path into the (filename, directory) pair DWARF wants.
LLVM::DIFileAttr makeFileAttr(MLIRContext *context, StringRef path) {
  return LLVM::DIFileAttr::get(context, llvm::sys::path::filename(path),
                               llvm::sys::path::parent_path(path));
}

/// Gives `llvmFunc` a subprogram scope, unless it already has one.
///
/// The scope is attached by fusing it onto the function's own location: MLIR
/// carries debug metadata as attributes on locations rather than as a separate
/// structure, so "this function has a subprogram" is spelled as "this
/// function's location is a FusedLoc whose metadata is a DISubprogramAttr".
void addScopeToFunction(LLVM::LLVMFuncOp llvmFunc,
                        LLVM::DICompileUnitAttr compileUnitAttr) {
  Location loc = llvmFunc.getLoc();
  if (loc->findInstanceOf<FusedLocWith<LLVM::DISubprogramAttr>>())
    return;

  MLIRContext *context = llvmFunc->getContext();

  LLVM::DIFileAttr fileAttr;
  int64_t line = 1;
  if (FileLineColLoc fileLoc = extractFileLoc(loc)) {
    line = fileLoc.getLine();
    fileAttr = makeFileAttr(context, fileLoc.getFilename().getValue());
  } else {
    // printf, and anything else the lowering declared for us, has no Toy source
    // of its own; hang it off the compile unit's file.
    fileAttr = compileUnitAttr
                   ? compileUnitAttr.getFile()
                   : LLVM::DIFileAttr::get(context, "<unknown>", "");
  }

  // Toy has one type and no parameters worth describing at this level, so the
  // subroutine type is deliberately empty: enough for a line table, nothing
  // more.
  auto subroutineTypeAttr =
      LLVM::DISubroutineTypeAttr::get(context, llvm::dwarf::DW_CC_normal, {});

  // A declaration lives in someone else's compile unit, so it gets no compile
  // unit and no distinct id of its own; a definition gets both.
  DistinctAttr id;
  auto subprogramFlags = LLVM::DISubprogramFlags::Optimized;
  if (!llvmFunc.isExternal()) {
    id = DistinctAttr::create(UnitAttr::get(context));
    subprogramFlags = subprogramFlags | LLVM::DISubprogramFlags::Definition;
  } else {
    compileUnitAttr = {};
  }

  StringAttr funcNameAttr = llvmFunc.getNameAttr();
  auto subprogramAttr = LLVM::DISubprogramAttr::get(
      context, id, compileUnitAttr, fileAttr, funcNameAttr, funcNameAttr,
      fileAttr, /*line=*/line, /*scopeLine=*/line, subprogramFlags,
      subroutineTypeAttr, /*retainedNodes=*/{}, /*annotations=*/{});
  llvmFunc->setLoc(FusedLoc::get(context, {loc}, subprogramAttr));
}

/// Builds the nested scope an inlined frame needs, recursing through a chain of
/// call sites so that an inlined body reports the file it was written in.
Location getNestedLoc(Operation *op, LLVM::DIScopeAttr scopeAttr,
                      Location calleeLoc) {
  MLIRContext *context = op->getContext();

  LLVM::DIFileAttr calleeFileAttr;
  if (FileLineColLoc calleeFileLoc = extractFileLoc(calleeLoc))
    calleeFileAttr =
        makeFileAttr(context, calleeFileLoc.getFilename().getValue());
  else
    calleeFileAttr = LLVM::DIFileAttr::get(context, "<unknown>", "");

  auto lexicalBlockFileAttr = LLVM::DILexicalBlockFileAttr::get(
      context, scopeAttr, calleeFileAttr, /*discriminator=*/0);

  Location loc = calleeLoc;
  if (auto callSiteLoc = dyn_cast<CallSiteLoc>(calleeLoc))
    loc = getNestedLoc(op, lexicalBlockFileAttr, callSiteLoc.getCallee());
  return FusedLoc::get(context, {loc}, lexicalBlockFileAttr);
}

/// Gives operations that came from a call site, or from a different file than
/// the function containing them, a lexical block scope of their own. Without it
/// their line numbers would be attributed to the enclosing function's file.
void setLexicalBlockFileAttr(Operation *op) {
  Location opLoc = op->getLoc();

  if (auto callSiteLoc = dyn_cast<CallSiteLoc>(opLoc)) {
    auto funcOp = op->getParentOfType<LLVM::LLVMFuncOp>();
    if (!funcOp)
      return;
    auto funcOpLoc = llvm::dyn_cast_if_present<FusedLoc>(funcOp.getLoc());
    if (!funcOpLoc)
      return;
    auto scopeAttr = dyn_cast<LLVM::DISubprogramAttr>(funcOpLoc.getMetadata());
    if (!scopeAttr)
      return;
    op->setLoc(CallSiteLoc::get(
        getNestedLoc(op, scopeAttr, callSiteLoc.getCallee()),
        callSiteLoc.getCaller()));
    return;
  }

  auto funcOp = op->getParentOfType<LLVM::LLVMFuncOp>();
  if (!funcOp)
    return;

  FileLineColLoc opFileLoc = extractFileLoc(opLoc);
  FileLineColLoc funcFileLoc = extractFileLoc(funcOp.getLoc());
  if (!opFileLoc || !funcFileLoc)
    return;

  StringRef opFile = opFileLoc.getFilename().getValue();
  if (opFile == funcFileLoc.getFilename().getValue())
    return;

  auto funcOpLoc = llvm::dyn_cast_if_present<FusedLoc>(funcOp.getLoc());
  if (!funcOpLoc)
    return;
  auto scopeAttr = dyn_cast<LLVM::DISubprogramAttr>(funcOpLoc.getMetadata());
  if (!scopeAttr)
    return;

  MLIRContext *context = op->getContext();
  auto lexicalBlockFileAttr = LLVM::DILexicalBlockFileAttr::get(
      context, scopeAttr, makeFileAttr(context, opFile), /*discriminator=*/0);
  op->setLoc(FusedLoc::get(context, {opLoc}, lexicalBlockFileAttr));
}

/// Attaches a compile unit naming the Toy source, plus a subprogram per
/// function.
struct AttachDebugInfoPass
    : public PassWrapper<AttachDebugInfoPass, OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(AttachDebugInfoPass)

  explicit AttachDebugInfoPass(std::string producer)
      : producer(std::move(producer)) {}

  StringRef getArgument() const override { return "toy-attach-debug-info"; }
  StringRef getDescription() const override {
    return "Attach a DWARF compile unit and per-function subprograms so that "
           "the locations carried since MLIRGen become a line table";
  }

  void runOnOperation() override {
    ModuleOp module = getOperation();
    MLIRContext *context = &getContext();

    // The pass runs after the LLVM lowering, so this should be impossible; it
    // is the one thing that would silently produce a module with no metadata.
    if (!context->getLoadedDialect<LLVM::LLVMDialect>()) {
      emitError(module.getLoc(), "LLVM dialect is not loaded.");
      return signalPassFailure();
    }

    // Respect a compile unit someone else already attached, so that running
    // this after upstream's pass is not a way to get two of them.
    LLVM::DICompileUnitAttr compileUnitAttr;
    if (auto fused =
            module->getLoc()
                ->findInstanceOf<FusedLocWith<LLVM::DICompileUnitAttr>>()) {
      compileUnitAttr = fused.getMetadata();
    } else {
      compileUnitAttr = LLVM::DICompileUnitAttr::get(
          DistinctAttr::create(UnitAttr::get(context)),
          llvm::dwarf::DW_LANG_C, findSourceFile(module),
          StringAttr::get(context, producer),
          /*isOptimized=*/true, LLVM::DIEmissionKind::LineTablesOnly);
    }

    module.walk<WalkOrder::PreOrder>([&](Operation *op) {
      if (auto funcOp = dyn_cast<LLVM::LLVMFuncOp>(op))
        addScopeToFunction(funcOp, compileUnitAttr);
      else
        setLexicalBlockFileAttr(op);
    });
  }

private:
  /// The file to name the compile unit after.
  ///
  /// The module's own location is unknown in a Toy compilation, because MLIRGen
  /// builds it with an unknown loc, so the .toy path has to be recovered from
  /// the functions inside it. This is the one place where we do better
  /// than the upstream pass, whose compile unit ends up file-less.
  LLVM::DIFileAttr findSourceFile(ModuleOp module) {
    MLIRContext *context = module->getContext();
    if (FileLineColLoc moduleLoc = extractFileLoc(module.getLoc()))
      return makeFileAttr(context, moduleLoc.getFilename().getValue());

    LLVM::DIFileAttr found;
    module.walk([&](LLVM::LLVMFuncOp funcOp) {
      if (found)
        return WalkResult::interrupt();
      if (FileLineColLoc funcLoc = extractFileLoc(funcOp.getLoc()))
        found = makeFileAttr(context, funcLoc.getFilename().getValue());
      return WalkResult::advance();
    });
    return found ? found : LLVM::DIFileAttr::get(context, "<unknown>", "");
  }

  std::string producer;
};

} // namespace

namespace toy {

std::unique_ptr<mlir::Pass> createAttachDebugInfoPass(std::string producer) {
  return std::make_unique<AttachDebugInfoPass>(std::move(producer));
}

} // namespace toy
