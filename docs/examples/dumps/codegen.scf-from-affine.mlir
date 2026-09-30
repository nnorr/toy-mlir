module {
  func.func @main() {
    %cst = arith.constant 6.000000e+00 : f64
    %cst_0 = arith.constant 5.000000e+00 : f64
    %cst_1 = arith.constant 4.000000e+00 : f64
    %cst_2 = arith.constant 3.000000e+00 : f64
    %cst_3 = arith.constant 2.000000e+00 : f64
    %cst_4 = arith.constant 1.000000e+00 : f64
    %alloc = memref.alloc() : memref<3x2xf64>
    %alloc_5 = memref.alloc() : memref<2x3xf64>
    %c0 = arith.constant 0 : index
    %c0_6 = arith.constant 0 : index
    memref.store %cst_4, %alloc_5[%c0, %c0_6] : memref<2x3xf64>
    %c0_7 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    memref.store %cst_3, %alloc_5[%c0_7, %c1] : memref<2x3xf64>
    %c0_8 = arith.constant 0 : index
    %c2 = arith.constant 2 : index
    memref.store %cst_2, %alloc_5[%c0_8, %c2] : memref<2x3xf64>
    %c1_9 = arith.constant 1 : index
    %c0_10 = arith.constant 0 : index
    memref.store %cst_1, %alloc_5[%c1_9, %c0_10] : memref<2x3xf64>
    %c1_11 = arith.constant 1 : index
    %c1_12 = arith.constant 1 : index
    memref.store %cst_0, %alloc_5[%c1_11, %c1_12] : memref<2x3xf64>
    %c1_13 = arith.constant 1 : index
    %c2_14 = arith.constant 2 : index
    memref.store %cst, %alloc_5[%c1_13, %c2_14] : memref<2x3xf64>
    %c0_15 = arith.constant 0 : index
    %c3 = arith.constant 3 : index
    %c1_16 = arith.constant 1 : index
    scf.for %arg0 = %c0_15 to %c3 step %c1_16 {
      %c0_17 = arith.constant 0 : index
      %c2_18 = arith.constant 2 : index
      %c1_19 = arith.constant 1 : index
      scf.for %arg1 = %c0_17 to %c2_18 step %c1_19 {
        %0 = memref.load %alloc_5[%arg1, %arg0] : memref<2x3xf64>
        %1 = arith.mulf %0, %0 : f64
        memref.store %1, %alloc[%arg0, %arg1] : memref<3x2xf64>
      }
    }
    "toy.print"(%alloc) : (memref<3x2xf64>) -> ()
    memref.dealloc %alloc_5 : memref<2x3xf64>
    memref.dealloc %alloc : memref<3x2xf64>
    return
  }
}

