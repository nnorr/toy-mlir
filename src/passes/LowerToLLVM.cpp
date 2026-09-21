//===- LowerToLLVM.cpp - Full lowering to the LLVM dialect ----------------===//
//
// Adapted from mlir/examples/toy/Ch7/mlir/LowerToLLVM.cpp in the LLVM Project,
// under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The second lowering: everything left becomes the LLVM dialect.
//
//                         Affine --
//                                  |
//                                  v
//                       Arithmetic + Func --> LLVM (Dialect)
//                                  ^
//                                  |
//     'toy.print' --> Loop (SCF) --
//
// The interesting part is what this file does *not* contain. Toy writes exactly
// one pattern, for toy.print; affine, arith, scf, cf, func and memref are
// lowered by pattern sets MLIR already ships. And the toy.print pattern does not
// emit LLVM operations at all. It emits an scf loop nest, which is itself
// illegal, and relies on the scf -> cf -> llvm patterns in the same set to
// finish the job. That is transitive lowering: a pattern may leave the IR
// illegal as long as some other pattern can legalise what it produced.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/LLVMIR/LLVMAttrs.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Support/TypeID.h"
#include "toy/Dialect.h"
#include "toy/Passes.h"

#include "mlir/Conversion/AffineToStandard/AffineToStandard.h"
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVMPass.h"
#include "mlir/Conversion/LLVMCommon/ConversionTarget.h"
#include "mlir/Conversion/LLVMCommon/TypeConverter.h"
#include "mlir/Conversion/MemRefToLLVM/MemRefToLLVM.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/Support/Casting.h"

#include <memory>
#include <utility>

using namespace mlir;

//===----------------------------------------------------------------------===//
// ToyToLLVM Conversion Patterns
//===----------------------------------------------------------------------===//

namespace {

/// toy.print -> a loop nest calling printf once per element.
///
/// printf is declared into the module and called through the LLVM dialect
/// directly; the loops are scf, left for the borrowed scf -> cf -> llvm patterns
/// to lower.
class PrintOpLowering : public OpConversionPattern<toy::PrintOp> {
public:
  using OpConversionPattern<toy::PrintOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(toy::PrintOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto *context = rewriter.getContext();
    auto memRefType = llvm::cast<MemRefType>((*op->operand_type_begin()));
    auto memRefShape = memRefType.getShape();
    auto loc = op->getLoc();

    ModuleOp parentModule = op->getParentOfType<ModuleOp>();

    // The format strings are globals in the module, created once however many
    // prints there are. Both are explicitly NUL-terminated: printf reads them
    // as C strings, and an MLIR StringAttr carries no terminator of its own.
    auto printfRef = getOrInsertPrintf(rewriter, parentModule);
    Value formatSpecifierCst = getOrCreateGlobalString(
        loc, rewriter, "frmt_spec", StringRef("%f \0", 4), parentModule);
    Value newLineCst = getOrCreateGlobalString(
        loc, rewriter, "nl", StringRef("\n\0", 2), parentModule);

    // One loop per dimension, built outside-in. Each new loop comes with a
    // terminator that has to go before the body is filled, hence the erase.
    SmallVector<Value, 4> loopIvs;
    for (unsigned i = 0, e = memRefShape.size(); i != e; ++i) {
      auto lowerBound = arith::ConstantIndexOp::create(rewriter, loc, 0);
      auto upperBound =
          arith::ConstantIndexOp::create(rewriter, loc, memRefShape[i]);
      auto step = arith::ConstantIndexOp::create(rewriter, loc, 1);
      auto loop =
          scf::ForOp::create(rewriter, loc, lowerBound, upperBound, step);
      for (Operation &nested : make_early_inc_range(*loop.getBody()))
        rewriter.eraseOp(&nested);
      loopIvs.push_back(loop.getInductionVar());

      rewriter.setInsertionPointToEnd(loop.getBody());

      // The newline belongs at the end of each row, i.e. after the inner loop
      // finishes, so it is emitted in every loop body except the innermost.
      if (i != e - 1)
        LLVM::CallOp::create(rewriter, loc, getPrintfType(context), printfRef,
                             newLineCst);
      scf::YieldOp::create(rewriter, loc);
      rewriter.setInsertionPointToStart(loop.getBody());
    }

    auto elementLoad =
        memref::LoadOp::create(rewriter, loc, op.getInput(), loopIvs);
    LLVM::CallOp::create(rewriter, loc, getPrintfType(context), printfRef,
                         ArrayRef<Value>({formatSpecifierCst, elementLoad}));

    rewriter.eraseOp(op);
    return success();
  }

private:
  /// printf's signature: `i32 (ptr, ...)`.
  ///
  /// The pointer is opaque, since modern LLVM has no typed pointers, so there
  /// is nothing to say about what it points at.
  static LLVM::LLVMFunctionType getPrintfType(MLIRContext *context) {
    auto llvmI32Ty = IntegerType::get(context, 32);
    auto llvmPtrTy = LLVM::LLVMPointerType::get(context);
    auto llvmFnType = LLVM::LLVMFunctionType::get(llvmI32Ty, llvmPtrTy,
                                                  /*isVarArg=*/true);
    return llvmFnType;
  }

