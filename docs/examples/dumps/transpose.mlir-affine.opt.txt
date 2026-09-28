module {
  func.func @main() {
    %cst = arith.constant 6.000000e+00 : f64
    %cst_0 = arith.constant 5.000000e+00 : f64
    %cst_1 = arith.constant 4.000000e+00 : f64
    %cst_2 = arith.constant 3.000000e+00 : f64
    %cst_3 = arith.constant 2.000000e+00 : f64
    %cst_4 = arith.constant 1.000000e+00 : f64
    %alloc = memref.alloc() : memref<2x3xf64>
    affine.store %cst_4, %alloc[0, 0] : memref<2x3xf64>
    affine.store %cst_3, %alloc[0, 1] : memref<2x3xf64>
    affine.store %cst_2, %alloc[0, 2] : memref<2x3xf64>
    affine.store %cst_1, %alloc[1, 0] : memref<2x3xf64>
    affine.store %cst_0, %alloc[1, 1] : memref<2x3xf64>
    affine.store %cst, %alloc[1, 2] : memref<2x3xf64>
    toy.print %alloc : memref<2x3xf64>
    memref.dealloc %alloc : memref<2x3xf64>
    return
  }
}
