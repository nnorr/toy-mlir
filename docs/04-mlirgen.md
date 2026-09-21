# 04. MLIRGen

Files: `include/toy/MLIRGen.h` (48), `src/MLIRGen.cpp` (687)

## What it does

`MLIRGen` turns the AST into Toy dialect IR. It is the one-way door out of the front end: after it returns, the program is `mlir::Operation`s and nothing downstream knows that Toy source text existed.

The tree walk is not here. `MLIRGenImpl` derives from `ASTVisitor`, which owns the switch and shares it with the dumper ([02-ast.md](02-ast.md)). This file holds the meaning of each node, which operation to build with which types and which location, plus the two tables that meaning depends on.

## Two conventions the rest follows from

The file comment states them (`src/MLIRGen.cpp:18`):

Every function is generic. Parameters and results are `tensor<*xf64>`, and real shapes arrive later from inlining plus shape inference. That is why a prototype is built with no result type at all and the result is patched in afterwards from whatever the body returned.

A hook returns `FailureOr<Value>`. Expressions yield the value they define, statements yield success with a null `Value`, and a failure has already emitted its diagnostic. Upstream signals the same three cases with a null `Value` plus a separate `LogicalResult` overload per statement kind.

## The builder and insertion points

`mlir::OpBuilder` is a stateful factory (`src/MLIRGen.cpp:125`). It holds an insertion point, and every `Op::create(builder, ...)` inserts there. Generation moves the point as it descends:

```c++
    builder.setInsertionPointToEnd(theModule.getBody());     // for the next function
    mlir::toy::FuncOp function = mlirGenPrototype(*funcAST.getProto());
    ...
    builder.setInsertionPointToStart(&entryBlock);           // for the body
```

`ModuleOp` is created detached, with an unknown location (`src/MLIRGen.cpp:91`):

```c++
    theModule = mlir::ModuleOp::create(builder.getUnknownLoc());
```

The unknown location is visible in the output, and it matters later: the debug-info pass has to hunt for a real file location because the module itself has none (see the debug-info document in this directory).

```console
$ build/bin/toyc docs/examples/ex.toy -emit=mlir -opt --mlir-print-debuginfo 2>&1 | tail -3
    toy.return loc("docs/examples/ex.toy":6:1)
  } loc("docs/examples/ex.toy":6:1)
} loc(unknown)
```

## Locations

One helper converts a front-end location to an MLIR one (`src/MLIRGen.cpp:150`):

```c++
  mlir::Location loc(const Location &loc) {
    return mlir::FileLineColLoc::get(builder.getStringAttr(*loc.file), loc.line,
                                     loc.col);
  }
```

Every operation gets one. Nothing else in the compiler does any debug-info bookkeeping, because locations propagate on their own through inlining, shape inference and both lowerings. Compare that with the Kaleidoscope compiler in `06_llvm_tutorial`, which builds `DIBuilder` metadata by hand while emitting IR.

## The symbol tables

Variables live in a `llvm::ScopedHashTable` (`src/MLIRGen.cpp:130`):

```c++
  llvm::ScopedHashTable<StringRef, std::pair<mlir::Value, VarDeclExprAST *>>
      symbolTable;
```

The value is what the name resolves to. The declaration is kept alongside it because struct member access needs the declared type name, which a `Value` does not carry.

Scopes are RAII. `mlirGenFunction` opens one for the parameters, and `mlirGenBlock` opens another for the body:

```c++
    // The parameters live in this scope, so it has to outlive the body walk.
    SymbolTableScopeT varScope(symbolTable);
```

Two more maps: `functionMap` records generated functions so a later call can find the callee's result type, and `structMap` maps a struct name to its MLIR type and its AST node. Toy has no forward declarations, so definition order is call order.

## Functions

`mlirGenPrototype` builds the signature with no results (`src/MLIRGen.cpp:246`):

