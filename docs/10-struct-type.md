# 10. The struct type

Files: `src/dialect/StructType.cpp` (194 lines), `src/dialect/Folders.cpp`, the
struct parts of `src/MLIRGen.cpp` and `src/dialect/Ops.cpp`,
`include/toy/Ops.td`.

## What this is for

Toy's one type of its own, `!toy.struct<...>`, and the machinery a *parametric*
type needs. Keep the payoff in view while reading: adding this type to the
language required no change to the lowering pipeline at all, and the last section
verifies that.

`docs/examples/dumps/struct.mlir.opt.diff` is that payoff in one file. Five
mentions of `!toy.struct` in the unoptimized dump, none in the optimized one, and
the affine and LLVM dumps beside it are what any struct-free program would
produce.

## A Type is a handle, not an object

`src/dialect/StructType.cpp:12-17` puts it plainly. `StructType` is two words
wide and copied by value everywhere. The data, which is the list of element types,
lives in a `StructTypeStorage` instance owned by the `MLIRContext` and uniqued on
its contents.

Two consequences:

- `a == b` on two types is a pointer comparison.
- Building the same struct type twice costs a hash lookup, not an allocation.

The same is true of every MLIR `Type` and `Attribute`; `StructType` is just the
one place in Toy where you see the machinery.

## The storage class

The uniquer needs three things from a storage class: a key type, a way to compare
a key against an existing instance, and a way to build a new instance from a key.

```c++
struct StructTypeStorage : public mlir::TypeStorage {
  using KeyTy = llvm::ArrayRef<mlir::Type>;

  StructTypeStorage(llvm::ArrayRef<mlir::Type> elementTypes)
      : elementTypes(elementTypes) {}

  bool operator==(const KeyTy &key) const { return key == elementTypes; }

  static llvm::hash_code hashKey(const KeyTy &key) {
    return llvm::hash_value(key);
  }

  static KeyTy getKey(llvm::ArrayRef<mlir::Type> elementTypes) {
    return KeyTy(elementTypes);
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

`src/dialect/StructType.cpp:54-87`. Points that matter:

- `KeyTy` is the element type list, so structs are uniqued structurally: two
  structs with the same element types are the same type, whatever they were named
  in the source. Toy's `struct Struct {...}` name never reaches the IR.
- `construct` must allocate through the given `allocator` (`:75-77`). The caller's
  `ArrayRef` is a temporary; the storage outlives it for as long as the context
  does. `allocator.copyInto(key)` is what makes the element list durable.
- A type with no parameters (`index`, say) needs none of this and uses the default
  `TypeStorage`.

`StructType::get` is the entry point (`:111-120`):

```c++
StructType StructType::get(llvm::ArrayRef<mlir::Type> elementTypes) {
  assert(!elementTypes.empty() && "expected at least 1 element type");
  mlir::MLIRContext *ctx = elementTypes.front().getContext();
  return Base::get(ctx, elementTypes);
}
```

`Base::get` is the uniquer: hash the arguments into a key, return the existing
instance if there is one, call `construct` otherwise. The context comes from the
element types because a type cannot exist outside the context that uniqued it.

## Registration, and why it lives here

```c++
void ToyDialect::registerTypes() { addTypes<StructType>(); }
```

`src/dialect/StructType.cpp:105`. This is a hook declared in
`include/toy/Ops.td`'s `extraClassDeclaration`, not a method of this file's own
invention. The reason is mechanical: `addTypes<>` instantiates the storage
uniquer, which needs the complete `StructTypeStorage`, and `Dialect.h` only
forward-declares it (`include/toy/Dialect.h:29-33`). So registration has to happen
in the translation unit that defines the storage.

Upstream keeps everything in one `Dialect.cpp` and never meets the question. In
this repo it was a compile error before the hook existed; `ARCHITECTURE.md`
records the decision. Every in-tree MLIR dialect that splits its types out does
the same thing.

## Syntax

MLIR parses `!toy.` and dispatches; the dialect owns everything after that. The
grammar (`:135`):

```
struct-type ::= `struct` `<` type (`,` type)* `>`
```

```c++
mlir::Type ToyDialect::parseType(mlir::DialectAsmParser &parser) const {
  if (parser.parseKeyword("struct") || parser.parseLess())
    return Type();

  SmallVector<mlir::Type, 1> elementTypes;
  do {
    SMLoc typeLoc = parser.getCurrentLocation();
    mlir::Type elementType;
    if (parser.parseType(elementType))
      return nullptr;

    if (!llvm::isa<mlir::TensorType, StructType>(elementType)) {
      parser.emitError(typeLoc, "element type for a struct must either "
                                "be a TensorType or a StructType, got: ")
          << elementType;
      return Type();
    }
    elementTypes.push_back(elementType);
  } while (succeeded(parser.parseOptionalComma()));

  if (parser.parseGreater())
    return Type();
  return StructType::get(elementTypes);
}
```

`:140-170`. Every MLIR parse function returns `ParseResult`, which converts to
`true` on failure. That inversion is what makes the `||` chains read as "if
anything went wrong, give up". Rejecting a bad element type here rather than in a
verifier means a malformed type never enters the context at all.

The printer (`:174-183`) must agree exactly, or the IR stops round-tripping:

```c++
printer << "struct<";
llvm::interleaveComma(structType.getElementTypes(), printer);
printer << '>';
```

`useDefaultTypePrinterParser = 1` in `include/toy/Ops.td:61` is what generates the
declarations for these two.

## The two operations

From `include/toy/Ops.td`:

```tablegen
def StructConstantOp : Toy_Op<"struct_constant", [ConstantLike, Pure]> {
  let arguments = (ins ArrayAttr:$value);
  let results = (outs Toy_StructType:$output);
  let assemblyFormat = "$value attr-dict `:` type($output)";
  let hasVerifier = 1;
  let hasFolder = 1;
}

