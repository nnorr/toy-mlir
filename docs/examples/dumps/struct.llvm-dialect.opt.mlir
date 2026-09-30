module {
  llvm.func @free(!llvm.ptr)
  llvm.mlir.global internal constant @nl("\0A\00") {addr_space = 0 : i32}
  llvm.mlir.global internal constant @frmt_spec("%f \00") {addr_space = 0 : i32}
  llvm.func @printf(!llvm.ptr, ...) -> i32
  llvm.func @malloc(i64) -> !llvm.ptr
  llvm.func @main() {
    %0 = llvm.mlir.constant(6.000000e+00 : f64) : f64
    %1 = llvm.mlir.constant(5.000000e+00 : f64) : f64
    %2 = llvm.mlir.constant(4.000000e+00 : f64) : f64
    %3 = llvm.mlir.constant(3.000000e+00 : f64) : f64
    %4 = llvm.mlir.constant(2.000000e+00 : f64) : f64
    %5 = llvm.mlir.constant(1.000000e+00 : f64) : f64
    %6 = llvm.mlir.constant(3 : index) : i64
    %7 = llvm.mlir.constant(2 : index) : i64
    %8 = llvm.mlir.constant(1 : index) : i64
    %9 = llvm.mlir.constant(6 : index) : i64
    %10 = llvm.mlir.zero : !llvm.ptr
    %11 = llvm.getelementptr %10[%9] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    %12 = llvm.ptrtoint %11 : !llvm.ptr to i64
    %13 = llvm.call @malloc(%12) : (i64) -> !llvm.ptr
    %14 = llvm.mlir.poison : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)>
    %15 = llvm.insertvalue %13, %14[0] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %16 = llvm.insertvalue %13, %15[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %17 = llvm.mlir.constant(0 : index) : i64
    %18 = llvm.insertvalue %17, %16[2] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %19 = llvm.insertvalue %6, %18[3, 0] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %20 = llvm.insertvalue %7, %19[3, 1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %21 = llvm.insertvalue %7, %20[4, 0] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %22 = llvm.insertvalue %8, %21[4, 1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %23 = llvm.mlir.constant(2 : index) : i64
    %24 = llvm.mlir.constant(3 : index) : i64
    %25 = llvm.mlir.constant(1 : index) : i64
    %26 = llvm.mlir.constant(6 : index) : i64
    %27 = llvm.mlir.zero : !llvm.ptr
    %28 = llvm.getelementptr %27[%26] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    %29 = llvm.ptrtoint %28 : !llvm.ptr to i64
    %30 = llvm.call @malloc(%29) : (i64) -> !llvm.ptr
    %31 = llvm.mlir.poison : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)>
    %32 = llvm.insertvalue %30, %31[0] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %33 = llvm.insertvalue %30, %32[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %34 = llvm.mlir.constant(0 : index) : i64
    %35 = llvm.insertvalue %34, %33[2] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %36 = llvm.insertvalue %23, %35[3, 0] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %37 = llvm.insertvalue %24, %36[3, 1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %38 = llvm.insertvalue %24, %37[4, 0] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %39 = llvm.insertvalue %25, %38[4, 1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %40 = llvm.mlir.constant(0 : index) : i64
    %41 = llvm.mlir.constant(0 : index) : i64
    %42 = llvm.extractvalue %39[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %43 = llvm.mlir.constant(3 : index) : i64
    %44 = llvm.mul %40, %43 overflow<nsw, nuw> : i64
    %45 = llvm.add %44, %41 overflow<nsw, nuw> : i64
    %46 = llvm.getelementptr inbounds|nuw %42[%45] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.store %5, %46 : f64, !llvm.ptr
    %47 = llvm.mlir.constant(0 : index) : i64
    %48 = llvm.mlir.constant(1 : index) : i64
    %49 = llvm.extractvalue %39[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %50 = llvm.mlir.constant(3 : index) : i64
    %51 = llvm.mul %47, %50 overflow<nsw, nuw> : i64
    %52 = llvm.add %51, %48 overflow<nsw, nuw> : i64
    %53 = llvm.getelementptr inbounds|nuw %49[%52] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.store %4, %53 : f64, !llvm.ptr
    %54 = llvm.mlir.constant(0 : index) : i64
    %55 = llvm.mlir.constant(2 : index) : i64
    %56 = llvm.extractvalue %39[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %57 = llvm.mlir.constant(3 : index) : i64
    %58 = llvm.mul %54, %57 overflow<nsw, nuw> : i64
    %59 = llvm.add %58, %55 overflow<nsw, nuw> : i64
    %60 = llvm.getelementptr inbounds|nuw %56[%59] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.store %3, %60 : f64, !llvm.ptr
    %61 = llvm.mlir.constant(1 : index) : i64
    %62 = llvm.mlir.constant(0 : index) : i64
    %63 = llvm.extractvalue %39[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %64 = llvm.mlir.constant(3 : index) : i64
    %65 = llvm.mul %61, %64 overflow<nsw, nuw> : i64
    %66 = llvm.add %65, %62 overflow<nsw, nuw> : i64
    %67 = llvm.getelementptr inbounds|nuw %63[%66] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.store %2, %67 : f64, !llvm.ptr
    %68 = llvm.mlir.constant(1 : index) : i64
    %69 = llvm.mlir.constant(1 : index) : i64
    %70 = llvm.extractvalue %39[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %71 = llvm.mlir.constant(3 : index) : i64
    %72 = llvm.mul %68, %71 overflow<nsw, nuw> : i64
    %73 = llvm.add %72, %69 overflow<nsw, nuw> : i64
    %74 = llvm.getelementptr inbounds|nuw %70[%73] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.store %1, %74 : f64, !llvm.ptr
    %75 = llvm.mlir.constant(1 : index) : i64
    %76 = llvm.mlir.constant(2 : index) : i64
    %77 = llvm.extractvalue %39[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %78 = llvm.mlir.constant(3 : index) : i64
    %79 = llvm.mul %75, %78 overflow<nsw, nuw> : i64
    %80 = llvm.add %79, %76 overflow<nsw, nuw> : i64
    %81 = llvm.getelementptr inbounds|nuw %77[%80] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.store %0, %81 : f64, !llvm.ptr
    %82 = llvm.mlir.constant(0 : index) : i64
    %83 = llvm.mlir.constant(3 : index) : i64
    %84 = llvm.mlir.constant(1 : index) : i64
    llvm.br ^bb1(%82 : i64)
  ^bb1(%85: i64):  // 2 preds: ^bb0, ^bb5
    %86 = llvm.icmp "slt" %85, %83 : i64
    llvm.cond_br %86, ^bb2, ^bb6
  ^bb2:  // pred: ^bb1
    %87 = llvm.mlir.constant(0 : index) : i64
    %88 = llvm.mlir.constant(2 : index) : i64
    %89 = llvm.mlir.constant(1 : index) : i64
    llvm.br ^bb3(%87 : i64)
  ^bb3(%90: i64):  // 2 preds: ^bb2, ^bb4
    %91 = llvm.icmp "slt" %90, %88 : i64
    llvm.cond_br %91, ^bb4, ^bb5
  ^bb4:  // pred: ^bb3
    %92 = llvm.extractvalue %39[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %93 = llvm.mlir.constant(3 : index) : i64
    %94 = llvm.mul %90, %93 overflow<nsw, nuw> : i64
    %95 = llvm.add %94, %85 overflow<nsw, nuw> : i64
    %96 = llvm.getelementptr inbounds|nuw %92[%95] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    %97 = llvm.load %96 : !llvm.ptr -> f64
    %98 = llvm.fmul %97, %97 : f64
    %99 = llvm.extractvalue %22[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %100 = llvm.mlir.constant(2 : index) : i64
    %101 = llvm.mul %85, %100 overflow<nsw, nuw> : i64
    %102 = llvm.add %101, %90 overflow<nsw, nuw> : i64
    %103 = llvm.getelementptr inbounds|nuw %99[%102] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.store %98, %103 : f64, !llvm.ptr
    %104 = llvm.add %90, %89 : i64
    llvm.br ^bb3(%104 : i64)
  ^bb5:  // pred: ^bb3
    %105 = llvm.add %85, %84 : i64
    llvm.br ^bb1(%105 : i64)
  ^bb6:  // pred: ^bb1
    %106 = llvm.mlir.addressof @frmt_spec : !llvm.ptr
    %107 = llvm.mlir.constant(0 : index) : i64
    %108 = llvm.getelementptr %106[%107, %107] : (!llvm.ptr, i64, i64) -> !llvm.ptr, !llvm.array<4 x i8>
    %109 = llvm.mlir.addressof @nl : !llvm.ptr
    %110 = llvm.mlir.constant(0 : index) : i64
    %111 = llvm.getelementptr %109[%110, %110] : (!llvm.ptr, i64, i64) -> !llvm.ptr, !llvm.array<2 x i8>
    %112 = llvm.mlir.constant(0 : index) : i64
    %113 = llvm.mlir.constant(3 : index) : i64
    %114 = llvm.mlir.constant(1 : index) : i64
    llvm.br ^bb7(%112 : i64)
  ^bb7(%115: i64):  // 2 preds: ^bb6, ^bb11
    %116 = llvm.icmp "slt" %115, %113 : i64
    llvm.cond_br %116, ^bb8, ^bb12
  ^bb8:  // pred: ^bb7
    %117 = llvm.mlir.constant(0 : index) : i64
    %118 = llvm.mlir.constant(2 : index) : i64
    %119 = llvm.mlir.constant(1 : index) : i64
    llvm.br ^bb9(%117 : i64)
  ^bb9(%120: i64):  // 2 preds: ^bb8, ^bb10
    %121 = llvm.icmp "slt" %120, %118 : i64
    llvm.cond_br %121, ^bb10, ^bb11
  ^bb10:  // pred: ^bb9
    %122 = llvm.extractvalue %22[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %123 = llvm.mlir.constant(2 : index) : i64
    %124 = llvm.mul %115, %123 overflow<nsw, nuw> : i64
    %125 = llvm.add %124, %120 overflow<nsw, nuw> : i64
    %126 = llvm.getelementptr inbounds|nuw %122[%125] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    %127 = llvm.load %126 : !llvm.ptr -> f64
    %128 = llvm.call @printf(%108, %127) vararg(!llvm.func<i32 (ptr, ...)>) : (!llvm.ptr, f64) -> i32
    %129 = llvm.add %120, %119 : i64
    llvm.br ^bb9(%129 : i64)
  ^bb11:  // pred: ^bb9
    %130 = llvm.call @printf(%111) vararg(!llvm.func<i32 (ptr, ...)>) : (!llvm.ptr) -> i32
    %131 = llvm.add %115, %114 : i64
    llvm.br ^bb7(%131 : i64)
  ^bb12:  // pred: ^bb7
    %132 = llvm.extractvalue %39[0] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    llvm.call @free(%132) : (!llvm.ptr) -> ()
    %133 = llvm.extractvalue %22[0] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    llvm.call @free(%133) : (!llvm.ptr) -> ()
    llvm.return
  }
}
