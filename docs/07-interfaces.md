# 07. Interfaces

Files: `include/toy/ShapeInferenceInterface.td`, `src/dialect/Interfaces.cpp`,
`src/passes/ShapeInference.cpp`, generated
`build/include/toy/ShapeInferenceOpInterfaces.{h,cpp}.inc`.

## What this is for

MLIR ships an inliner, a canonicalizer and a CSE pass. None of them has heard of
Toy, yet all three transform Toy IR. Interfaces are the reason. An interface is a
contract an operation or a dialect implements so that generic code can ask it a
question instead of switching on a list of operations it knows.

Toy needs this twice over:

- It consumes MLIR's interfaces (`CallOpInterface`, `CastOpInterface`,
  `FunctionOpInterface`) so MLIR's inliner can process `toy.generic_call`.
- It defines one of its own, `ShapeInference`, so its own pass can infer shapes
  without knowing what `toy.transpose` is.

## Interface versus trait

Both are listed in the same place in ODS, which makes them easy to confuse.

| | Trait | Interface |
| --- | --- | --- |
| Carries | a fact | a method |
| Example | `Pure`, `Terminator`, `IsolatedFromAbove` | `ShapeInference`, `CallOpInterface` |
| Checked by | `op->hasTrait<Pure>()` | `dyn_cast<ShapeInference>(op)` |
| Costs | nothing at runtime | one indirect call |

A trait says *this operation has no side effects*. An interface says *ask this
operation to infer its shapes*: there is code behind it, and the code differs per
operation. See [05-dialect.md](05-dialect.md) for the trait side.

## Op interface versus dialect interface

`src/dialect/Interfaces.cpp:12-22` draws the line, and it is worth stating
plainly because the two are registered differently:

- A **dialect** interface answers for the dialect as a whole. The inliner asks
  "may I inline across this dialect's calls, and how do I fix a type mismatch?"
  once, and the answer covers every Toy operation.
- An **op** interface is implemented per operation. `ShapeInference` has a
  different body on `toy.transpose` than on `toy.mul`.

## Toy's own interface, in 12 lines of ODS

```tablegen
def ShapeInferenceOpInterface : OpInterface<"ShapeInference"> {
  let methods = [
    InterfaceMethod<"Infer and set the output shape for the current operation.",
                    "void", "inferShapes">
  ];
}
```

`include/toy/ShapeInferenceInterface.td:25-35`. An operation opts in from
`Ops.td`:

```tablegen
def MulOp : Toy_Op<"mul",
    [Pure, DeclareOpInterfaceMethods<ShapeInferenceOpInterface>]> {
```

`include/toy/Ops.td:152-153`. `DeclareOpInterfaceMethods` declares
`MulOp::inferShapes()` and leaves you to define it.

## How `dyn_cast<ShapeInference>(op)` works

This is how MLIR dispatches onto types that did not exist when the caller was
compiled. mlir-tblgen generates a Concept/Model pair in
`build/include/toy/ShapeInferenceOpInterfaces.h.inc`:

```c++
struct Concept {
  /// The methods defined by the interface.
  void (*inferShapes)(const Concept *impl, ::mlir::Operation *);
};
template<typename ConcreteOp>
class Model : public Concept {
public:
  using Interface = ShapeInference;
  Model() : Concept{inferShapes} {}
  static inline void inferShapes(const Concept *impl,
                                 ::mlir::Operation *tablegen_opaque_val);
};
```

`Concept` is a hand-rolled vtable: one function pointer per interface method.
`Model<ConcreteOp>` fills it in, and its thunk casts the opaque `Operation*` back
to the concrete operation:

```c++
template<typename ConcreteOp>
void detail::ShapeInferenceInterfaceTraits::Model<ConcreteOp>::inferShapes(
    const Concept *impl, ::mlir::Operation *tablegen_opaque_val) {
  return (llvm::cast<ConcreteOp>(tablegen_opaque_val)).inferShapes();
}
```

A `Model` instance is registered per operation when the dialect is loaded, so
`dyn_cast<ShapeInference>(op)` is a lookup of the interface in that operation's
registered interface map. It is worth being explicit that this is neither C++
RTTI nor a virtual call on the operation, since the spelling suggests both. The
call you write, `shapeOp.inferShapes()`, is one line
(`ShapeInferenceOpInterfaces.cpp.inc`):

