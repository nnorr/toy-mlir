//===- Ops.td - Toy dialect operation definitions ----------*- tablegen -*-===//
//
// Adapted from mlir/examples/toy/Ch7/include/toy/Ops.td in the LLVM Project,
// under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The whole Toy IR in one file: one dialect, one type, eleven operations.
//
// This is ODS (Operation Definition Specification). mlir-tblgen turns each
// `def` below into a C++ class with accessors, builders, verifiers, a parser
// and a printer, so the facts about an operation live in one place. Read
// the generated result with:
//
//   mlir-tblgen -gen-op-decls include/toy/Ops.td -I <mlir>/include -I include
//
// The op set is deliberately identical to the upstream tutorial's Ch7: this
// repo restructures the *implementation*, not the language, so that the IR it
// emits can be diffed against toyc-ch7 output (see tests/compat).
//
//===----------------------------------------------------------------------===//

#ifndef TOY_OPS
#define TOY_OPS

include "mlir/IR/SymbolInterfaces.td"
include "mlir/Interfaces/CallInterfaces.td"
include "mlir/Interfaces/CastInterfaces.td"
include "mlir/Interfaces/FunctionInterfaces.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "toy/ShapeInferenceInterface.td"

//===----------------------------------------------------------------------===//
// Dialect
//===----------------------------------------------------------------------===//

// A dialect is a namespace: it owns the `toy.` prefix and, through the bits
// below, the hooks MLIR calls back into for Toy-specific behaviour.
def Toy_Dialect : Dialect {
  let name = "toy";
  let cppNamespace = "::mlir::toy";

  let summary = "A high-level dialect for the Toy language";
  let description = [{
    Toy models a tiny tensor language: rank <= 2 values of f64, immutable, with
    two builtins (transpose, print) and user-defined generic functions. Shapes
    are inferred rather than declared. That inference is why the dialect exists:
    it needs Toy's own semantics, and those are gone by the time the program
    reaches LLVM IR.
  }];

  // Generates materializeConstant(), the hook generic transformations call when
  // a folder produced an Attribute and someone has to turn it back into an op.
  // Without it, folding struct constants would leave the IR unbuildable.
  let hasConstantMaterializer = 1;

  // Generates the declarations of parseType/printType, which we implement in
  // dialect/StructType.cpp for !toy.struct<...>.
  let useDefaultTypePrinterParser = 1;

  let extraClassDeclaration = [{
    /// Registers !toy.struct with this dialect.
    ///
    /// Separate from initialize() on purpose: addTypes<StructType>() has to see
    /// the complete StructTypeStorage, so it belongs in the translation unit
    /// that defines it (dialect/StructType.cpp). Upstream Toy has one big
    /// Dialect.cpp and never faces the question; every in-tree MLIR dialect
    /// that splits its types out uses exactly this hook.
    void registerTypes();

    /// Registers the dialect interfaces with this dialect.
    ///
    /// Same reasoning as registerTypes(): ToyInlinerInterface is private to
    /// dialect/Interfaces.cpp, so the registration belongs there too. Dialect
    /// interfaces could be registered from a free function, since
    /// addInterfaces() is public where addTypes() is protected, but paying for
    /// that with a declaration duplicated across two files is worse than one
    /// hook.
    void registerInterfaces();
  }];
}

// Every Toy operation inherits from this: it binds the op to the dialect above
// and gives one place to add traits that should apply dialect-wide.
class Toy_Op<string mnemonic, list<Trait> traits = []> :
    Op<Toy_Dialect, mnemonic, traits>;

//===----------------------------------------------------------------------===//
// Types
//===----------------------------------------------------------------------===//

// Exposes the C++ StructType (dialect/StructType.cpp) to ODS, so it can be used
// in `arguments`/`results` the same way F64Tensor is.
def Toy_StructType :
    DialectType<Toy_Dialect, CPred<"::llvm::isa<StructType>($_self)">,
                "Toy struct type">;

// The types a Toy value may have: a tensor of f64, or a struct of those.
def Toy_Type : AnyTypeOf<[F64Tensor, Toy_StructType]>;

//===----------------------------------------------------------------------===//
// ConstantOp
//===----------------------------------------------------------------------===//

