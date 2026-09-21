//===- MLIRGen.h - AST -> Toy dialect IR ----------------------------------===//
//
// Adapted from mlir/examples/toy/Ch7 in the LLVM Project, under the Apache
// License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The one-way door from the front end into MLIR.
//
// Only the entry point is public: the implementation class in src/MLIRGen.cpp
// is an ASTVisitor<MLIRGenImpl, mlir::FailureOr<mlir::Value>>, so the tree walk
// is shared with the AST dumper (see ASTVisitor.h) while the meaning of each
// node (what op to build, which types to give it) lives here. Statements
// (var decls, return, print) yield success with a null Value; expressions yield
// the value they defined; anything that failed yields failure, after emitting a
// diagnostic.
//
// This header deliberately forward-declares its MLIR types instead of
// including them: the driver needs the declaration, and keeping the include out
// of the header keeps the front end's translation units free of MLIR.
//
//===----------------------------------------------------------------------===//

#ifndef TOY_MLIRGEN_H
#define TOY_MLIRGEN_H

namespace mlir {
class MLIRContext;
template <typename OpTy> class OwningOpRef;
class ModuleOp;
} // namespace mlir

namespace toy {
class ModuleAST;

/// Emits Toy dialect IR for `moduleAST`, or a null module if generation failed
/// (in which case a diagnostic has already been emitted through `context`).
///
/// The result is verified before it is returned: a generator bug shows up here
/// rather than as a confusing failure three passes later.
mlir::OwningOpRef<mlir::ModuleOp> mlirGen(mlir::MLIRContext &context,
                                          ModuleAST &moduleAST);

} // namespace toy

#endif // TOY_MLIRGEN_H
