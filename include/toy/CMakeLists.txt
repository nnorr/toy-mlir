#===----------------------------------------------------------------------===#
# TableGen: the ODS definitions that generate the dialect's C++.
#
# Outputs land in ${PROJECT_BINARY_DIR}/include/toy/, which the root
# CMakeLists puts on the include path, so sources include them as
# "toy/Ops.h.inc" exactly like the hand-written headers next to them.
#
# add_public_tablegen_target() creates the target other libraries name in
# DEPENDS; without that dependency a parallel build races the generator.
#===----------------------------------------------------------------------===#

# Operations and the dialect itself. Upstream Toy hand-rolls these four calls
# instead of add_mlir_dialect() because the dialect predates that helper; we
# keep them explicit so each generated file is visible in the build.
set(LLVM_TARGET_DEFINITIONS Ops.td)
mlir_tablegen(Ops.h.inc -gen-op-decls)
mlir_tablegen(Ops.cpp.inc -gen-op-defs)
mlir_tablegen(Dialect.h.inc -gen-dialect-decls)
mlir_tablegen(Dialect.cpp.inc -gen-dialect-defs)
add_public_tablegen_target(ToyOpsIncGen)

# The one op interface Toy defines itself. Everything else it uses
# (CallOpInterface, CastOpInterface, FunctionOpInterface) comes from MLIR.
set(LLVM_TARGET_DEFINITIONS ShapeInferenceInterface.td)
mlir_tablegen(ShapeInferenceOpInterfaces.h.inc -gen-op-interface-decls)
mlir_tablegen(ShapeInferenceOpInterfaces.cpp.inc -gen-op-interface-defs)
add_public_tablegen_target(ToyShapeInferenceInterfaceIncGen)

# `ninja mlir-doc` renders the dialect reference from the same ODS records that
# generate the C++, so the docs cannot drift from the implementation.
add_mlir_doc(Ops ToyOps toy/ -gen-op-doc)
add_mlir_doc(Ops ToyDialect toy/ -gen-dialect-doc)
