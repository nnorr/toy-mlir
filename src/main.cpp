//===- main.cpp - The Toy compiler driver ---------------------------------===//
//
// Adapted from mlir/examples/toy/Ch7/toyc.cpp in the LLVM Project, under the
// Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Argument parsing, then one small function per stage. The compilation itself
// lives elsewhere: the pipeline in Pipeline.cpp, the exit from MLIR in
// Translate.cpp, the back ends in ObjectEmitter.cpp and Jit.cpp.
//
// Exit codes. The first group is upstream's, kept so that scripts written
// against toyc-ch7 behave the same way:
//
//    0  success
//    1  MLIR generation from the AST failed, or an AST dump could not be parsed
//    3  the input .mlir could not be parsed
//    4  the pass manager rejected its options, the pipeline, or a pass failed
//    5  -emit=ast was asked for, but the input is MLIR
//    6  the Toy source could not be parsed
//   -1  translation to LLVM IR failed, the JIT failed, or no action was given
//
// and the codes this driver adds for the options upstream does not have:
//
//    7  contradictory options
//    8  a token was malformed (deviation D1: the lexer keeps going, the driver
//       refuses to proceed, where upstream silently truncated the number)
//    9  object emission failed
//   10  the output file could not be opened
//
//===----------------------------------------------------------------------===//

#include "toy/ASTDumper.h"
#include "toy/Dialect.h"
#include "toy/Jit.h"
#include "toy/Lexer.h"
#include "toy/MLIRGen.h"
#include "toy/ObjectEmitter.h"
#include "toy/Parser.h"
#include "toy/Pipeline.h"
#include "toy/Translate.h"

#include "mlir/Dialect/Func/Extensions/AllExtensions.h"
#include "mlir/Dialect/LLVMIR/Transforms/InlinerInterfaceImpl.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/PassManager.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
// ObjectEmitter.h only forward-declares TargetMachine; destroying the
// unique_ptr it hands back needs the complete type.
#include "llvm/Target/TargetMachine.h"

#include <memory>
#include <string>

using namespace toy;
namespace cl = llvm::cl;

//===----------------------------------------------------------------------===//
// Options
//===----------------------------------------------------------------------===//

static cl::opt<std::string> inputFilename(cl::Positional,
                                          cl::desc("<input toy file>"),
                                          cl::init("-"),
                                          cl::value_desc("filename"));

namespace {
enum InputType { Toy, MLIR };
} // namespace
static cl::opt<enum InputType> inputType(
    "x", cl::init(Toy), cl::desc("Decided the kind of output desired"),
    cl::values(clEnumValN(Toy, "toy", "load the input file as a Toy source.")),
    cl::values(clEnumValN(MLIR, "mlir",
                          "load the input file as an MLIR file")));

namespace {
enum Action {
  None,
  DumpAST,
  DumpMLIR,
  DumpMLIRAffine,
  DumpMLIRLLVM,
  DumpLLVMIR,
  RunJIT
};
} // namespace
static cl::opt<enum Action> emitAction(
    "emit", cl::desc("Select the kind of output desired"),
    cl::values(clEnumValN(DumpAST, "ast", "output the AST dump")),
    cl::values(clEnumValN(DumpMLIR, "mlir", "output the MLIR dump")),
    cl::values(clEnumValN(DumpMLIRAffine, "mlir-affine",
                          "output the MLIR dump after affine lowering")),
    cl::values(clEnumValN(DumpMLIRLLVM, "mlir-llvm",
                          "output the MLIR dump after llvm lowering")),
    cl::values(clEnumValN(DumpLLVMIR, "llvm", "output the LLVM IR dump")),
    cl::values(
        clEnumValN(RunJIT, "jit",
                   "JIT the code and run it by invoking the main function")));

static cl::opt<bool> enableOpt("opt", cl::desc("Enable optimizations"));

static cl::opt<std::string>
    outputFilename("o", cl::desc("Write output to <file> instead of stderr"),
                   cl::value_desc("filename"));

static cl::opt<bool>
    emitObject("c", cl::desc("Emit a native object file (implies -emit=llvm's "
                             "pipeline, then runs the code generator)"));

static cl::opt<bool>
    alsoEmitLLVM("emit-llvm",
                 cl::desc("With -c, also write the LLVM IR beside the object"));

static cl::opt<bool>
    emitDebugInfo("g", cl::desc("Attach debug info, so the object file carries "
                                "a DWARF line table pointing at the .toy"));

