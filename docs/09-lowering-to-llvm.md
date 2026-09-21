# 09. Lowering to LLVM

File: `src/passes/LowerToLLVM.cpp` (234 lines).

## What this is for

The second lowering, and the last thing that happens inside MLIR. Everything
still standing (affine, arith, func, memref, scf, and the one remaining
`toy.print`) becomes the LLVM dialect, after which
[Translate.cpp](12-debug-info-and-objects.md) can hand the module to LLVM.

What stands out about this file is how little of it there is. Toy writes one
pattern and borrows the rest.

## Transitive lowering

```
                         Affine --
                                  |
                                  v
                       Arithmetic + Func --> LLVM (Dialect)
                                  ^
                                  |
     'toy.print' --> Loop (SCF) --
```

`src/passes/LowerToLLVM.cpp:12-26` draws this and explains the consequence: the
`toy.print` pattern does not emit LLVM operations at all. It emits an `scf` loop
nest, which is *itself illegal*, and relies on the scf → cf → llvm patterns in the
same pattern set to finish the job.

That is transitive lowering: a pattern may leave the IR illegal as long as some
other pattern in the set can legalize what it produced. It is why you can add a
dialect to a pipeline without teaching every existing pattern about it.

## The pass, in full

```c++
LLVMConversionTarget target(getContext());
target.addLegalOp<ModuleOp>();

LLVMTypeConverter typeConverter(&getContext());

RewritePatternSet patterns(&getContext());
populateAffineToStdConversionPatterns(patterns);
populateSCFToControlFlowConversionPatterns(patterns);
mlir::arith::populateArithToLLVMConversionPatterns(typeConverter, patterns);
populateFinalizeMemRefToLLVMConversionPatterns(typeConverter, patterns);
cf::populateControlFlowToLLVMConversionPatterns(typeConverter, patterns);
cf::populateAssertToLLVMConversionPattern(typeConverter, patterns);
populateFuncToLLVMConversionPatterns(typeConverter, patterns);

patterns.add<PrintOpLowering>(&getContext());

auto module = getOperation();
if (failed(applyFullConversion(module, target, std::move(patterns))))
  signalPassFailure();
```

`:213-239`. Seven `populate*` calls from MLIR, one line of Toy. Three details:

- `LLVMConversionTarget` declares the LLVM dialect legal and nothing else, so
  `ModuleOp` has to be allowed through explicitly, since it belongs to the builtin
  dialect.
- `applyFullConversion` treats anything still illegal afterwards as an error,
  which is exactly the guarantee translation to LLVM IR needs.
- `LLVMTypeConverter` is required here where the affine lowering needed none,
  because this conversion changes block argument types (loop induction variables
  become `i64`), and only a type converter can rewrite those.

## The memref descriptor

`LLVMTypeConverter` is what turns a memref into its LLVM representation.
Everything the `memref` type carried implicitly becomes explicit, and shows up in
the output as a five-field struct:

```
!llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)>
```

In order: allocated pointer, aligned pointer, offset, sizes, strides. Both
pointers are opaque; modern LLVM has no typed pointers, so there is nothing to say
about what they point at.

The struct is assembled field by field with `llvm.insertvalue`:

```
%13 = llvm.call @malloc(%12) : (i64) -> !llvm.ptr
%14 = llvm.mlir.poison : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)>
%15 = llvm.insertvalue %13, %14[0] : !llvm.struct<(ptr, ptr, i64, ...)>
%16 = llvm.insertvalue %13, %15[1] : !llvm.struct<(ptr, ptr, i64, ...)>
```

Note the two pointers start equal here, and that the allocation size is computed
by the `getelementptr`-on-null idiom (`llvm.mlir.zero`, then `getelementptr %10[6]`,
then `ptrtoint`) rather than by hardcoding `sizeof(double)`.

> The published tutorial text shows four fields and typed pointers. Both are
> stale: this LLVM emits five fields with opaque pointers, which is what the
> struct above was copied from.

## The one pattern Toy writes

`PrintOpLowering` (`:75-187`) turns `toy.print` into a loop nest calling `printf`
per element. Three parts.

### Format strings as module globals (`:89-96`)

```c++
auto printfRef = getOrInsertPrintf(rewriter, parentModule);
Value formatSpecifierCst = getOrCreateGlobalString(
    loc, rewriter, "frmt_spec", StringRef("%f \0", 4), parentModule);
Value newLineCst = getOrCreateGlobalString(
    loc, rewriter, "nl", StringRef("\n\0", 2), parentModule);
```

Both are explicitly NUL-terminated, because `printf` reads C strings and an MLIR
`StringAttr` carries no terminator of its own. The lengths (4 and 2) include it.

### The loop nest (`:98-127`)

Built outside-in. Each new `scf.for` arrives with a terminator that must be
removed before the body is filled:

