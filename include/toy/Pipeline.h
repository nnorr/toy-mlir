//===- Pipeline.h - The Toy compilation pipeline --------------------------===//
//
// Part of toy-mlir. Upstream builds this inline in toyc.cpp.
//
//===----------------------------------------------------------------------===//
//
// One place that answers "which passes run, in what order, nested under what".
//
// Upstream's toyc.cpp interleaves this with argument parsing and file loading,
// and each chapter's copy differs slightly. Pulling it out makes the pipeline a
// value the driver configures and the tests can inspect: `--print-pipeline`
// prints the resulting pass pipeline as a string without compiling anything.
//
// The Stage order is load-bearing. Each stage is a superset of the previous
// one, so the pipeline is built with comparisons (`stage >= Stage::MLIRAffine`)
// rather than a switch, exactly as upstream does with its Action enum. The
// emitted IR is byte-identical to upstream's at each stage (tests/compat).
//
//===----------------------------------------------------------------------===//

#ifndef TOY_PIPELINE_H
#define TOY_PIPELINE_H

#include "mlir/Support/LLVM.h"

#include "llvm/ADT/StringRef.h"

namespace mlir {
class PassManager;
} // namespace mlir

namespace toy {

/// How far to compile. Ordered: later stages include every earlier stage's
/// passes.
enum class Stage {
  AST,        ///< Parse only, print the tree. No MLIR involved.
  MLIR,       ///< Toy dialect. With -opt: canonicalize, inline, infer shapes.
  MLIRAffine, ///< Affine + arith + memref + the surviving toy.print.
  MLIRLLVM,   ///< The LLVM dialect, nothing else.
  LLVMIR,     ///< llvm::Module, out of MLIR entirely.
  Object,     ///< A native .o, via TargetMachine.
  JIT,        ///< Compiled and run in this process.
};

/// The name accepted on the command line for a stage ("mlir-affine", "jit").
llvm::StringRef getStageName(Stage stage);

struct PipelineOptions {
  Stage stage = Stage::MLIR;

  /// -opt: run the optimization passes that are not required for correctness.
  /// Lowering to affine or beyond implies the Toy-level ones regardless,
  /// because shape inference is a prerequisite for lowering at all.
  bool enableOpt = false;

  /// -g: attach debug metadata during the LLVM-dialect stage so the object
  /// file carries a DWARF line table pointing back at the .toy source.
  bool debugInfo = false;
};

/// Populates `pm` with the passes needed to reach `opts.stage`.
///
/// Fails only if the pass manager rejects the pipeline; an empty pipeline (for
/// Stage::MLIR without -opt) is a success.
mlir::LogicalResult buildPipeline(mlir::PassManager &pm,
                                 const PipelineOptions &opts);

} // namespace toy

#endif // TOY_PIPELINE_H