static cl::opt<ASTDumper::Style> astStyle(
    "dump-ast-style", cl::init(ASTDumper::Style::Toy),
    cl::desc("Format for -emit=ast"),
    cl::values(clEnumValN(ASTDumper::Style::Toy, "toy",
                          "the upstream Toy tutorial's format")),
    cl::values(clEnumValN(ASTDumper::Style::Kaleidoscope, "kaleidoscope",
                          "the format used by the Kaleidoscope compiler")));

static cl::opt<std::string>
    targetTriple("target", cl::desc("Target triple for -c (default: host)"),
                 cl::value_desc("triple"));

static cl::opt<std::string>
    targetFeatures("mattr",
                   cl::desc("Target feature string for -c, e.g. +m,+d"),
                   cl::value_desc("features"));

static cl::opt<std::string> targetABI("mabi",
                                      cl::desc("Target ABI name for -c"),
                                      cl::value_desc("abi"));

static cl::opt<bool> printPipeline(
    "print-pipeline",
    cl::desc("Print the pass pipeline that would run, then exit"));

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

namespace {

/// Where textual output goes.
///
/// With no -o this is stderr, because that is where upstream's module->dump()
/// and its LLVM IR dump write; keeping the stream the same is what lets the
/// compat tests be a plain byte comparison against toyc-ch7. -o redirects to a
/// file and is the only reason this class exists.
class TextOutput {
public:
  explicit TextOutput(llvm::StringRef path) {
    if (path.empty())
      return;
    std::error_code ec;
    file = std::make_unique<llvm::raw_fd_ostream>(path, ec,
                                                  llvm::sys::fs::OF_None);
    if (ec) {
      llvm::errs() << "Could not open output file: " << ec.message() << "\n";
      file.reset();
      failed = true;
    }
  }

  bool hasError() const { return failed; }
  bool isFile() const { return static_cast<bool>(file); }
  llvm::raw_ostream &os() { return file ? *file : llvm::errs(); }

private:
  std::unique_ptr<llvm::raw_fd_ostream> file;
  bool failed = false;
};

/// A parsed Toy file, or the exit code explaining why there isn't one.
struct ParsedToy {
  std::unique_ptr<ModuleAST> ast;
  int error = 0;
};

} // namespace

/// Reads and parses the Toy input.
static ParsedToy parseToyInput() {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> fileOrErr =
      llvm::MemoryBuffer::getFileOrSTDIN(inputFilename);
  if (std::error_code ec = fileOrErr.getError()) {
    llvm::errs() << "Could not open input file: " << ec.message() << "\n";
    return {nullptr, 6};
  }

  // The AST copies every name and number it needs, so the buffer only has to
  // outlive the parse, not the tree.
  Lexer lexer((*fileOrErr)->getBuffer(), std::string(inputFilename));
  Parser parser(lexer);
  std::unique_ptr<ModuleAST> moduleAST = parser.parseModule();
  if (!moduleAST)
    return {nullptr, 6};

  // Deviation D1: a malformed token still produced a usable value so that
  // lexing could continue and report more than one problem. Refusing to go on
  // is the driver's call, not the lexer's.
  if (lexer.hadError())
    return {nullptr, 8};

  return {std::move(moduleAST), 0};
}

/// Which stage the options ask for.
static Stage selectStage() {
  if (emitObject)
    return Stage::Object;
  switch (emitAction) {
  case DumpAST:
    return Stage::AST;
  case DumpMLIR:
    return Stage::MLIR;
  case DumpMLIRAffine:
    return Stage::MLIRAffine;
  case DumpMLIRLLVM:
    return Stage::MLIRLLVM;
  case DumpLLVMIR:
    return Stage::LLVMIR;
  case RunJIT:
    return Stage::JIT;
  case None:
    break;
  }
  llvm_unreachable("no action; main() rejects this before asking");
}

/// The spelling of an -emit action, for diagnostics about the option itself.
static llvm::StringRef getActionName(Action action) {
  switch (action) {
  case None:
    return "<none>";
  case DumpAST:
    return "ast";
  case DumpMLIR:
    return "mlir";
  case DumpMLIRAffine:
    return "mlir-affine";
  case DumpMLIRLLVM:
    return "mlir-llvm";
  case DumpLLVMIR:
    return "llvm";
  case RunJIT:
    return "jit";
  }
  llvm_unreachable("unknown Action");
}

