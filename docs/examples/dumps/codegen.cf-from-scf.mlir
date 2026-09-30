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
    cf.br ^bb1(%c0_15 : index)
  ^bb1(%0: index):  // 2 preds: ^bb0, ^bb5
    %1 = arith.cmpi slt, %0, %c3 : index
    cf.cond_br %1, ^bb2, ^bb6
  ^bb2:  // pred: ^bb1
    %c0_17 = arith.constant 0 : index
    %c2_18 = arith.constant 2 : index
    %c1_19 = arith.constant 1 : index
    cf.br ^bb3(%c0_17 : index)
  ^bb3(%2: index):  // 2 preds: ^bb2, ^bb4
    %3 = arith.cmpi slt, %2, %c2_18 : index
    cf.cond_br %3, ^bb4, ^bb5
  ^bb4:  // pred: ^bb3
    %4 = memref.load %alloc_5[%2, %0] : memref<2x3xf64>
    %5 = arith.mulf %4, %4 : f64
    memref.store %5, %alloc[%0, %2] : memref<3x2xf64>
    %6 = arith.addi %2, %c1_19 : index
    cf.br ^bb3(%6 : index)
  ^bb5:  // pred: ^bb3
    %7 = arith.addi %0, %c1_16 : index
    cf.br ^bb1(%7 : index)
  ^bb6:  // pred: ^bb1
    "toy.print"(%alloc) : (memref<3x2xf64>) -> ()
    memref.dealloc %alloc_5 : memref<2x3xf64>
    memref.dealloc %alloc : memref<3x2xf64>
    return
  }
}