def ConstantOp : Toy_Op<"constant",
    [ConstantLike, Pure,
     DeclareOpInterfaceMethods<ShapeInferenceOpInterface>]> {
  let summary = "constant";
  let description = [{
    Turns a literal into an SSA value. The data rides on the operation as an
    attribute, not as an operand:

    ```mlir
      %0 = toy.constant dense<[[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]]>
                        : tensor<2x3xf64>
    ```
  }];

  // An attribute, not an operand: known at compile time, no use-def edge.
  let arguments = (ins F64ElementsAttr:$value);
  let results = (outs F64Tensor);

  // `toy.constant dense<...> : tensor<...>` is not expressible in the
  // declarative format, so the parser and printer are written by hand in
  // dialect/Ops.cpp.
  let hasCustomAssemblyFormat = 1;

  let builders = [
    // From an already-built attribute: the type comes from the attribute.
    OpBuilder<(ins "DenseElementsAttr":$value), [{
      build($_builder, $_state, value.getType(), value);
    }]>,

    // From a plain double: builds the rank-0 tensor attribute for you.
    OpBuilder<(ins "double":$value)>
  ];

  // ODS checks the constraints above; this adds the check ODS cannot express
  // (that the attribute's shape matches the result type).
  let hasVerifier = 1;

  // Lets the constant fold to its own attribute, which is how ConstantLike
  // becomes useful to the generic folding machinery.
  let hasFolder = 1;
}

//===----------------------------------------------------------------------===//
// AddOp
//===----------------------------------------------------------------------===//

def AddOp : Toy_Op<"add",
    [Pure, DeclareOpInterfaceMethods<ShapeInferenceOpInterface>]> {
  let summary = "element-wise addition operation";
  let description = [{
    Element-wise addition of two tensors of matching shape.
  }];

  let arguments = (ins F64Tensor:$lhs, F64Tensor:$rhs);
  let results = (outs F64Tensor);

  // Shares one hand-written parser/printer with MulOp (dialect/Ops.cpp), which
  // is why the format is custom rather than declarative.
  let hasCustomAssemblyFormat = 1;

  let builders = [
    OpBuilder<(ins "Value":$lhs, "Value":$rhs)>
  ];
}

//===----------------------------------------------------------------------===//
// CastOp
//===----------------------------------------------------------------------===//

def CastOp : Toy_Op<"cast", [
     DeclareOpInterfaceMethods<CastOpInterface>,
     DeclareOpInterfaceMethods<ShapeInferenceOpInterface>,
     Pure,
     SameOperandsAndResultShape
  ]> {
  let summary = "shape cast operation";
  let description = [{
    Converts a tensor to an equivalent type without touching the data, e.g.
    `tensor<2x3xf64>` to `tensor<*xf64>`. This op exists for the inliner: a
    call site passes ranked tensors to a function whose parameters are
    unranked, and the inliner refuses to bridge that gap itself. It asks the
    dialect to materialize a cast instead (see dialect/Interfaces.cpp).
  }];

  let arguments = (ins F64Tensor:$input);
  let results = (outs F64Tensor:$output);

  let assemblyFormat = "$input attr-dict `:` type($input) `to` type($output)";
}

//===----------------------------------------------------------------------===//
// FuncOp
//===----------------------------------------------------------------------===//