/// Rejects option combinations that cannot mean anything, rather than letting
/// one of them be silently ignored.
static int validateOptions(Stage stage) {
  if (emitObject && emitAction.getNumOccurrences() &&
      emitAction != DumpLLVMIR) {
    llvm::errs() << "-c cannot be combined with -emit="
                 << getActionName(emitAction)
                 << "; -c always compiles through to machine code\n";
    return 7;
  }
  if (astStyle.getNumOccurrences() && stage != Stage::AST) {
    llvm::errs() << "--dump-ast-style only applies to -emit=ast\n";
    return 7;
  }
  if (alsoEmitLLVM && !emitObject) {
    llvm::errs() << "--emit-llvm only applies with -c; use -emit=llvm to get "
                    "LLVM IR on its own\n";
    return 7;
  }
  if (emitDebugInfo && stage < Stage::MLIRLLVM) {
    llvm::errs() << "-g has no effect before the LLVM dialect; use it with -c, "
                    "-emit=llvm or -emit=mlir-llvm\n";
    return 7;
  }
  const bool hasTargetOption = targetTriple.getNumOccurrences() ||
                               targetFeatures.getNumOccurrences() ||
                               targetABI.getNumOccurrences();
  if (hasTargetOption && !emitObject) {
    llvm::errs() << "--target=/-mattr=/-mabi= only apply with -c\n";
    return 7;
  }
  return 0;
}

/// The dialects and extensions the context needs, exactly as upstream loads
/// them: the func extensions and the LLVM inliner interface are what let the
/// generic inliner work across the dialects Toy lowers into.
static mlir::DialectRegistry makeRegistry() {
  mlir::DialectRegistry registry;
  mlir::func::registerAllExtensions(registry);
  mlir::LLVM::registerInlinerInterface(registry);
  return registry;
}

/// The object file to write: -o if given, else the input's basename with .o.
static std::string getObjectPath() {
  if (!outputFilename.empty())
    return outputFilename;
  if (inputFilename == "-")
    return "a.o";
  llvm::SmallString<128> path(llvm::sys::path::stem(inputFilename));
  llvm::sys::path::replace_extension(path, ".o");
  return std::string(path);
}

//===----------------------------------------------------------------------===//
// Stages
//===----------------------------------------------------------------------===//

static int dumpAST() {
  if (inputType == InputType::MLIR) {
    llvm::errs() << "Can't dump a Toy AST when the input is MLIR\n";
    return 5;
  }

  ParsedToy parsed = parseToyInput();
  if (!parsed.ast) {
    // Upstream reports a failed parse as 1 on this path, and as 6 when it was
    // on the way to MLIR. Kept as-is rather than unified.
    return parsed.error == 6 ? 1 : parsed.error;
  }

  TextOutput out(outputFilename);
  if (out.hasError())
    return 10;

  ASTDumper dumper(out.os(), astStyle);
  dumper.dump(*parsed.ast);
  return 0;
}

/// Gets a module into `module`, from either a .toy or a .mlir input.
static int loadMLIR(mlir::MLIRContext &context,
                    mlir::OwningOpRef<mlir::ModuleOp> &module) {
  if (inputType != InputType::MLIR &&
      !llvm::StringRef(inputFilename).ends_with(".mlir")) {
    ParsedToy parsed = parseToyInput();
    if (!parsed.ast)
      return parsed.error;
    module = mlirGen(context, *parsed.ast);
    return !module ? 1 : 0;
  }

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> fileOrErr =
      llvm::MemoryBuffer::getFileOrSTDIN(inputFilename);
  if (std::error_code ec = fileOrErr.getError()) {
    llvm::errs() << "Could not open input file: " << ec.message() << "\n";
    return -1;
  }

  llvm::SourceMgr sourceMgr;
  sourceMgr.AddNewSourceBuffer(std::move(*fileOrErr), llvm::SMLoc());
  module = mlir::parseSourceFile<mlir::ModuleOp>(sourceMgr, &context);
  if (!module) {
    llvm::errs() << "Error can't load file " << inputFilename << "\n";
    return 3;
  }
  return 0;
}

static int loadAndProcessMLIR(mlir::MLIRContext &context,
                              mlir::OwningOpRef<mlir::ModuleOp> &module,
                              const PipelineOptions &opts) {
  if (int error = loadMLIR(context, module))
    return error;

  mlir::PassManager pm(module.get()->getName());

  // Before the pipeline is built, so that -mlir-print-ir-after-all and friends
  // apply to the passes added below.
  if (mlir::failed(mlir::applyPassManagerCLOptions(pm)))
    return 4;
  if (mlir::failed(buildPipeline(pm, opts)))
    return 4;
  if (mlir::failed(pm.run(*module)))
    return 4;
  return 0;
}