```c++
void ShapeInference::inferShapes() {
  return getImpl()->inferShapes(getImpl(), getOperation());
}
```

### Where those definitions are compiled

`src/dialect/Interfaces.cpp:52` includes
`toy/ShapeInferenceOpInterfaces.cpp.inc`. Upstream includes it in its shape
inference pass instead. The placement matters here because this repo builds
separate libraries: every operation declaring the interface needs that symbol, so
compiling it into the pass would make `libToyDialect.a` unlinkable without
`libToyPasses.a`. `src/passes/ShapeInference.cpp:45-49` records the same point
from the other side.

## The shape rules

Each is one or two lines. That brevity is the argument for interfaces: the
knowledge lives with the operation.

```c++
void AddOp::inferShapes() { getResult().setType(getLhs().getType()); }   // Ops.cpp:229
void MulOp::inferShapes() { getResult().setType(getLhs().getType()); }   // Ops.cpp:340
void CastOp::inferShapes() { getResult().setType(getInput().getType()); } // Ops.cpp:235

void TransposeOp::inferShapes() {                                        // Ops.cpp:428
  auto arrayTy = llvm::cast<RankedTensorType>(getOperand().getType());
  SmallVector<int64_t, 2> dims(llvm::reverse(arrayTy.getShape()));
  getResult().setType(RankedTensorType::get(dims, arrayTy.getElementType()));
}
```

## The inliner, and why Toy must supply a cast

Toy inlines because it has to, before any shape can be inferred
(`src/dialect/Interfaces.cpp:62-64`): functions are shape-polymorphic, so shapes
resolve only once each call has been replaced by the callee's body with concrete
types in place.

`ToyInlinerInterface` implements five hooks. Three are permission checks that
return `true` unconditionally, since Toy has no recursion, no indirect calls and
no cost model. The two interesting ones:

```c++
void handleTerminator(Operation *op, ValueRange valuesToRepl) const final {
  auto returnOp = cast<ReturnOp>(op);
  assert(returnOp.getNumOperands() == valuesToRepl.size());
  for (const auto &it : llvm::enumerate(returnOp.getOperands()))
    valuesToRepl[it.index()].replaceAllUsesWith(it.value());
}
```

`src/dialect/Interfaces.cpp:98-105`. A `toy.return` cannot survive inlining,
because control no longer leaves a function there, and only the dialect knows
which terminator operands correspond to which call results.

```c++
Operation *materializeCallConversion(OpBuilder &builder, Value input,
                                     Type resultType,
                                     Location conversionLoc) const final {
  return CastOp::create(builder, conversionLoc, resultType, input);
}
```

`src/dialect/Interfaces.cpp:113-117`. The call passes `tensor<2x3xf64>` to a
parameter typed `tensor<*xf64>`. The inliner refuses to paper over that; it asks
the dialect for an operation that converts. Without this hook, inlining silently
does nothing: nothing crashes and nothing is reported, which is what makes the
failure hard to place.

`toy.generic_call` becomes visible to the inliner through four
`CallOpInterface` methods (`src/dialect/Ops.cpp:324-336`):

```c++
CallInterfaceCallable GenericCallOp::getCallableForCallee() {
  return (*this)->getAttrOfType<SymbolRefAttr>("callee");
}
void GenericCallOp::setCalleeFromCallable(CallInterfaceCallable callee) {
  (*this)->setAttr("callee", cast<SymbolRefAttr>(callee));
}
Operation::operand_range GenericCallOp::getArgOperands() { return getInputs(); }
MutableOperandRange GenericCallOp::getArgOperandsMutable() {
  return getInputsMutable();
}
```

The callable side is `FuncOp::getCallableRegion()`, declared inline in
`include/toy/Ops.td:215-218`. Returning the body is what makes the operation
callable at all.

## The pass

`src/passes/ShapeInference.cpp:72-106`. A worklist, not a single traversal,
because operations are not necessarily ordered so that one walk sees every
operand already resolved:

1. Collect every operation returning a dynamically shaped result.
2. Repeatedly pick one whose operands are all ranked, and let it infer.
3. If none qualifies, stop. A non-empty worklist then means failure.

```c++
while (!opWorklist.empty()) {
  auto nextop = llvm::find_if(opWorklist, allOperandsInferred);
  if (nextop == opWorklist.end())
    break;
  Operation *op = *nextop;
  opWorklist.erase(op);
  if (auto shapeOp = dyn_cast<ShapeInference>(op)) {
    shapeOp.inferShapes();
  } else {
    op->emitError("unable to infer shape of operation without shape "
                  "inference interface");
    return signalPassFailure();
  }
}
```

