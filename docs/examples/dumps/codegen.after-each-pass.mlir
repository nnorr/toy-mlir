// -----// IR Dump After CanonicalizerPass: canonicalize{cse-between-iterations=false    max-iterations=10 max-num-rewrites=-1 region-simplify=normal test-convergence=false top-down=true} //----- //
toy.func private @multiply_transpose(%arg0: tensor<*xf64>, %arg1: tensor<*xf64>) -> tensor<*xf64> {
  %0 = toy.transpose(%arg0 : tensor<*xf64>) to tensor<*xf64>
  %1 = toy.transpose(%arg1 : tensor<*xf64>) to tensor<*xf64>
  %2 = toy.mul %0, %1 : tensor<*xf64>
  toy.return %2 : tensor<*xf64>
}

// -----// IR Dump After CanonicalizerPass: canonicalize{cse-between-iterations=false    max-iterations=10 max-num-rewrites=-1 region-simplify=normal test-convergence=false top-down=true} //----- //
toy.func @main() {
  %0 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
  %1 = toy.generic_call @multiply_transpose(%0, %0) : (tensor<2x3xf64>, tensor<2x3xf64>) -> tensor<*xf64>
  %2 = toy.generic_call @multiply_transpose(%0, %0) : (tensor<2x3xf64>, tensor<2x3xf64>) -> tensor<*xf64>
  toy.print %2 : tensor<*xf64>
  toy.return
}

// -----// IR Dump After CanonicalizerPass: canonicalize{cse-between-iterations=false    max-iterations=10 max-num-rewrites=-1 region-simplify=normal test-convergence=false top-down=true} //----- //
toy.func @main() {
  %0 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
  %1 = toy.cast %0 : tensor<2x3xf64> to tensor<*xf64>
  %2 = toy.cast %0 : tensor<2x3xf64> to tensor<*xf64>
  %3 = toy.transpose(%1 : tensor<*xf64>) to tensor<*xf64>
  %4 = toy.transpose(%2 : tensor<*xf64>) to tensor<*xf64>
  %5 = toy.mul %3, %4 : tensor<*xf64>
  toy.print %5 : tensor<*xf64>
  toy.return
}

// -----// IR Dump After InlinerPass: inline{default-pipeline=canonicalize inlining-threshold=4294967295 max-iterations=4 } //----- //
module {
  toy.func @main() {
    %0 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
    %1 = toy.cast %0 : tensor<2x3xf64> to tensor<*xf64>
    %2 = toy.cast %0 : tensor<2x3xf64> to tensor<*xf64>
    %3 = toy.transpose(%1 : tensor<*xf64>) to tensor<*xf64>
    %4 = toy.transpose(%2 : tensor<*xf64>) to tensor<*xf64>
    %5 = toy.mul %3, %4 : tensor<*xf64>
    toy.print %5 : tensor<*xf64>
    toy.return
  }
}


// -----// IR Dump After CanonicalizerPass: canonicalize{cse-between-iterations=false    max-iterations=10 max-num-rewrites=-1 region-simplify=normal test-convergence=false top-down=true} //----- //
toy.func @main() {
  %0 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
  %1 = toy.cast %0 : tensor<2x3xf64> to tensor<*xf64>
  %2 = toy.cast %0 : tensor<2x3xf64> to tensor<*xf64>
  %3 = toy.transpose(%1 : tensor<*xf64>) to tensor<*xf64>
  %4 = toy.transpose(%2 : tensor<*xf64>) to tensor<*xf64>
  %5 = toy.mul %3, %4 : tensor<*xf64>
  toy.print %5 : tensor<*xf64>
  toy.return
}

// -----// IR Dump After (anonymous namespace)::ShapeInferencePass: toy-shape-inference //----- //
toy.func @main() {
  %0 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
  %1 = toy.cast %0 : tensor<2x3xf64> to tensor<2x3xf64>
  %2 = toy.cast %0 : tensor<2x3xf64> to tensor<2x3xf64>
  %3 = toy.transpose(%1 : tensor<2x3xf64>) to tensor<3x2xf64>
  %4 = toy.transpose(%2 : tensor<2x3xf64>) to tensor<3x2xf64>
  %5 = toy.mul %3, %4 : tensor<3x2xf64>
  toy.print %5 : tensor<3x2xf64>
  toy.return
}

