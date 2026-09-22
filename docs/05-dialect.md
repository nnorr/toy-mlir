# 05. The Toy dialect

Files: `include/toy/Ops.td` (498), `include/toy/Dialect.h` (94), `src/dialect/ToyDialect.cpp` (79), `src/dialect/Ops.cpp` (501), `src/dialect/StructType.cpp` (183), `src/dialect/Interfaces.cpp` (131), and the generated `build/include/toy/*.inc` (about 6,500 lines)

This is the document to read first if you want to understand MLIR rather than Toy.

## What a dialect is

A dialect is a namespace that owns a set of operations, types and attributes, plus the hooks MLIR calls back into for behavior specific to them. Toy owns the `toy.` prefix, twelve operations, one type, and four hooks.

It is not a separate IR. MLIR has one IR, and its structure is fixed:

```
Operation
 ├─ name                  "toy.transpose"
 ├─ operands              SSA values it reads
 ├─ results               SSA values it defines
 ├─ attributes            compile-time data, no use-def edge
 ├─ properties            inherent attributes, stored inline
 ├─ regions               nested blocks (a function body, a loop body)
 ├─ successors            for terminators
 └─ location              where it came from
```

Every operation from every dialect has exactly that shape, which is why a pass written years before Toy existed can walk Toy IR. A dialect supplies the vocabulary; the grammar is MLIR's.

Several dialects coexist in one module. During the affine lowering, `toy.print` sits beside `affine.for` and `memref.alloc` in the same function, which is the property progressive lowering depends on.

## Operation versus Op

`mlir::Operation` is the class that actually holds the data above. It is opaque: it knows its name as a string, and nothing about what `toy.transpose` means.

`TransposeOp` is a generated wrapper around an `Operation*`. It has no fields of its own, so it is passed by value, and `llvm::dyn_cast<TransposeOp>(op)` is a name check rather than a C++ downcast. The generated declaration shows both facts (`build/include/toy/Ops.h.inc`):

```c++
class TransposeOp : public ::mlir::Op<TransposeOp, ::mlir::OpTrait::ZeroRegions,
    ::mlir::OpTrait::OneResult,
    ::mlir::OpTrait::OneTypedResult<::mlir::TensorType>::Impl,
    ::mlir::OpTrait::ZeroSuccessors, ::mlir::OpTrait::OneOperand,
    ::mlir::OpTrait::OpInvariants, ::mlir::ConditionallySpeculatable::Trait,
    ::mlir::OpTrait::AlwaysSpeculatableImplTrait,
    ::mlir::MemoryEffectOpInterface::Trait, ShapeInference::Trait> {
```

The accessors reach through to the operation:

```c++
  ::mlir::TypedValue<::mlir::TensorType> getInput() {
    ...
  }
  ::mlir::OpOperand &getInputMutable() {
    ...
    return getOperation()->getOpOperand(range.first);
  }
```

The practical consequences: store `Operation*` if you need a handle that outlives a cast, never expect `sizeof(TransposeOp)` to mean anything, and remember that two `TransposeOp` values comparing equal means they wrap the same operation.

## Operands, attributes, results, regions

The distinction that trips people up is operand versus attribute. An operand is an SSA value with a use-def edge, produced by another operation. An attribute is data known at compile time with no edge.

`toy.constant` shows it (`include/toy/Ops.td:120`):

```tablegen
  // An attribute, not an operand: known at compile time, no use-def edge.
  let arguments = (ins F64ElementsAttr:$value);
  let results = (outs F64Tensor);
```

A 2x3 literal is one operation carrying one `dense<...>` attribute, not six operations feeding a constructor. In the generic form the difference is visible, because properties print inside `<{...}>` and operands inside `(...)`:

```console
$ build/bin/toyc docs/examples/ex.toy -emit=mlir -opt --mlir-print-op-generic 2>&1
"builtin.module"() ({
  "toy.func"() <{function_type = () -> (), sym_name = "main"}> ({
    %0 = "toy.constant"() <{value = dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>}> : () -> tensor<2x3xf64>
    %1 = "toy.transpose"(%0) : (tensor<2x3xf64>) -> tensor<3x2xf64>
    %2 = "toy.mul"(%1, %1) : (tensor<3x2xf64>, tensor<3x2xf64>) -> tensor<3x2xf64>
    "toy.print"(%2) : (tensor<3x2xf64>) -> ()
    "toy.return"() : () -> ()
  }) : () -> ()
}) : () -> ()
```

`toy.constant` has zero operands, one result and one property. `toy.func` has a property for its name and its type, and a region holding the body. That region is the only reason `main`'s operations are nested rather than free-floating.

Properties are inherent attributes stored inline in the operation rather than in a dictionary. ODS generates a struct for them (`build/include/toy/Ops.h.inc`):

