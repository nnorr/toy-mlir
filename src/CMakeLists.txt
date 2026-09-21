#===----------------------------------------------------------------------===#
# Four libraries, layered so each one's dependencies are visible here.
#
#   ToyFrontend  source text -> AST.            No MLIR, no LLVM IR.
#   ToyDialect   the Toy IR: ops, types, interfaces, folders.
#   ToyPasses    transformations and lowerings over that IR.
#   ToyCodegen   AST -> IR (MLIRGen), pipeline, LLVM IR, object file, JIT.
#
# Nothing links upward. ToyFrontend has no idea MLIR exists, which is what lets
# tests/FrontendTests link it on its own; ToyDialect knows nothing about
# lowering; ToyPasses knows nothing about the driver.
#
# These are plain add_library() targets rather than add_mlir_library(): the
# MLIR helper also registers into MLIR's global library bookkeeping, which an
# out-of-tree project neither needs nor benefits from, and which makes a broken
# link harder to read.
#===----------------------------------------------------------------------===#

# The declarative rewrite rules (DRR) compile to a pattern set in ToyCombine.inc
# next to this build directory, so dialect/ToyCombine.cpp can include it.
set(LLVM_TARGET_DEFINITIONS dialect/ToyCombine.td)
mlir_tablegen(ToyCombine.inc -gen-rewriters)
add_public_tablegen_target(ToyCombineIncGen)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

#===----------------------------------------------------------------------===#
# ToyFrontend
#===----------------------------------------------------------------------===#

add_library(ToyFrontend STATIC
  Lexer.cpp
  Parser.cpp
  ASTDumper.cpp
)
llvm_update_compile_flags(ToyFrontend)
# Only the header-only side of LLVM (StringRef, ArrayRef, raw_ostream). Linking
# LLVMSupport is still required for raw_ostream's out-of-line parts.
target_link_libraries(ToyFrontend PUBLIC LLVMSupport)

#===----------------------------------------------------------------------===#
# ToyDialect
#===----------------------------------------------------------------------===#

add_library(ToyDialect STATIC
  dialect/ToyDialect.cpp
  dialect/StructType.cpp
  dialect/Ops.cpp
  dialect/Folders.cpp
  dialect/Interfaces.cpp
  dialect/ToyCombine.cpp
)
llvm_update_compile_flags(ToyDialect)
add_dependencies(ToyDialect ToyOpsIncGen ToyShapeInferenceInterfaceIncGen
                            ToyCombineIncGen)
target_link_libraries(ToyDialect PUBLIC
  MLIRIR
  MLIRSupport
  MLIRCallInterfaces
  MLIRCastInterfaces
  MLIRControlFlowInterfaces
  MLIRFunctionInterfaces
  MLIRSideEffectInterfaces
  MLIRTransformUtils          # InliningUtils, for ToyInlinerInterface
)

#===----------------------------------------------------------------------===#
# ToyPasses
#===----------------------------------------------------------------------===#

add_library(ToyPasses STATIC
  passes/ShapeInference.cpp
  passes/LowerToAffine.cpp
  passes/LowerToLLVM.cpp
)
llvm_update_compile_flags(ToyPasses)
add_dependencies(ToyPasses ToyOpsIncGen ToyShapeInferenceInterfaceIncGen)
target_link_libraries(ToyPasses PUBLIC
  ToyDialect
  MLIRAnalysis
  MLIRPass
  MLIRTransforms
  # Dialects the lowerings produce.
  MLIRAffineDialect
  MLIRArithDialect
  MLIRFuncDialect
  MLIRMemRefDialect
  MLIRSCFDialect
  MLIRLLVMDialect
  # Conversion pattern sets the LLVM lowering reuses. Toy only writes one
  # pattern of its own (toy.print); everything else is borrowed.
  MLIRLLVMCommonConversion
  MLIRAffineToStandard
  MLIRSCFToControlFlow
  MLIRArithToLLVM
  MLIRControlFlowToLLVM
  MLIRFuncToLLVM
  MLIRMemRefToLLVM
)

#===----------------------------------------------------------------------===#
# ToyCodegen
#===----------------------------------------------------------------------===#

add_library(ToyCodegen STATIC
  MLIRGen.cpp
  Pipeline.cpp
  Translate.cpp
  DebugInfo.cpp
  ObjectEmitter.cpp
  Jit.cpp
)
llvm_update_compile_flags(ToyCodegen)
add_dependencies(ToyCodegen ToyOpsIncGen ToyShapeInferenceInterfaceIncGen)
target_link_libraries(ToyCodegen PUBLIC
  ToyFrontend
  ToyDialect
  ToyPasses
  MLIRParser
  MLIRPass
  MLIRTransforms
  MLIRAffineTransforms        # loop fusion, scalar replacement (-opt)
  MLIRLLVMIRTransforms        # DIScopeForLLVMFuncOpPass (-g)
  MLIRFuncAllExtensions       # func extensions the inliner needs
  MLIRExecutionEngine
  MLIRTargetLLVMIRExport
  MLIRBuiltinToLLVMIRTranslation
  MLIRLLVMToLLVMIRTranslation
)

#===----------------------------------------------------------------------===#
# toyc
#===----------------------------------------------------------------------===#

# AllTargets* rather than the native target alone, so --target= accepts any
# triple this LLVM was built with -- same reasoning as the Kaleidoscope repo's
# ObjectEmitter. With LLVM_TARGETS_TO_BUILD=host that resolves to one backend.
llvm_map_components_to_libnames(LLVM_LIBS
  Core
  OrcJIT
  Passes
  Target
  AllTargetsCodeGens
  AllTargetsAsmParsers
  AllTargetsDescs
  AllTargetsInfos
)

# PARTIAL_SOURCES_INTENDED: LLVM's build system otherwise insists that one
# target in a directory lists every source in it, which is incompatible with
# the four libraries above living beside main.cpp.
add_llvm_executable(toyc PARTIAL_SOURCES_INTENDED main.cpp)
llvm_update_compile_flags(toyc)
target_link_libraries(toyc PRIVATE
  ToyCodegen
  ToyPasses
  ToyDialect
  ToyFrontend
  ${LLVM_LIBS}
)