  /// Declares printf in the module on first use and returns a reference to it.
  static FlatSymbolRefAttr getOrInsertPrintf(PatternRewriter &rewriter,
                                             ModuleOp module) {
    auto *context = module.getContext();
    if (module.lookupSymbol<LLVM::LLVMFuncOp>("printf"))
      return SymbolRefAttr::get(context, "printf");

    // The guard restores the insertion point: the declaration goes at the top
    // of the module, while the caller is mid-way through a function body.
    PatternRewriter::InsertionGuard insertGuard(rewriter);
    rewriter.setInsertionPointToStart(module.getBody());
    LLVM::LLVMFuncOp::create(rewriter, module.getLoc(), "printf",
                             getPrintfType(context));
    return SymbolRefAttr::get(context, "printf");
  }

  /// Returns a pointer to the first character of a module-level string global,
  /// creating the global on first use.
  static Value getOrCreateGlobalString(Location loc, OpBuilder &builder,
                                       StringRef name, StringRef value,
                                       ModuleOp module) {
    LLVM::GlobalOp global;
    if (!(global = module.lookupSymbol<LLVM::GlobalOp>(name))) {
      OpBuilder::InsertionGuard insertGuard(builder);
      builder.setInsertionPointToStart(module.getBody());
      auto type = LLVM::LLVMArrayType::get(
          IntegerType::get(builder.getContext(), 8), value.size());
      global = LLVM::GlobalOp::create(builder, loc, type, /*isConstant=*/true,
                                      LLVM::Linkage::Internal, name,
                                      builder.getStringAttr(value),
                                      /*alignment=*/0);
    }

    // Two zero indices, as usual for indexing into an array through a pointer
    // to it: the first steps over the array, the second selects its element.
    Value globalPtr = LLVM::AddressOfOp::create(builder, loc, global);
    Value cst0 = LLVM::ConstantOp::create(builder, loc, builder.getI64Type(),
                                          builder.getIndexAttr(0));
    return LLVM::GEPOp::create(
        builder, loc, LLVM::LLVMPointerType::get(builder.getContext()),
        global.getType(), globalPtr, ArrayRef<Value>({cst0, cst0}));
  }
};

} // namespace

//===----------------------------------------------------------------------===//
// ToyToLLVMLoweringPass
//===----------------------------------------------------------------------===//

namespace {
struct ToyToLLVMLoweringPass
    : public PassWrapper<ToyToLLVMLoweringPass, OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ToyToLLVMLoweringPass)

  StringRef getArgument() const override { return "toy-to-llvm"; }

  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<LLVM::LLVMDialect, scf::SCFDialect>();
  }

  void runOnOperation() final;
};
} // namespace

void ToyToLLVMLoweringPass::runOnOperation() {
  // LLVMConversionTarget declares the LLVM dialect legal and nothing else; the
  // module itself has to be allowed through explicitly since it is builtin.
  LLVMConversionTarget target(getContext());
  target.addLegalOp<ModuleOp>();

  // The type converter is what turns a memref into its LLVM representation: a
  // descriptor struct of allocated pointer, aligned pointer, offset, sizes and
  // strides. Everything the memref type carried implicitly becomes explicit
  // here, which is also why loop region arguments need converting and a plain
  // pattern set would not be enough.
  LLVMTypeConverter typeConverter(&getContext());

  RewritePatternSet patterns(&getContext());
  populateAffineToStdConversionPatterns(patterns);
  populateSCFToControlFlowConversionPatterns(patterns);
  mlir::arith::populateArithToLLVMConversionPatterns(typeConverter, patterns);
  populateFinalizeMemRefToLLVMConversionPatterns(typeConverter, patterns);
  cf::populateControlFlowToLLVMConversionPatterns(typeConverter, patterns);
  cf::populateAssertToLLVMConversionPattern(typeConverter, patterns);
  populateFuncToLLVMConversionPatterns(typeConverter, patterns);

  // The one operation from the Toy dialect that is still standing.
  patterns.add<PrintOpLowering>(&getContext());

  // Full conversion: anything still illegal afterwards is an error, which is the
  // guarantee the translation to LLVM IR needs.
  auto module = getOperation();
  if (failed(applyFullConversion(module, target, std::move(patterns))))
    signalPassFailure();
}

std::unique_ptr<mlir::Pass> mlir::toy::createLowerToLLVMPass() {
  return std::make_unique<ToyToLLVMLoweringPass>();
}