```c++
class FuncOpGenericAdaptorBase {
public:
  struct Properties {
    using arg_attrsTy = ::mlir::ArrayAttr;
    arg_attrsTy arg_attrs;
    ...
    using function_typeTy = ::mlir::TypeAttr;
    function_typeTy function_type;
```

Discardable attributes still live in a dictionary and still print as `{...}`. The split is why older MLIR documentation shows `{value = ...}` where current output shows `<{value = ...}>`.

## Traits

A trait is a compile-time mixin that adds behavior and invariants. Toy uses these:

`Pure` says the operation reads and writes nothing and can be deleted when its results are unused. Without it MLIR must assume a side effect and keeps dead operations. Canonicalization visibly depends on it; [06-patterns-and-folding.md](06-patterns-and-folding.md) shows the before and after.

`Terminator` marks the operation that ends a block, and `HasParent<"FuncOp">` restricts where it may appear. `toy.return` has both (`include/toy/Ops.td:401`), so a return outside a function is rejected structurally rather than by a verifier.

`IsolatedFromAbove` on `FuncOp` says the region does not reference values defined outside it. That lets the pass manager work on each function in parallel, and makes a function a legal anchor for a nested pass pipeline.

`SameOperandsAndResultShape` on `CastOp` is a verifier generated from a trait rather than written by hand.

`ConstantLike` on the two constant operations tells generic folding machinery that these operations are how a constant is spelled in this dialect.

## ODS and what it generates

`Ops.td` is the single source of truth. `mlir-tblgen` turns each `def` into a C++ class: accessors, builders, an adaptor, a parser, a printer, and the verification that constraints imply.

A small operation and the code it produces:

```tablegen
def TransposeOp : Toy_Op<"transpose",
    [Pure, DeclareOpInterfaceMethods<ShapeInferenceOpInterface>]> {
  let arguments = (ins F64Tensor:$input);
  let results = (outs F64Tensor);

  let assemblyFormat = [{
    `(` $input `:` type($input) `)` attr-dict `to` type(results)
  }];

  let hasCanonicalizer = 1;
  let builders = [OpBuilder<(ins "Value":$input)>];
  let hasVerifier = 1;
}
```

From that, the generated header declares:

| Written in ODS | Generated C++ |
| --- | --- |
| `$input` | `getInput()`, `getInputMutable()` |
| `results = (outs F64Tensor)` | `OneResult`, `OneTypedResult<TensorType>::Impl` traits |
| `Pure` | `AlwaysSpeculatableImplTrait`, `MemoryEffectOpInterface::Trait` |
| `DeclareOpInterfaceMethods<...>` | `ShapeInference::Trait`, a declaration of `inferShapes()` |
| `builders = [...]` | `static TransposeOp create(OpBuilder &, Location, Value input)` |
| `assemblyFormat` | `parse()` and `print()` bodies |
| `hasVerifier = 1` | a declaration of `verify()`, called from `verifyInvariants()` |
| `hasCanonicalizer = 1` | a declaration of `getCanonicalizationPatterns()` |

`getOperationName()` returns the mnemonic, and that string is what `dyn_cast` compares.

Regenerate and read it yourself:

```console
$ ~/dev/08_mlir_toy/build/bin/mlir-tblgen -gen-op-decls include/toy/Ops.td \
    -I ~/dev/08_mlir_toy/llvm-project/mlir/include -I include | less
```

The declarations ODS writes for you are also the reason the hand-written files are short. `src/dialect/Ops.cpp` is 455 lines for twelve operations, and most of it is the three custom parsers and the five verifiers.

## Declarative format versus a hand-written one

Most Toy operations declare an `assemblyFormat` and get their syntax for free. Three cannot.

`ConstantOp` prints its result type through its attribute rather than separately, so the type is not an independent piece of syntax (`src/dialect/Ops.cpp:141`):

```c++
mlir::ParseResult ConstantOp::parse(mlir::OpAsmParser &parser,
                                    mlir::OperationState &result) {
  mlir::DenseElementsAttr value;
  if (parser.parseOptionalAttrDict(result.attributes) ||
      parser.parseAttribute(value, "value", result.attributes))
    return failure();

  result.addTypes(value.getType());
  return success();
}
```

The printer elides `value` from the dictionary because it prints it positionally; printing it twice would not parse back (`src/dialect/Ops.cpp:152`).

`AddOp` and `MulOp` share one parser and printer, because they print one type when operands and result agree and a functional type when they do not (`src/dialect/Ops.cpp:95`):