```c++
auto loop = scf::ForOp::create(rewriter, loc, lowerBound, upperBound, step);
for (Operation &nested : make_early_inc_range(*loop.getBody()))
  rewriter.eraseOp(&nested);
loopIvs.push_back(loop.getInductionVar());

rewriter.setInsertionPointToEnd(loop.getBody());
if (i != e - 1)
  LLVM::CallOp::create(rewriter, loc, getPrintfType(context), printfRef,
                       newLineCst);
scf::YieldOp::create(rewriter, loc);
rewriter.setInsertionPointToStart(loop.getBody());
```

The newline goes at the end of each *row*, so it is emitted in every loop body
except the innermost. That is what `i != e - 1` is testing.

### printf's declaration (`:137-159`)

```c++
static LLVM::LLVMFunctionType getPrintfType(MLIRContext *context) {
  auto llvmI32Ty = IntegerType::get(context, 32);
  auto llvmPtrTy = LLVM::LLVMPointerType::get(context);
  auto llvmFnType = LLVM::LLVMFunctionType::get(llvmI32Ty, llvmPtrTy,
                                                /*isVarArg=*/true);
  return llvmFnType;
}
```

`i32 (ptr, ...)`. `LLVMPointerType::get(context)` takes only the context, as
there is no pointee type to pass. `getOrInsertPrintf` declares it once, using an
`InsertionGuard` because the declaration belongs at the top of the module while
the caller is mid-function.

`getOrCreateGlobalString` (`:163-186`) ends with the two-zero-index GEP that is
standard for indexing into an array through a pointer to it: the first index steps
over the array, the second selects its element.

## Try it

```console
$ build/bin/toyc reference/tests/Ch6/codegen.toy -emit=mlir-llvm 2>&1 | head -8
module {
  llvm.func @free(!llvm.ptr)
  llvm.mlir.global internal constant @nl("\0A\00") {addr_space = 0 : i32}
  llvm.mlir.global internal constant @frmt_spec("%f \00") {addr_space = 0 : i32}
  llvm.func @printf(!llvm.ptr, ...) -> i32
  llvm.func @malloc(i64) -> !llvm.ptr
  llvm.func @main() {
    %0 = llvm.mlir.constant(6.000000e+00 : f64) : f64
```

The globals and the three runtime declarations are all there. The structured loops
are gone: `grep -c '\^bb'` reports 36 basic blocks, and the loops survive only as
branches:

```
    llvm.br ^bb1(%99 : i64)
  ^bb1(%102: i64):  // 2 preds: ^bb0, ^bb5
    llvm.cond_br %103, ^bb2, ^bb6
```

That is the information loss to point at: after scf → cf, loop structure exists
only as a control-flow graph. Recovering it would take analysis. This is the same
argument as [08](08-lowering-to-affine.md), one level lower.

### LLVM IR, O0 versus -opt

```console
$ build/bin/toyc reference/tests/Ch6/codegen.toy -emit=llvm 2>&1 | wc -l
186
$ build/bin/toyc reference/tests/Ch6/codegen.toy -emit=llvm -opt 2>&1 | wc -l
44
```

And the whole program at `-opt`:

```llvm
define void @main() local_unnamed_addr #0 !dbg !6 {
.preheader5:
  %0 = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) @frmt_spec, double 1.000000e+00), !dbg !8
  %1 = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) @frmt_spec, double 1.600000e+01), !dbg !8
  %putchar = tail call i32 @putchar(i32 10), !dbg !8
  %2 = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) @frmt_spec, double 4.000000e+00), !dbg !8
  %3 = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) @frmt_spec, double 2.500000e+01), !dbg !8
  %putchar.1 = tail call i32 @putchar(i32 10), !dbg !8
  %4 = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) @frmt_spec, double 9.000000e+00), !dbg !8
  %5 = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) @frmt_spec, double 3.600000e+01), !dbg !8
  %putchar.2 = tail call i32 @putchar(i32 10), !dbg !8
  ret void, !dbg !9
}
```

LLVM constant-folded the entire computation. What remains is six `printf` calls
with literal doubles and three `putchar`s, with no malloc, no loops and no
arithmetic left. The values are the squares of the transposed matrix, computed at
compile time. Two asides worth making: LLVM rewrote `printf("\n")` into
`putchar(10)`, and the `!dbg` references survive, which is [the next
document](12-debug-info-and-objects.md).

## Pitfalls

- `applyFullConversion` fails loudly, which is the point. If you add a Toy
  operation and forget a pattern, this pass reports it rather than emitting a
  module LLVM cannot translate.
- The `populate*` order in `:224-230` matters less than it looks, since patterns
  are ordered by benefit rather than by insertion. What must be complete is the
  set. Dropping `populateFinalizeMemRefToLLVMConversionPatterns` leaves memref
  operations unlowered with a confusing error.
- `scf.ForOp` comes with a terminator. Filling the body without erasing it first
  gives you two terminators and a verifier failure.
- Format strings need explicit NUL bytes and matching lengths; `StringRef("%f ")`
  would pass 3 bytes and `printf` would read past the end.