def FuncOp : Toy_Op<"func", [
    FunctionOpInterface, IsolatedFromAbove
  ]> {
  let summary = "user defined function operation";
  let description = [{
    A callable region-holding operation:

    ```mlir
    toy.func @main() {
      %0 = toy.constant dense<5.500000e+00> : tensor<f64>
      %1 = toy.reshape(%0 : tensor<f64>) to tensor<2x2xf64>
      toy.print %1 : tensor<2x2xf64>
      toy.return
    }
    ```

    Toy defines its own function op rather than reusing func.func because its
    functions are generic: parameters start unranked and get their shapes from
    inlining plus shape inference. IsolatedFromAbove is what lets the pass
    manager schedule work on each function in parallel.
  }];

  let arguments = (ins
    SymbolNameAttr:$sym_name,
    TypeAttrOf<FunctionType>:$function_type,
    OptionalAttr<DictArrayAttr>:$arg_attrs,
    OptionalAttr<DictArrayAttr>:$res_attrs
  );
  let regions = (region AnyRegion:$body);

  let builders = [OpBuilder<(ins
    "StringRef":$name, "FunctionType":$type,
    CArg<"ArrayRef<NamedAttribute>", "{}">:$attrs)
  >];

  let extraClassDeclaration = [{
    //===------------------------------------------------------------------===//
    // FunctionOpInterface Methods
    //===------------------------------------------------------------------===//

    /// Returns the argument types of this function.
    ArrayRef<Type> getArgumentTypes() { return getFunctionType().getInputs(); }

    /// Returns the result types of this function.
    ArrayRef<Type> getResultTypes() { return getFunctionType().getResults(); }

    //===------------------------------------------------------------------===//
    // CallableOpInterface Methods
    //===------------------------------------------------------------------===//

    /// The region the inliner clones from, and the region shape inference
    /// walks. Without it, nothing would treat this op as callable.
    Region *getCallableRegion() { return &getBody(); }
  }];

  let hasCustomAssemblyFormat = 1;
  let skipDefaultBuilders = 1;
}

//===----------------------------------------------------------------------===//
// GenericCallOp
//===----------------------------------------------------------------------===//

def GenericCallOp : Toy_Op<"generic_call",
    [DeclareOpInterfaceMethods<CallOpInterface>]> {
  let summary = "generic call operation";
  let description = [{
    A call to a user-defined function, whose result shape is not yet known:

    ```mlir
     %4 = toy.generic_call @my_func(%1, %3)
           : (tensor<2x3xf64>, tensor<2x3xf64>) -> tensor<*xf64>
    ```

    "Generic" because the callee is shape-polymorphic. Implementing
    CallOpInterface is what lets MLIR's own inliner, which has never heard of
    Toy, recognise this as a call and remove it.
  }];

  let arguments = (ins
    FlatSymbolRefAttr:$callee,
    Variadic<Toy_Type>:$inputs,
    // Required by CallOpInterface, unused by Toy itself.
    OptionalAttr<DictArrayAttr>:$arg_attrs,
    OptionalAttr<DictArrayAttr>:$res_attrs
  );

  let results = (outs Toy_Type);

  let assemblyFormat = [{
    $callee `(` $inputs `)` attr-dict `:` functional-type($inputs, results)
  }];

  let builders = [
    OpBuilder<(ins "Type":$result_type, "StringRef":$callee,
              "ArrayRef<Value>":$arguments)>
  ];
}

//===----------------------------------------------------------------------===//
// MulOp
//===----------------------------------------------------------------------===//

def MulOp : Toy_Op<"mul",
    [Pure, DeclareOpInterfaceMethods<ShapeInferenceOpInterface>]> {
  let summary = "element-wise multiplication operation";
  let description = [{
    Element-wise multiplication of two tensors of matching shape.
  }];

  let arguments = (ins F64Tensor:$lhs, F64Tensor:$rhs);
  let results = (outs F64Tensor);

  let hasCustomAssemblyFormat = 1;

  let builders = [
    OpBuilder<(ins "Value":$lhs, "Value":$rhs)>
  ];
}

//===----------------------------------------------------------------------===//
// PrintOp
//===----------------------------------------------------------------------===//

def PrintOp : Toy_Op<"print"> {
  let summary = "print operation";
  let description = [{
    Prints its input and returns nothing.

    There is no Pure trait here: printing is a side effect, so the canonicalizer
    must not delete this op even though its result is unused. The input also
    accepts a memref, which is the seam that lets the affine lowering convert
    everything around this op while leaving the op itself for the LLVM lowering
    to handle.
  }];

  let arguments = (ins AnyTypeOf<[F64Tensor, F64MemRef]>:$input);

  let assemblyFormat = "$input attr-dict `:` type($input)";
}

//===----------------------------------------------------------------------===//
// ReshapeOp
//===----------------------------------------------------------------------===//