def StructAccessOp : Toy_Op<"struct_access", [Pure]> {
  let arguments = (ins Toy_StructType:$input, I64Attr:$index);
  let results = (outs Toy_Type:$output);
  let assemblyFormat = [{
    $input `[` $index `]` attr-dict `:` type($input) `->` type($output)
  }];
  let builders = [
    OpBuilder<(ins "Value":$input, "size_t":$index)>
  ];
  let hasVerifier = 1;
  let hasFolder = 1;
}
```

A struct constant is an `ArrayAttr` of the elements' own constant attributes, so a
constant of a composite type needs no new attribute kind. `Toy_StructType` and
`Toy_Type` (`include/toy/Ops.td:95-100`) are what let ODS accept the new type
alongside `F64Tensor`.

## The folders that make structs vanish

```c++
OpFoldResult StructConstantOp::fold(FoldAdaptor adaptor) { return getValue(); }

OpFoldResult StructAccessOp::fold(FoldAdaptor adaptor) {
  auto structAttr =
      llvm::dyn_cast_if_present<mlir::ArrayAttr>(adaptor.getInput());
  if (!structAttr)
    return nullptr;
  size_t elementIndex = getIndex();
  return structAttr[elementIndex];
}
```

`src/dialect/Folders.cpp:47-66`. The chain, spelled out in `:18-23`: inlining turns
every field access into `struct_access(struct_constant)`; each fold replaces it
with the element's attribute; by the time lowering starts there is no struct left
for the affine or LLVM conversions to handle.

The adaptor hands over what the operands folded to rather than the operands
themselves, so a null entry means "not a constant", which is the common case
before inlining has exposed it. The index needs no bounds check because the
verifier already validated it against the struct type.

## Try it

```toy
struct Struct {
  var a;
  var b;
}

def multiply_transpose(Struct value) {
  return transpose(value.a) * transpose(value.b);
}