The `dyn_cast` is the interface check described above. The `else` branch is the
contract being enforced: an operation producing an unranked result with no way to
resolve it would otherwise stall the loop silently.

## Try it

Input: `reference/tests/Ch4/codegen.toy`, a generic `multiply_transpose(a, b)`
called twice from `main`.

```console
$ build/bin/toyc reference/tests/Ch4/codegen.toy -emit=mlir 2>&1
module {
  toy.func private @multiply_transpose(%arg0: tensor<*xf64>, %arg1: tensor<*xf64>) -> tensor<*xf64> {
    %0 = toy.transpose(%arg0 : tensor<*xf64>) to tensor<*xf64>
    %1 = toy.transpose(%arg1 : tensor<*xf64>) to tensor<*xf64>
    %2 = toy.mul %0, %1 : tensor<*xf64>
    toy.return %2 : tensor<*xf64>
  }
  toy.func @main() {
    %0 = toy.constant dense<[[1.000000e+00, ...]]> : tensor<2x3xf64>
    %1 = toy.reshape(%0 : tensor<2x3xf64>) to tensor<2x3xf64>
    %2 = toy.constant dense<[1.000000e+00, ...]> : tensor<6xf64>
    %3 = toy.reshape(%2 : tensor<6xf64>) to tensor<2x3xf64>
    %4 = toy.generic_call @multiply_transpose(%1, %3) : (tensor<2x3xf64>, tensor<2x3xf64>) -> tensor<*xf64>
    %5 = toy.generic_call @multiply_transpose(%3, %1) : (tensor<2x3xf64>, tensor<2x3xf64>) -> tensor<*xf64>
    toy.print %5 : tensor<*xf64>
    toy.return
  }
}
```

Everything is `tensor<*xf64>`. Now with `-opt`:

```console
$ build/bin/toyc reference/tests/Ch4/codegen.toy -emit=mlir -opt 2>&1
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

The generic function is gone, every type is ranked, and the two transposes have
collapsed into one. To watch it happen pass by pass:

```console
$ build/bin/toyc reference/tests/Ch4/codegen.toy -emit=mlir -opt --mlir-print-ir-after-all 2>&1
```

The interesting frames, abridged. The `<-` note on the `toy.cast` line is added
here, not printed by the compiler:

```
// IR Dump After CanonicalizerPass  (run on the callee, before inlining)
// IR Dump After CanonicalizerPass  (the reshapes in main fold away)
  %1 = toy.cast %0 : tensor<2x3xf64> to tensor<*xf64>      <- materializeCallConversion
  %2 = toy.cast %0 : tensor<2x3xf64> to tensor<*xf64>
  %3 = toy.transpose(%1 : tensor<*xf64>) to tensor<*xf64>

// IR Dump After InlinerPass
    ... the callee's body is now in main, still unranked ...

// IR Dump After (anonymous namespace)::ShapeInferencePass: toy-shape-inference
  %1 = toy.cast %0 : tensor<2x3xf64> to tensor<2x3xf64>
  %3 = toy.transpose(%1 : tensor<2x3xf64>) to tensor<3x2xf64>
  %5 = toy.mul %3, %4 : tensor<3x2xf64>
```

Two things to point at in a presentation. The `toy.cast` operations appear
*before* the inliner dump, because the inliner inserts them through the hook
while preparing the call. And the casts become no-ops (`2x3` to `2x3`) after
inference, which is why the later canonicalizer can delete them.

## Pitfalls

- Forgetting `materializeCallConversion` produces no error. Inlining just does
  not happen, and every later stage then fails for an unrelated-looking reason
  (unranked types reaching the affine lowering).
- The interface definitions must be compiled exactly once. Included in two
  translation units, the link fails with duplicate symbols; in none, with
  undefined ones.
- `allOperandsInferred` checks for `RankedTensorType`, so an operand of any other
  type (a `!toy.struct`, say) never becomes ready. Structs are folded away before
  this pass runs; see [10-struct-type.md](10-struct-type.md).
- The pass is anchored on `toy::FuncOp`
  (`src/passes/ShapeInference.cpp:66-67`), so it only ever sees one function.
  Inter-procedural inference was never the design; inlining made it unnecessary.