static int emitLLVMIR(mlir::ModuleOp module, const PipelineOptions &opts) {
  llvm::LLVMContext llvmContext;
  std::unique_ptr<llvm::Module> llvmModule =
      translateToLLVMIR(module, llvmContext, opts.enableOpt);
  if (!llvmModule)
    return -1;

  TextOutput out(outputFilename);
  if (out.hasError())
    return 10;
  out.os() << *llvmModule << "\n";
  return 0;
}

static int emitObjectFile(mlir::ModuleOp module, const PipelineOptions &opts) {
  ObjectEmitter::initializeTargets();

  std::string error;
  std::unique_ptr<llvm::TargetMachine> tm = ObjectEmitter::createTargetMachine(
      targetTriple, targetFeatures, targetABI, error);
  if (!tm) {
    llvm::errs() << "Could not create TargetMachine: " << error << "\n";
    return 9;
  }

  // The machine is passed down so the module is tagged for the target being
  // emitted for rather than for the host.
  llvm::LLVMContext llvmContext;
  std::unique_ptr<llvm::Module> llvmModule =
      translateToLLVMIR(module, llvmContext, opts.enableOpt, tm.get());
  if (!llvmModule)
    return -1;

  const std::string objectPath = getObjectPath();

  if (alsoEmitLLVM) {
    llvm::SmallString<128> irPath(objectPath);
    llvm::sys::path::replace_extension(irPath, ".ll");
    std::error_code ec;
    llvm::raw_fd_ostream irFile(irPath, ec, llvm::sys::fs::OF_None);
    if (ec) {
      llvm::errs() << "Could not open output file: " << ec.message() << "\n";
      return 10;
    }
    irFile << *llvmModule << "\n";
  }

  if (!ObjectEmitter::emit(*llvmModule, *tm, objectPath, error)) {
    llvm::errs() << error << "\n";
    return 9;
  }
  return 0;
}

static int doPrintPipeline(mlir::MLIRContext &context,
                          const PipelineOptions &opts) {
  mlir::PassManager pm(&context, mlir::ModuleOp::getOperationName());
  if (mlir::failed(buildPipeline(pm, opts)))
    return 4;
  pm.printAsTextualPipeline(llvm::outs());
  llvm::outs() << "\n";
  return 0;
}

//===----------------------------------------------------------------------===//
// main
//===----------------------------------------------------------------------===//

int main(int argc, char **argv) {
  // These add the -mlir-* options the tests use to observe the compiler:
  // -mlir-print-debuginfo, -mlir-print-ir-after-all, --mlir-pass-statistics.
  mlir::registerAsmPrinterCLOptions();
  mlir::registerMLIRContextCLOptions();
  mlir::registerPassManagerCLOptions();

  cl::ParseCommandLineOptions(argc, argv, "toy compiler\n");

  if (emitAction == Action::None && !emitObject && !printPipeline) {
    llvm::errs() << "No action specified (parsing only?), use -emit=<action>\n";
    return -1;
  }

  PipelineOptions opts;
  opts.stage = selectStage();
  opts.enableOpt = enableOpt;
  opts.debugInfo = emitDebugInfo;

  if (int error = validateOptions(opts.stage))
    return error;

  // Asks what the pipeline would be, so it needs the dialect (to resolve the
  // nesting anchors) but no input file.
  if (printPipeline) {
    mlir::MLIRContext context(makeRegistry());
    context.getOrLoadDialect<mlir::toy::ToyDialect>();
    return doPrintPipeline(context, opts);
  }

  // The AST never reaches MLIR, so this path builds no context at all.
  if (opts.stage == Stage::AST)
    return dumpAST();

  mlir::MLIRContext context(makeRegistry());
  context.getOrLoadDialect<mlir::toy::ToyDialect>();

  mlir::OwningOpRef<mlir::ModuleOp> module;
  if (int error = loadAndProcessMLIR(context, module, opts))
    return error;

  // Stages that stop inside MLIR: print the module and be done.
  if (opts.stage <= Stage::MLIRLLVM) {
    TextOutput out(outputFilename);
    if (out.hasError())
      return 10;
    if (out.isFile())
      module->print(out.os(), mlir::OpPrintingFlags());
    else
      module->dump(); // stderr, plus the trailing newline, as upstream
    if (out.isFile())
      out.os() << "\n";
    return 0;
  }

  if (opts.stage == Stage::LLVMIR)
    return emitLLVMIR(*module, opts);
  if (opts.stage == Stage::Object)
    return emitObjectFile(*module, opts);
  if (opts.stage == Stage::JIT)
    return runJit(*module, opts.enableOpt);

  llvm_unreachable("every stage is handled above");
}