```c++
static void printBinaryOp(mlir::OpAsmPrinter &printer, mlir::Operation *op) {
  printer << " " << op->getOperands();
  printer.printOptionalAttrDict(op->getAttrs());
  printer << " : ";

  Type resultType = *op->result_type_begin();
  if (llvm::all_of(op->getOperandTypes(),
                   [=](Type type) { return type == resultType; })) {
    printer << resultType;
    return;
  }

  printer.printFunctionalType(op->getOperandTypes(), op->getResultTypes());
}
```

That is not a style choice. Between inlining and shape inference the operands are ranked and the result is not, so the same operation has to print both ways at different points in the pipeline:

```console
$ tail -4 docs/examples/functional.mlir
toy.func @f(%a: tensor<2x3xf64>) {
  %0 = "toy.mul"(%a, %a) : (tensor<2x3xf64>, tensor<2x3xf64>) -> tensor<*xf64>
  toy.return
}
$ build/bin/toyc docs/examples/functional.mlir -x mlir -emit=mlir 2>&1 | sed -n '3p'
    %0 = toy.mul %arg0, %arg0 : (tensor<2x3xf64>, tensor<2x3xf64>) -> tensor<*xf64>
```

Once every type agrees, the same operation prints as `toy.mul %1, %1 : tensor<3x2xf64>`. A declarative `assemblyFormat` has to commit to one of those two shapes.

## Verifiers

ODS generates verification from the constraints. `F64Tensor:$input` already rejects an integer tensor, and `results = (outs F64Tensor)` already rejects two results. That is where this comes from:

```console
$ build/bin/toyc reference/tests/Ch2/invalid.mlir -emit=mlir -x mlir 2>&1 | head -1
loc("reference/tests/Ch2/invalid.mlir":8:8): error: 'toy.print' op requires zero results
```

Nobody wrote that check. `toy.print` declares no results, so the generated `verifyInvariants()` enforces it.

A hand-written `verify()` is for constraints ODS cannot state. `ReturnOp::verify` relates two operations, and only one of them is `this` (`src/dialect/Ops.cpp:397`):

```c++
llvm::LogicalResult ReturnOp::verify() {
  auto function = cast<FuncOp>((*this)->getParentOp());

  if (getNumOperands() > 1)
    return emitOpError() << "expects at most 1 return operand";

  const auto &results = function.getFunctionType().getResults();
  if (getNumOperands() != results.size())
    return emitOpError() << "does not return the same number of values ("
                         << getNumOperands() << ") as the enclosing function ("
                         << results.size() << ")";
```

```console
$ build/bin/toyc docs/examples/badreturn.mlir -emit=mlir -x mlir 2>&1 | head -1
loc("docs/examples/badreturn.mlir":5:3): error: 'toy.return' op does not return the same number of values (1) as the enclosing function (0)
```

The `HasParent<"FuncOp">` trait has already guaranteed the parent, which is why the `cast` on the first line cannot fail.

One detail in these verifiers is worth copying into your own dialects. An unranked type is not a mismatch, it is a shape that has not been inferred yet (`src/dialect/Ops.cpp:415`):

```c++
  // An unranked type on either side is a shape that has not been inferred yet,
  // not a mismatch. Rejecting it here would make the IR invalid between
  // MLIRGen and the shape inference pass.
  if (inputType == resultType ||
      llvm::isa<mlir::UnrankedTensorType>(inputType) ||
      llvm::isa<mlir::UnrankedTensorType>(resultType))
    return mlir::success();
```

A verifier that rejects unranked types would make the IR invalid for the whole window between generation and shape inference. `TransposeOp::verify` has the same guard, and fires once shapes are known:

```console
$ build/bin/toyc docs/examples/badtranspose.mlir -emit=mlir -x mlir 2>&1 | head -1
loc("docs/examples/badtranspose.mlir":5:8): error: expected result shape to be a transpose of the input
```

## The struct type

`StructType` is the one type Toy defines. An MLIR `Type` is a handle, two words wide, copied by value; the data lives in a storage instance owned by the `MLIRContext` and uniqued on its contents (`src/dialect/StructType.cpp:54`):

```c++
struct StructTypeStorage : public mlir::TypeStorage {
  /// Structs are uniqued structurally: two structs with the same element types
  /// are the same type, whatever they were named in the source.
  using KeyTy = llvm::ArrayRef<mlir::Type>;

  bool operator==(const KeyTy &key) const { return key == elementTypes; }

  static llvm::hash_code hashKey(const KeyTy &key) {
    return llvm::hash_value(key);
  }

  static StructTypeStorage *construct(mlir::TypeStorageAllocator &allocator,
                                      const KeyTy &key) {
    llvm::ArrayRef<mlir::Type> elementTypes = allocator.copyInto(key);

    return new (allocator.allocate<StructTypeStorage>())
        StructTypeStorage(elementTypes);
  }

  llvm::ArrayRef<mlir::Type> elementTypes;
};
```