// -----// IR Dump After CanonicalizerPass: canonicalize{cse-between-iterations=false    max-iterations=10 max-num-rewrites=-1 region-simplify=normal test-convergence=false top-down=true} //----- //
toy.func @main() {
  %0 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
  %1 = toy.transpose(%0 : tensor<2x3xf64>) to tensor<3x2xf64>
  %2 = toy.transpose(%0 : tensor<2x3xf64>) to tensor<3x2xf64>
  %3 = toy.mul %1, %2 : tensor<3x2xf64>
  toy.print %3 : tensor<3x2xf64>
  toy.return
}

// -----// IR Dump After CSEPass: cse //----- //
toy.func @main() {
  %0 = toy.constant dense<[[1.000000e+00, 2.000000e+00, 3.000000e+00], [4.000000e+00, 5.000000e+00, 6.000000e+00]]> : tensor<2x3xf64>
  %1 = toy.transpose(%0 : tensor<2x3xf64>) to tensor<3x2xf64>
  %2 = toy.mul %1, %1 : tensor<3x2xf64>
  toy.print %2 : tensor<3x2xf64>
  toy.return
}

// -----// IR Dump After (anonymous namespace)::ToyToAffineLoweringPass: toy-to-affine //----- //
module {
  func.func @main() {
    %alloc = memref.alloc() : memref<3x2xf64>
    %alloc_0 = memref.alloc() : memref<3x2xf64>
    %alloc_1 = memref.alloc() : memref<2x3xf64>
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c2 = arith.constant 2 : index
    %cst = arith.constant 1.000000e+00 : f64
    affine.store %cst, %alloc_1[%c0, %c0] : memref<2x3xf64>
    %cst_2 = arith.constant 2.000000e+00 : f64
    affine.store %cst_2, %alloc_1[%c0, %c1] : memref<2x3xf64>
    %cst_3 = arith.constant 3.000000e+00 : f64
    affine.store %cst_3, %alloc_1[%c0, %c2] : memref<2x3xf64>
    %cst_4 = arith.constant 4.000000e+00 : f64
    affine.store %cst_4, %alloc_1[%c1, %c0] : memref<2x3xf64>
    %cst_5 = arith.constant 5.000000e+00 : f64
    affine.store %cst_5, %alloc_1[%c1, %c1] : memref<2x3xf64>
    %cst_6 = arith.constant 6.000000e+00 : f64
    affine.store %cst_6, %alloc_1[%c1, %c2] : memref<2x3xf64>
    affine.for %arg0 = 0 to 3 {
      affine.for %arg1 = 0 to 2 {
        %0 = affine.load %alloc_1[%arg1, %arg0] : memref<2x3xf64>
        affine.store %0, %alloc_0[%arg0, %arg1] : memref<3x2xf64>
      }
    }
    affine.for %arg0 = 0 to 3 {
      affine.for %arg1 = 0 to 2 {
        %0 = affine.load %alloc_0[%arg0, %arg1] : memref<3x2xf64>
        %1 = affine.load %alloc_0[%arg0, %arg1] : memref<3x2xf64>
        %2 = arith.mulf %0, %1 : f64
        affine.store %2, %alloc[%arg0, %arg1] : memref<3x2xf64>
      }
    }
    toy.print %alloc : memref<3x2xf64>
    memref.dealloc %alloc_1 : memref<2x3xf64>
    memref.dealloc %alloc_0 : memref<3x2xf64>
    memref.dealloc %alloc : memref<3x2xf64>
    return
  }
}