def ReshapeOp : Toy_Op<"reshape", [Pure]> {
  let summary = "tensor reshape operation";
  let description = [{
    Reinterprets a tensor with the same element count under a new shape:

    ```mlir
       %0 = toy.reshape (%arg1 : tensor<10xf64>) to tensor<5x2xf64>
    ```
  }];

  let arguments = (ins F64Tensor:$input);
  // Statically shaped: a reshape whose target shape is unknown is meaningless.
  let results = (outs StaticShapeTensorOf<[F64]>);

  let assemblyFormat = [{
    `(` $input `:` type($input) `)` attr-dict `to` type(results)
  }];

  // Three DRR rules in dialect/ToyCombine.td plus one C++ registration hook.
  let hasCanonicalizer = 1;
}

//===----------------------------------------------------------------------===//
// ReturnOp
//===----------------------------------------------------------------------===//

def ReturnOp : Toy_Op<"return", [Pure, HasParent<"FuncOp">, Terminator]> {
  let summary = "return operation";
  let description = [{
    Terminates a toy.func, with an optional operand:

    ```mlir
      toy.func @foo() -> tensor<2xf64> {
        ...
        toy.return %0 : tensor<2xf64>
      }
    ```
  }];

  let arguments = (ins Variadic<Toy_Type>:$input);

  // The `^` marks the group that is only printed when $input is present, which
  // is how `toy.return` and `toy.return %0 : tensor<2xf64>` share one format.
  let assemblyFormat = "($input^ `:` type($input))? attr-dict ";

  let builders = [
    OpBuilder<(ins), [{ build($_builder, $_state, {}); }]>
  ];

  let extraClassDeclaration = [{
    bool hasOperand() { return getNumOperands() != 0; }
  }];

  // Checks the operand against the enclosing function's signature, a
  // relationship between two ops that no ODS constraint can state.
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//
// StructAccessOp
//===----------------------------------------------------------------------===//

def StructAccessOp : Toy_Op<"struct_access", [Pure]> {
  let summary = "struct access";
  let description = [{
    Extracts the Nth element of a struct value.
  }];

  let arguments = (ins Toy_StructType:$input, I64Attr:$index);
  let results = (outs Toy_Type:$output);

  let assemblyFormat = [{
    $input `[` $index `]` attr-dict `:` type($input) `->` type($output)
  }];

  let builders = [
    OpBuilder<(ins "Value":$input, "size_t":$index)>
  ];

  let hasVerifier = 1;

  // struct_access(struct_constant) folds to the element's attribute. Inlining
  // creates that pattern in bulk, and folding it is what makes structs vanish
  // before lowering, so Ch6's pipeline needs no changes to support them.
  let hasFolder = 1;
}

//===----------------------------------------------------------------------===//
// StructConstantOp
//===----------------------------------------------------------------------===//

def StructConstantOp : Toy_Op<"struct_constant", [ConstantLike, Pure]> {
  let summary = "struct constant";
  let description = [{
    A struct literal, encoded as an array of the elements' constant attributes:

    ```mlir
      %0 = toy.struct_constant [
        dense<[[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]]> : tensor<2x3xf64>
      ] : !toy.struct<tensor<*xf64>>
    ```
  }];

  let arguments = (ins ArrayAttr:$value);
  let results = (outs Toy_StructType:$output);

  let assemblyFormat = "$value attr-dict `:` type($output)";

  let hasVerifier = 1;
  let hasFolder = 1;
}

//===----------------------------------------------------------------------===//
// TransposeOp
//===----------------------------------------------------------------------===//

def TransposeOp : Toy_Op<"transpose",
    [Pure, DeclareOpInterfaceMethods<ShapeInferenceOpInterface>]> {
  let summary = "transpose operation";
  let description = [{
    Transposes a rank-2 tensor. transpose(transpose(x)) folds to x, the
    canonicalization Toy uses to introduce pattern rewriting. It is only
    expressible while the op still means "transpose" rather than a loop nest
    over a memref.
  }];

  let arguments = (ins F64Tensor:$input);
  let results = (outs F64Tensor);

  let assemblyFormat = [{
    `(` $input `:` type($input) `)` attr-dict `to` type(results)
  }];

  // The hand-written C++ pattern lives in dialect/ToyCombine.cpp.
  let hasCanonicalizer = 1;

  let builders = [
    OpBuilder<(ins "Value":$input)>
  ];

  let hasVerifier = 1;
}

#endif // TOY_OPS