The uniquer needs three things: a key type, a way to compare a key against an existing instance, and a way to build one from a key. `allocator.copyInto` matters because the caller's `ArrayRef` is a temporary and the storage outlives it.

Uniquing is why `type1 == type2` is a pointer comparison, and why building the same struct type twice costs a hash lookup instead of an allocation.

`Base::get` is the uniquer entry point (`src/dialect/StructType.cpp:111`), and the context comes from the element types because a type cannot exist outside the context that uniqued it.

The syntax is the dialect's responsibility below `!toy.`:

```
struct-type ::= `struct` `<` type (`,` type)* `>`
```

`parseType` and `printType` implement it (`src/dialect/StructType.cpp:140`, `:174`). Note that every MLIR parse function returns `ParseResult`, which converts to true on failure, which is what makes the `||` chains read as "if anything went wrong, give up":

```c++
  // Parse: `struct` `<`
  if (parser.parseKeyword("struct") || parser.parseLess())
    return Type();
```

Rejecting a bad element type in the parser rather than in a verifier means a malformed type never enters the context at all.

## Registration

`initialize()` is the whole registration surface (`src/dialect/ToyDialect.cpp:40`):

```c++
void ToyDialect::initialize() {
  addOperations<
#define GET_OP_LIST
#include "toy/Ops.cpp.inc"
      >();

  registerInterfaces();
  registerTypes();
}
```

Registering an operation gives it a verifier, a parser and a printer. An unregistered `toy.foo` still parses in generic form, and nothing ever checks it.

The two hooks exist for a concrete reason. `addTypes<StructType>()` instantiates the storage uniquer, which needs the complete `StructTypeStorage`, and that type is private to `StructType.cpp`. So the registration lives beside the storage (`src/dialect/StructType.cpp:105`):

```c++
void ToyDialect::registerTypes() { addTypes<StructType>(); }
```

`registerInterfaces()` is the same move for `ToyInlinerInterface`, which is in an anonymous namespace in `Interfaces.cpp`. Upstream keeps everything in one `Dialect.cpp` and never meets the problem; in-tree MLIR dialects split their types out and register them through a hook for this reason. `ARCHITECTURE.md` records the decision.

Two more dialect-level hooks:

`materializeConstant` turns an `Attribute` produced by a folder back into an operation (`src/dialect/ToyDialect.cpp:70`). Folding replaces an operation with an attribute, the IR can only hold operations, so somebody has to build one again. A generic transformation cannot know which Toy operation carries which kind of attribute, so it asks the dialect.

`ToyInlinerInterface` answers the inliner. The interesting hook is `materializeCallConversion` (`src/dialect/Interfaces.cpp:113`), which bridges a call passing `tensor<2x3xf64>` to a parameter typed `tensor<*xf64>`. The inliner refuses to paper over that itself; without the hook, inlining silently does nothing.

## Interfaces, briefly

A dialect interface answers for the dialect as a whole. An op interface is implemented per operation. `src/dialect/Interfaces.cpp:12` states the difference, and the per-operation implementations of `CallOpInterface`, `CastOpInterface` and Toy's own `ShapeInference` are in `Ops.cpp`. The interface mechanism itself is covered in the interfaces document.

The inliner, the canonicalizer and CSE are generic MLIR passes. They work on a dialect that postdates them because `Interfaces.cpp` answers the questions they ask.

## Try it

```console
$ build/bin/toyc <file>.toy -emit=mlir                      # custom syntax
$ build/bin/toyc <file>.toy -emit=mlir --mlir-print-op-generic   # the real structure
$ build/bin/toyc <file>.mlir -x mlir -emit=mlir             # parse MLIR back in
```

Round-tripping is the check that the parsers and printers agree:

```console
$ build/bin/toyc docs/examples/ex.toy -emit=mlir -opt 2>build/rt.mlir
$ diff <(build/bin/toyc build/rt.mlir -emit=mlir -x mlir 2>&1) build/rt.mlir && echo identical
identical
```

## Pitfalls

`Ops.cpp.inc` with `GET_OP_CLASSES` must be included exactly once in the whole project, and it is, at the bottom of `src/dialect/Ops.cpp`. A second include is a duplicate-symbol link error.

A trait can silently do nothing if you forget it. Dropping `Pure` from `TransposeOp` does not break the build or any verifier; dead transposes simply stop being deleted, and the only symptom is IR that looks slightly wrong at `-opt`.

Editing `Ops.td` regenerates headers, so a stale build directory produces confusing errors about accessors that exist in the file you are reading. Re-run `ninja -C build`.

An unregistered dialect is not an error by default in every tool. `mlir-opt` needs `--allow-unregistered-dialect` to accept one, and then it checks only structural invariants, not yours.
