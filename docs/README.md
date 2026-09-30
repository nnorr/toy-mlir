# Component walkthroughs

These documents follow the code in `include/toy/` and `src/` and explain how each
part is implemented. The design decisions behind the layout are in
[`../ARCHITECTURE.md`](../ARCHITECTURE.md); building and testing is in
[`../README.md`](../README.md).

The order matches the upstream [Toy tutorial](https://mlir.llvm.org/docs/Tutorials/Toy/),
so a chapter of the tutorial and the document beside it can be read together. The
upstream source for each chapter is under `reference/Ch1` through
`reference/Ch7`.

| Document | Files it covers | Upstream chapter |
| --- | --- | --- |
| [01. Lexer](01-lexer.md) | `Lexer.{h,cpp}` | Ch1, the Toy language and its lexer |
| [02. AST](02-ast.md) | `AST.h`, `ASTVisitor.h`, `ASTDumper.{h,cpp}` | Ch1 |
| [03. Parser](03-parser.md) | `Parser.{h,cpp}` | Ch1 |
| [04. MLIRGen](04-mlirgen.md) | `MLIRGen.{h,cpp}` | Ch2, emitting basic MLIR |
| [05. The dialect](05-dialect.md) | `Ops.td`, `Dialect.h`, `dialect/{ToyDialect,Ops}.cpp` | Ch2 |
| [06. Patterns and folding](06-patterns-and-folding.md) | `dialect/ToyCombine.{td,cpp}`, `dialect/Folders.cpp` | Ch3, high-level transformation |
| [07. Interfaces](07-interfaces.md) | `ShapeInferenceInterface.{h,td}`, `dialect/Interfaces.cpp`, `passes/ShapeInference.cpp` | Ch4, generic transformation through interfaces |
| [08. Lowering to affine](08-lowering-to-affine.md) | `passes/LowerToAffine.cpp` | Ch5, partial lowering |
| [09. Lowering to LLVM](09-lowering-to-llvm.md) | `passes/LowerToLLVM.cpp` | Ch6, lowering to LLVM and code generation |
| [10. The struct type](10-struct-type.md) | `dialect/StructType.cpp`, the struct paths in `MLIRGen.cpp` and `Parser.cpp` | Ch7, adding a composite type |
| [11. Driver and pipeline](11-driver-and-pipeline.md) | `Pipeline.{h,cpp}`, `main.cpp` | Ch6's `toyc.cpp`, reorganized |
| [12. Debug info and object files](12-debug-info-and-objects.md) | `DebugInfo.{h,cpp}`, `ObjectEmitter.{h,cpp}`, `Translate.{h,cpp}`, `Jit.{h,cpp}` | past Ch7 |
| [13. Testing and equivalence](13-testing-and-equivalence.md) | `test/`, `tests/`, `tests/compat/` | none |

## Why the design is what it is

[`adr/`](adr/README.md) holds fifteen decision records. The walkthroughs above
explain how the code works and `../ARCHITECTURE.md` explains what the design is;
the records explain what else was considered and what each choice cost. Several
were forced by a compile or link error rather than chosen, and those records say so.

## Where to start

Reading 01 through 09 in order follows the tutorial and builds up the compiler
one layer at a time.

To understand the IR before the code that produces it, read `include/toy/Ops.td`
first, then jump to 05.

For the parts that have no upstream equivalent, read 11 and 12 for the driver and
the back end, and 13 for how the repo proves it matches `toyc-ch7`.
