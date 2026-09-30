module {
  func.func @main() {
    %cst = arith.constant 2.000000e+00 : f64
    %cst_0 = arith.constant 1.000000e+00 : f64
    %alloc = memref.alloc() : memref<2x1xf64>
    affine.store %cst_0, %alloc[0, 0] : memref<2x1xf64>
    affine.store %cst, %alloc[1, 0] : memref<2x1xf64>
    toy.print %alloc : memref<2x1xf64>
    memref.dealloc %alloc : memref<2x1xf64>
    return
  }
}
