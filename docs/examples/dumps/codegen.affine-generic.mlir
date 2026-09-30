"builtin.module"() ({
  "func.func"() <{function_type = () -> (), sym_name = "main"}> ({
    %0 = "arith.constant"() <{value = 6.000000e+00 : f64}> : () -> f64
    %1 = "arith.constant"() <{value = 5.000000e+00 : f64}> : () -> f64
    %2 = "arith.constant"() <{value = 4.000000e+00 : f64}> : () -> f64
    %3 = "arith.constant"() <{value = 3.000000e+00 : f64}> : () -> f64
    %4 = "arith.constant"() <{value = 2.000000e+00 : f64}> : () -> f64
    %5 = "arith.constant"() <{value = 1.000000e+00 : f64}> : () -> f64
    %6 = "memref.alloc"() <{operandSegmentSizes = array<i32: 0, 0>}> : () -> memref<3x2xf64>
    %7 = "memref.alloc"() <{operandSegmentSizes = array<i32: 0, 0>}> : () -> memref<2x3xf64>
    "affine.store"(%5, %7) <{map = affine_map<() -> (0, 0)>}> : (f64, memref<2x3xf64>) -> ()
    "affine.store"(%4, %7) <{map = affine_map<() -> (0, 1)>}> : (f64, memref<2x3xf64>) -> ()
    "affine.store"(%3, %7) <{map = affine_map<() -> (0, 2)>}> : (f64, memref<2x3xf64>) -> ()
    "affine.store"(%2, %7) <{map = affine_map<() -> (1, 0)>}> : (f64, memref<2x3xf64>) -> ()
    "affine.store"(%1, %7) <{map = affine_map<() -> (1, 1)>}> : (f64, memref<2x3xf64>) -> ()
    "affine.store"(%0, %7) <{map = affine_map<() -> (1, 2)>}> : (f64, memref<2x3xf64>) -> ()
    "affine.for"() <{lowerBoundMap = affine_map<() -> (0)>, operandSegmentSizes = array<i32: 0, 0, 0>, step = 1 : index, upperBoundMap = affine_map<() -> (3)>}> ({
    ^bb0(%arg0: index):
      "affine.for"() <{lowerBoundMap = affine_map<() -> (0)>, operandSegmentSizes = array<i32: 0, 0, 0>, step = 1 : index, upperBoundMap = affine_map<() -> (2)>}> ({
      ^bb0(%arg1: index):
        %8 = "affine.load"(%7, %arg1, %arg0) <{map = affine_map<(d0, d1) -> (d0, d1)>}> : (memref<2x3xf64>, index, index) -> f64
        %9 = "arith.mulf"(%8, %8) <{fastmath = #arith.fastmath<none>}> : (f64, f64) -> f64
        "affine.store"(%9, %6, %arg0, %arg1) <{map = affine_map<(d0, d1) -> (d0, d1)>}> : (f64, memref<3x2xf64>, index, index) -> ()
        "affine.yield"() : () -> ()
      }) : () -> ()
      "affine.yield"() : () -> ()
    }) : () -> ()
    "toy.print"(%6) : (memref<3x2xf64>) -> ()
    "memref.dealloc"(%7) : (memref<2x3xf64>) -> ()
    "memref.dealloc"(%6) : (memref<3x2xf64>) -> ()
    "func.return"() : () -> ()
  }) : () -> ()
}) : () -> ()