```c++
    auto funcType = builder.getFunctionType(argTypes, /*results=*/{});
    return mlir::toy::FuncOp::create(builder, location, proto.getName(),
                                     funcType);
```

Parameters become block arguments of the entry block, and the names are bound to them (`src/MLIRGen.cpp:264`):

```c++
    // Block arguments are the parameters' values; bind the names to them.
    for (const auto nameValue :
         llvm::zip(protoArgs, entryBlock.getArguments())) {
      if (failed(declare(*std::get<0>(nameValue), std::get<1>(nameValue))))
        return nullptr;
    }
```

After the body, three things happen (`src/MLIRGen.cpp:280`). A block must end in a terminator, so a missing `return` gets one. If the body did return a value, the signature is updated to match, which is the deferred result type arriving. And every function except `main` is marked private:

```c++
    // Private visibility is what allows the inliner to delete a callee once it
    // has been inlined everywhere; `main` must stay, since it is the entry
    // point the JIT and the object file expose.
    if (funcAST.getProto()->getName() != "main")
      function.setPrivate();
```

That one line is why `-opt` output contains only `main`. The inliner only deletes a callee it can prove is unused, and a public symbol could be called from outside the module. In the unoptimized output the modifier is visible:

```console
$ build/bin/toyc docs/examples/ex.toy -emit=mlir 2>&1 | sed -n '2p'
  toy.func private @multiply_transpose(%arg0: tensor<*xf64>, %arg1: tensor<*xf64>) -> tensor<*xf64> {
```

On failure the half-built function is erased (`src/MLIRGen.cpp:273`), because leaving it in the module would fail verification with a confusing second error.

## Statements versus expressions

`mlirGenBlock` handles declarations, `return` and `print` itself (`src/MLIRGen.cpp:314`), because each carries a rule that only applies at statement level: a declaration adds to this scope, a return ends the block, and a print produces no value.

`mlirGenExpr` guards the other direction (`src/MLIRGen.cpp:345`). The parser can place a `print` inside an expression, so the expression context rejects the three statement kinds. Upstream did that in its dispatch switch's `default:` case; since the visitor has a hook for every kind, the check moved to the one place that knows the context.

## What each node becomes

A number is a rank-0 constant. A tensor literal is one constant holding the flattened data, built by `getConstantAttr` plus `collectData` (`src/MLIRGen.cpp:436`, `:485`). The data becomes an attribute rather than operands because it is known at compile time.

A declaration with an explicit shape emits a reshape (`src/MLIRGen.cpp:527`):

```c++
    } else if (!varType.shape.empty()) {
      // A declared shape is Toy's only reshape: emit one and let
      // canonicalization remove it when it turns out to be a no-op.
      value = mlir::Value(ReshapeOp::create(
          builder, loc(vardecl.loc()), getType(varType.shape), *value));
```

This is why `var a<2, 3> = [[1, 2, 3], [4, 5, 6]];` produces a `toy.reshape` from `tensor<2x3xf64>` to `tensor<2x3xf64>`, a no-op that the canonicalizer removes later ([06-patterns-and-folding.md](06-patterns-and-folding.md)).

`transpose` is a builtin with its own operation; any other callee becomes a `generic_call` whose result type is the callee's declared result, still unranked at this point (`src/MLIRGen.cpp:658`).

The `.` operator is resolved at compile time. `getStructFor` recovers which struct a sub-expression evaluates to, walking through nested accesses, and `getMemberIndex` turns the member name into a position in the definition (`src/MLIRGen.cpp:368`, `:407`). Toy has no type checker, so this recovery from declarations is the closest thing to one.

## Verification before handing off

```c++
    // Verify before handing the module on. A generator bug caught here points
    // at the op that is wrong; the same bug caught three passes later does not.
    if (failed(mlir::verify(theModule))) {
      theModule.emitError("module verification error");
      return nullptr;
    }
```

## Deviation D4: redeclaration is reported

