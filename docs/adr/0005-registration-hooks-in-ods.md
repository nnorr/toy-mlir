# 0005. Registration hooks declared in ODS, defined beside what they register

Status: accepted
Provenance: recorded in the implementing session

## Context

Also forced by a compile error, and hit independently by two implementers.

`ToyDialect::initialize()` has to call `addTypes<StructType>()`. That instantiates
the storage uniquer, which needs the complete `StructTypeStorage`, and
`include/toy/Dialect.h` only forward-declares it. Put `initialize()` in
`ToyDialect.cpp` and the type's storage in `StructType.cpp`, and the call does not
compile: `static_cast from 'BaseStorage *' ... not related by inheritance`, plus an
incomplete-type error in a type trait.

Upstream never meets this, because it has one `Dialect.cpp` holding the dialect,
the operations, the type and its storage together.

## Options considered

| Option | Trade-off |
| --- | --- |
| A private `src/dialect/TypeDetail.h` included by both files | MLIR's own convention, cf. `mlir/lib/IR/TypeDetail.h`. One implementer had this written and working. The storage becomes visible to more than one translation unit |
| Keep everything in one `Dialect.cpp`, as upstream does | No hook needed. Gives up the file split that [0002](0002-four-layered-static-libraries.md) and readability wanted |
| A `registerTypes()` hook declared in ODS, defined in the file that defines the storage | The storage never leaves `StructType.cpp`. Adds an indirection whose reason is not obvious without a comment |

## Decision

The hook. `Toy_Dialect`'s `extraClassDeclaration` declares `registerTypes()` and
`registerInterfaces()`; `initialize()` calls them; each is defined in the file
that defines what it registers. The already-working `TypeDetail.h` was deleted in
favour of it, because it keeps the storage private to one translation unit, and
because in-tree MLIR dialects that split their types out use this same hook.

`registerInterfaces()` came second, for symmetry. It is not strictly forced:
`addInterfaces` is public where `addTypes` is protected, so a free function in
`Interfaces.cpp` would have compiled. That was in fact the first implementation.
It required the declaration to be duplicated in two files, which is worse than one
hook declared in the place that already declares the dialect's other hooks.

## Consequences

`ToyDialect.cpp` knows nothing about `StructTypeStorage` or about
`ToyInlinerInterface`, which stays in an anonymous namespace and is therefore
unnameable from outside its file.

The cost is a reader wondering why `initialize()` calls two methods defined
elsewhere. Both declarations in `Ops.td` carry a comment saying what forced it,
since the answer is a compile error rather than a preference.

## Evidence

- Both hooks declared in ODS, `include/toy/Ops.td:71` and `:81`, each with the
  reasoning in a comment above it.
- Called from `initialize()`, `src/dialect/ToyDialect.cpp:54` and `:60`.
- Defined beside what they register: `src/dialect/StructType.cpp:105`,
  `src/dialect/Interfaces.cpp:129`.
- No `src/dialect/TypeDetail.h` exists in the tree.