def main() {
  Struct value = {[[1, 2, 3], [4, 5, 6]], [[1, 2, 3], [4, 5, 6]]};
  var c = multiply_transpose(value);
  print(c);
}
```

`reference/tests/Ch7/struct-codegen.toy`. Before optimization the type and both
operations are visible:

```console
$ build/bin/toyc reference/tests/Ch7/struct-codegen.toy -emit=mlir 2>&1
module {
  toy.func private @multiply_transpose(%arg0: !toy.struct<tensor<*xf64>, tensor<*xf64>>) -> tensor<*xf64> {
    %0 = toy.struct_access %arg0[0] : !toy.struct<tensor<*xf64>, tensor<*xf64>> -> tensor<*xf64>
    %1 = toy.transpose(%0 : tensor<*xf64>) to tensor<*xf64>
    %2 = toy.struct_access %arg0[1] : !toy.struct<tensor<*xf64>, tensor<*xf64>> -> tensor<*xf64>
    %3 = toy.transpose(%2 : tensor<*xf64>) to tensor<*xf64>
    %4 = toy.mul %1, %3 : tensor<*xf64>
    toy.return %4 : tensor<*xf64>
  }
  toy.func @main() {
    %0 = toy.struct_constant [dense<[[1.000000e+00, ...]]> : tensor<2x3xf64>, dense<[[...]]> : tensor<2x3xf64>] : !toy.struct<tensor<*xf64>, tensor<*xf64>>
    %1 = toy.generic_call @multiply_transpose(%0) : (!toy.struct<tensor<*xf64>, tensor<*xf64>>) -> tensor<*xf64>
    toy.print %1 : tensor<*xf64>
    toy.return
  }
}
```

With `-opt`, every trace of the struct is gone:

```console
$ build/bin/toyc reference/tests/Ch7/struct-codegen.toy -emit=mlir -opt 2>&1
module {
  toy.func @main() {
    %0 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
    %1 = toy.transpose(%0 : tensor<2x3xf64>) to tensor<3x2xf64>
    %2 = toy.mul %1, %1 : tensor<3x2xf64>
    toy.print %2 : tensor<3x2xf64>
    toy.return
  }
}
```

### The payoff, verified

That output is identical to the struct-free program's, both at the Toy level and
after the affine lowering:

```console
$ diff <(build/bin/toyc reference/tests/Ch7/struct-codegen.toy -emit=mlir -opt 2>&1) \
       <(build/bin/toyc reference/tests/Ch5/codegen.toy       -emit=mlir -opt 2>&1) \
  && echo IDENTICAL
IDENTICAL

$ diff <(build/bin/toyc reference/tests/Ch7/struct-codegen.toy -emit=mlir-affine -opt 2>&1) \
       <(build/bin/toyc reference/tests/Ch5/codegen.toy       -emit=mlir-affine -opt 2>&1) \
  && echo IDENTICAL
IDENTICAL
```

So the claim to make in a presentation is concrete. A new type was added to the
language, with storage, uniquing, syntax, two operations, two verifiers and two
folders, and the affine lowering, the LLVM lowering, the translation and the JIT
were left untouched. The type is a front-end and dialect concern that folding
erases before lowering begins.

## Pitfalls

- Registration in the wrong translation unit is a compile error, and a confusing
  one (`static_cast from 'BaseStorage *' ... not related by inheritance`). If you
  see it, the storage class is incomplete where `addTypes` was instantiated.
- `construct` must copy through the allocator. Storing the caller's `ArrayRef`
  directly leaves the storage pointing at a dead temporary, and it will appear to
  work for a while.
- Parser and printer must agree. A mismatch shows up as IR that prints but will
  not parse back, which the round-trip test in `test/dialect/roundtrip.toy` is
  there to catch.
- `StructType::get` asserts on an empty element list; the parser's `do/while`
  cannot produce one, but a caller could.
- Shape inference never sees a struct, because `allOperandsInferred` tests for
  `RankedTensorType` ([07](07-interfaces.md)). The pipeline runs a canonicalizer
  before inference so the folds happen first.