Upstream's `declare` returns failure with no diagnostic, so a redeclaration makes the whole enclosing function disappear from the output with nothing printed. This version reports it (`src/MLIRGen.cpp:165`):

```c++
  llvm::LogicalResult declare(VarDeclExprAST &var, mlir::Value value) {
    if (symbolTable.count(var.getName()))
      return mlir::emitError(loc(var.loc()))
             << "variable '" << var.getName()
             << "' is already declared in this scope";
    symbolTable.insert(var.getName(), {value, &var});
    return mlir::success();
  }
```

```console
$ build/bin/toyc docs/examples/redecl.toy -emit=mlir 2>&1
loc("docs/examples/redecl.toy":4:3): error: variable 'a' is already declared in this scope

$ ~/dev/08_mlir_toy/build/bin/toyc-ch7 docs/examples/redecl.toy -emit=mlir 2>&1
$ echo $?
1
```

Upstream prints nothing at all and exits 1.

## An upstream defect reproduced on purpose

`mlir::emitError` already prints `error: `. Upstream's messages spell it out again, so its diagnostics read:

```console
$ ~/dev/08_mlir_toy/build/bin/toyc-ch7 docs/examples/unk.toy -emit=mlir 2>&1 | head -1
loc("docs/examples/unk.toy":3:9): error: error: unknown variable 'nope'
```

Those strings are compared against `toyc-ch7` byte for byte by `tests/compat`, so they keep the doubled prefix here too (`src/MLIRGen.cpp:586` and elsewhere). D4's message is ours, so it is not doubled. The comment at `src/MLIRGen.cpp:160` records the reasoning, so that a later reader does not "fix" one and break the sweep.

One more upstream behavior is kept deliberately: a `print` that fails to generate returns success from the block walk (`src/MLIRGen.cpp:324`), so the driver exits 0 after printing a diagnostic.

## Try it

```console
$ build/bin/toyc <file>.toy -emit=mlir              # what MLIRGen produced
$ build/bin/toyc <file>.toy -emit=mlir -opt         # after inlining and shape inference
$ build/bin/toyc <file>.toy -emit=mlir --mlir-print-debuginfo    # with locations
```

The unranked-to-ranked transition is the thing to look at:

```console
$ build/bin/toyc docs/examples/ex.toy -emit=mlir 2>&1 | sed -n '9,14p'
    %0 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
    %1 = toy.reshape(%0 : tensor<2x3xf64>) to tensor<2x3xf64>
    %2 = toy.constant dense<[1.000000e+00, 2.000000e+00, 3.000000e+00, 4.000000e+00, 5.000000e+00, 6.000000e+00]> : tensor<6xf64>
    %3 = toy.reshape(%2 : tensor<6xf64>) to tensor<2x3xf64>
    %4 = toy.generic_call @multiply_transpose(%1, %3) : (tensor<2x3xf64>, tensor<2x3xf64>) -> tensor<*xf64>
    toy.print %4 : tensor<*xf64>

$ build/bin/toyc docs/examples/ex.toy -emit=mlir -opt 2>&1
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

The callee is gone, the two reshapes are gone, the call is gone, every type is ranked, and the two identical transposes collapsed into one. None of that happened in this file; it is the pass pipeline acting on what this file produced.

## Pitfalls

`getType(shape)` returns an unranked tensor for an empty shape, so a declaration without a type annotation produces `tensor<*xf64>` and relies on shape inference. A shape that is present but wrong is not checked here; the reshape is emitted and the failure surfaces in `DenseElementsAttr::reshape`, which asserts. `reference/tests/Ch*/scalar.toy` hits exactly that, in both compilers.

`functionMap` is consulted for the callee's result type, so calling a function defined later in the file fails with `no defined function found`. Toy has no forward declarations.

The struct type is compared by identity in a struct-typed declaration (`src/MLIRGen.cpp:520`). Since MLIR types are uniqued, two structurally identical structs are the same type, and two structs with different element types can never be assigned to each other.