// -----// IR Dump After CanonicalizerPass: canonicalize{cse-between-iterations=false    max-iterations=10 max-num-rewrites=-1 region-simplify=normal test-convergence=false top-down=true} //----- //
func.func @main() {
  %cst = arith.constant 6.000000e+00 : f64
  %cst_0 = arith.constant 5.000000e+00 : f64
  %cst_1 = arith.constant 4.000000e+00 : f64
  %cst_2 = arith.constant 3.000000e+00 : f64
  %cst_3 = arith.constant 2.000000e+00 : f64
  %cst_4 = arith.constant 1.000000e+00 : f64
  %alloc = memref.alloc() : memref<3x2xf64>
  %alloc_5 = memref.alloc() : memref<3x2xf64>
  %alloc_6 = memref.alloc() : memref<2x3xf64>
  affine.store %cst_4, %alloc_6[0, 0] : memref<2x3xf64>
  affine.store %cst_3, %alloc_6[0, 1] : memref<2x3xf64>
  affine.store %cst_2, %alloc_6[0, 2] : memref<2x3xf64>
  affine.store %cst_1, %alloc_6[1, 0] : memref<2x3xf64>
  affine.store %cst_0, %alloc_6[1, 1] : memref<2x3xf64>
  affine.store %cst, %alloc_6[1, 2] : memref<2x3xf64>
  affine.for %arg0 = 0 to 3 {
    affine.for %arg1 = 0 to 2 {
      %0 = affine.load %alloc_6[%arg1, %arg0] : memref<2x3xf64>
      affine.store %0, %alloc_5[%arg0, %arg1] : memref<3x2xf64>
    }
  }
  affine.for %arg0 = 0 to 3 {
    affine.for %arg1 = 0 to 2 {
      %0 = affine.load %alloc_5[%arg0, %arg1] : memref<3x2xf64>
      %1 = affine.load %alloc_5[%arg0, %arg1] : memref<3x2xf64>
      %2 = arith.mulf %0, %1 : f64
      affine.store %2, %alloc[%arg0, %arg1] : memref<3x2xf64>
    }
  }
  toy.print %alloc : memref<3x2xf64>
  memref.dealloc %alloc_6 : memref<2x3xf64>
  memref.dealloc %alloc_5 : memref<3x2xf64>
  memref.dealloc %alloc : memref<3x2xf64>
  return
}

// -----// IR Dump After CSEPass: cse //----- //
func.func @main() {
  %cst = arith.constant 6.000000e+00 : f64
  %cst_0 = arith.constant 5.000000e+00 : f64
  %cst_1 = arith.constant 4.000000e+00 : f64
  %cst_2 = arith.constant 3.000000e+00 : f64
  %cst_3 = arith.constant 2.000000e+00 : f64
  %cst_4 = arith.constant 1.000000e+00 : f64
  %alloc = memref.alloc() : memref<3x2xf64>
  %alloc_5 = memref.alloc() : memref<3x2xf64>
  %alloc_6 = memref.alloc() : memref<2x3xf64>
  affine.store %cst_4, %alloc_6[0, 0] : memref<2x3xf64>
  affine.store %cst_3, %alloc_6[0, 1] : memref<2x3xf64>
  affine.store %cst_2, %alloc_6[0, 2] : memref<2x3xf64>
  affine.store %cst_1, %alloc_6[1, 0] : memref<2x3xf64>
  affine.store %cst_0, %alloc_6[1, 1] : memref<2x3xf64>
  affine.store %cst, %alloc_6[1, 2] : memref<2x3xf64>
  affine.for %arg0 = 0 to 3 {
    affine.for %arg1 = 0 to 2 {
      %0 = affine.load %alloc_6[%arg1, %arg0] : memref<2x3xf64>
      affine.store %0, %alloc_5[%arg0, %arg1] : memref<3x2xf64>
    }
  }
  affine.for %arg0 = 0 to 3 {
    affine.for %arg1 = 0 to 2 {
      %0 = affine.load %alloc_5[%arg0, %arg1] : memref<3x2xf64>
      %1 = arith.mulf %0, %0 : f64
      affine.store %1, %alloc[%arg0, %arg1] : memref<3x2xf64>
    }
  }
  toy.print %alloc : memref<3x2xf64>
  memref.dealloc %alloc_6 : memref<2x3xf64>
  memref.dealloc %alloc_5 : memref<3x2xf64>
  memref.dealloc %alloc : memref<3x2xf64>
  return
}

