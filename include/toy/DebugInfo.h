//===- DebugInfo.h - Debug metadata for the LLVM dialect ------------------===//
//
// Part of toy-mlir. Upstream uses LLVM::createDIScopeForLLVMFuncOpPass()
// directly and notes that proper emission from the front end is future work.
//
//===----------------------------------------------------------------------===//
//
// Where debug information comes from in an MLIR pipeline.
//
// In the Kaleidoscope compiler (06_llvm_tutorial/src/DebugInfo.cpp) debug info
// is built by hand while emitting IR: a DIBuilder, a DICompileUnit, a
// DISubprogram per function, a DILocalVariable per parameter, and an explicit
// SetCurrentDebugLocation before each expression.
//
// Here nothing of that kind happens in the front end. Every operation carries
// an mlir::Location from the moment MLIRGen builds it, and those locations
// survive inlining, shape inference and both lowerings on their own. What is
// missing at the end is only the *scope* structure DWARF requires: a compile
// unit and a subprogram for the locations to hang from. This pass attaches
// those, just before translation to LLVM IR.
//
// The consequence, and the honest limit of the comparison: Toy gets line tables
// and no variable information. `readelf --debug-dump=decodedline` shows the
// .toy lines; `info locals` in a debugger shows nothing, because no
// DILocalVariable is ever created. See docs/12-debug-info-and-objects.md.
//
//===----------------------------------------------------------------------===//

#ifndef TOY_DEBUGINFO_H
#define TOY_DEBUGINFO_H

#include <memory>
#include <string>

namespace mlir {
class Pass;
} // namespace mlir

namespace toy {

/// Creates a pass that gives every LLVM-dialect function a debug scope, so that
/// translation to LLVM IR emits a DWARF line table.
///
/// `producer` is recorded in the compile unit (DW_AT_producer).
std::unique_ptr<mlir::Pass>
createAttachDebugInfoPass(std::string producer = "toyc (toy-mlir)");

} // namespace toy

#endif // TOY_DEBUGINFO_H
