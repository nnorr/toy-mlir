#===----------------------------------------------------------------------===#
# toy-mlir --- a modular, out-of-tree rebuild of the MLIR Toy tutorial.
#
# Unlike the in-tree tutorial (mlir/examples/toy/Ch1..Ch7, one executable per
# chapter) this is a single `toyc` built out-of-tree against an installed or
# built MLIR. Point CMake at the MLIR you want:
#
#   cmake -S . -B build -G Ninja \
#     -DMLIR_DIR=<llvm-build>/lib/cmake/mlir \
#     -DLLVM_DIR=<llvm-build>/lib/cmake/llvm
#
# MLIRConfig.cmake brings LLVM with it, so LLVM_DIR is only needed when the two
# live in different prefixes.
#===----------------------------------------------------------------------===#

cmake_minimum_required(VERSION 3.20.0)
project(toy-mlir LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to conform to")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(MLIR REQUIRED CONFIG)

message(STATUS "Using MLIRConfig.cmake in: ${MLIR_DIR}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "LLVM version: ${LLVM_PACKAGE_VERSION}")

# Where our own binaries and libraries land. add_llvm_executable() and
# add_mlir_library() read these rather than CMAKE_*_OUTPUT_DIRECTORY.
set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/bin)
set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/lib)
set(MLIR_BINARY_DIR ${CMAKE_BINARY_DIR})

list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")

include(TableGen)   # tablegen(), add_public_tablegen_target()
include(AddLLVM)    # add_llvm_executable(), llvm_update_compile_flags()
include(AddMLIR)    # mlir_tablegen(), add_mlir_library()

# Matches our flags (RTTI/EH, warnings, visibility) to the LLVM we link
# against. Skipping this is the usual cause of ODR/ABI surprises out-of-tree.
include(HandleLLVMOptions)

# TableGen resolves `include "toy/..."` through the directory's
# INCLUDE_DIRECTORIES property, so our own include dir has to be visible here,
# not only to the compiler.
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${MLIR_INCLUDE_DIRS})
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_BINARY_DIR}/include)
link_directories(${LLVM_BUILD_LIBRARY_DIR})
add_definitions(${LLVM_DEFINITIONS})

if(NOT MLIR_ENABLE_EXECUTION_ENGINE)
  message(FATAL_ERROR
    "This MLIR build has no ExecutionEngine; -emit=jit cannot be built. "
    "Rebuild MLIR with -DMLIR_ENABLE_EXECUTION_ENGINE=ON.")
endif()

add_subdirectory(include/toy)
add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
