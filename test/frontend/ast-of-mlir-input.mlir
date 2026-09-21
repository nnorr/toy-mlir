// Asking for an AST dump of MLIR input is refused, with upstream's exit code 5.
//
// Note it takes `-x mlir` to reach this path: the extension alone is not
// enough, because -emit=ast runs before the input kind is inferred. Passing a
// .mlir file without -x mlir instead produces a Toy parse error on the first
// '//', which is its own kind of confusing and is upstream's behavior too.
//
// RUN: not %toyc %s -x mlir -emit=ast 2>&1 | %filecheck %s

toy.func @main() {
  toy.return
}

// CHECK: Can't dump a Toy AST when the input is MLIR