// -----// IR Dump After AffineLoopFusion: affine-loop-fusion{compute-tolerance=3.000000e-01 fast-mem-space=0 local-buf-threshold=0 maximal=false mode=greedy} //----- //
func.func @main() {
  %cst = arith.constant 6.000000e+00 : f64
  %cst_0 = arith.constant 5.000000e+00 : f64
  %cst_1 = arith.constant 4.000000e+00 : f64
  %cst_2 = arith.constant 3.000000e+00 : f64
  %cst_3 = arith.constant 2.000000e+00 : f64
  %cst_4 = arith.constant 1.000000e+00 : f64
  %alloc = memref.alloc() : memref<3x2xf64>
  %alloc_5 = memref.alloc() : memref<3x2xf64>
  %alloc_6 = memref.alloc() : memref<2x3xf64>
  affine.store %cst_4, %alloc_6[0, 0] : memref<2x3xf64>
  affine.store %cst_3, %alloc_6[0, 1] : memref<2x3xf64>
  affine.store %cst_2, %alloc_6[0, 2] : memref<2x3xf64>
  affine.store %cst_1, %alloc_6[1, 0] : memref<2x3xf64>
  affine.store %cst_0, %alloc_6[1, 1] : memref<2x3xf64>
  affine.store %cst, %alloc_6[1, 2] : memref<2x3xf64>
  affine.for %arg0 = 0 to 3 {
    affine.for %arg1 = 0 to 2 {
      %0 = affine.load %alloc_6[%arg1, %arg0] : memref<2x3xf64>
      affine.store %0, %alloc_5[%arg0, %arg1] : memref<3x2xf64>
      %1 = affine.load %alloc_5[%arg0, %arg1] : memref<3x2xf64>
      %2 = arith.mulf %1, %1 : f64
      affine.store %2, %alloc[%arg0, %arg1] : memref<3x2xf64>
    }
  }
  toy.print %alloc : memref<3x2xf64>
  memref.dealloc %alloc_6 : memref<2x3xf64>
  memref.dealloc %alloc_5 : memref<3x2xf64>
  memref.dealloc %alloc : memref<3x2xf64>
  return
}

// -----// IR Dump After AffineScalarReplacement: affine-scalrep //----- //
func.func @main() {
  %cst = arith.constant 6.000000e+00 : f64
  %cst_0 = arith.constant 5.000000e+00 : f64
  %cst_1 = arith.constant 4.000000e+00 : f64
  %cst_2 = arith.constant 3.000000e+00 : f64
  %cst_3 = arith.constant 2.000000e+00 : f64
  %cst_4 = arith.constant 1.000000e+00 : f64
  %alloc = memref.alloc() : memref<3x2xf64>
  %alloc_5 = memref.alloc() : memref<2x3xf64>
  affine.store %cst_4, %alloc_5[0, 0] : memref<2x3xf64>
  affine.store %cst_3, %alloc_5[0, 1] : memref<2x3xf64>
  affine.store %cst_2, %alloc_5[0, 2] : memref<2x3xf64>
  affine.store %cst_1, %alloc_5[1, 0] : memref<2x3xf64>
  affine.store %cst_0, %alloc_5[1, 1] : memref<2x3xf64>
  affine.store %cst, %alloc_5[1, 2] : memref<2x3xf64>
  affine.for %arg0 = 0 to 3 {
    affine.for %arg1 = 0 to 2 {
      %0 = affine.load %alloc_5[%arg1, %arg0] : memref<2x3xf64>
      %1 = arith.mulf %0, %0 : f64
      affine.store %1, %alloc[%arg0, %arg1] : memref<3x2xf64>
    }
  }
  toy.print %alloc : memref<3x2xf64>
  memref.dealloc %alloc_5 : memref<2x3xf64>
  memref.dealloc %alloc : memref<3x2xf64>
  return
}

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
    affine.store %cst_4, %alloc_5[0, 0] : memref<2x3xf64>
    affine.store %cst_3, %alloc_5[0, 1] : memref<2x3xf64>
    affine.store %cst_2, %alloc_5[0, 2] : memref<2x3xf64>
    affine.store %cst_1, %alloc_5[1, 0] : memref<2x3xf64>
    affine.store %cst_0, %alloc_5[1, 1] : memref<2x3xf64>
    affine.store %cst, %alloc_5[1, 2] : memref<2x3xf64>
    affine.for %arg0 = 0 to 3 {
      affine.for %arg1 = 0 to 2 {
        %0 = affine.load %alloc_5[%arg1, %arg0] : memref<2x3xf64>
        %1 = arith.mulf %0, %0 : f64
        affine.store %1, %alloc[%arg0, %arg1] : memref<3x2xf64>
      }
    }
    toy.print %alloc : memref<3x2xf64>
    memref.dealloc %alloc_5 : memref<2x3xf64>
    memref.dealloc %alloc : memref<3x2xf64>
    return
  }
}
