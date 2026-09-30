module {
  llvm.func @free(!llvm.ptr)
  llvm.mlir.global internal constant @nl("\0A\00") {addr_space = 0 : i32}
  llvm.mlir.global internal constant @frmt_spec("%f \00") {addr_space = 0 : i32}
  llvm.func @printf(!llvm.ptr, ...) -> i32
  llvm.func @malloc(i64) -> !llvm.ptr
  llvm.func @main() {
    %0 = llvm.mlir.constant(2.000000e+00 : f64) : f64
    %1 = llvm.mlir.constant(1.000000e+00 : f64) : f64
    %2 = llvm.mlir.constant(2 : index) : i64
    %3 = llvm.mlir.constant(1 : index) : i64
    %4 = llvm.mlir.constant(1 : index) : i64
    %5 = llvm.mlir.zero : !llvm.ptr
    %6 = llvm.getelementptr %5[%2] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    %7 = llvm.ptrtoint %6 : !llvm.ptr to i64
    %8 = llvm.call @malloc(%7) : (i64) -> !llvm.ptr
    %9 = llvm.mlir.poison : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)>
    %10 = llvm.insertvalue %8, %9[0] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %11 = llvm.insertvalue %8, %10[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %12 = llvm.mlir.constant(0 : index) : i64
    %13 = llvm.insertvalue %12, %11[2] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %14 = llvm.insertvalue %2, %13[3, 0] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %15 = llvm.insertvalue %3, %14[3, 1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %16 = llvm.insertvalue %3, %15[4, 0] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %17 = llvm.insertvalue %4, %16[4, 1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %18 = llvm.mlir.constant(0 : index) : i64
    %19 = llvm.mlir.constant(0 : index) : i64
    %20 = llvm.extractvalue %17[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %21 = llvm.add %18, %19 overflow<nsw, nuw> : i64
    %22 = llvm.getelementptr inbounds|nuw %20[%21] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.store %1, %22 : f64, !llvm.ptr
    %23 = llvm.mlir.constant(1 : index) : i64
    %24 = llvm.mlir.constant(0 : index) : i64
    %25 = llvm.extractvalue %17[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %26 = llvm.add %23, %24 overflow<nsw, nuw> : i64
    %27 = llvm.getelementptr inbounds|nuw %25[%26] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.store %0, %27 : f64, !llvm.ptr
    %28 = llvm.mlir.addressof @frmt_spec : !llvm.ptr
    %29 = llvm.mlir.constant(0 : index) : i64
    %30 = llvm.getelementptr %28[%29, %29] : (!llvm.ptr, i64, i64) -> !llvm.ptr, !llvm.array<4 x i8>
    %31 = llvm.mlir.addressof @nl : !llvm.ptr
    %32 = llvm.mlir.constant(0 : index) : i64
    %33 = llvm.getelementptr %31[%32, %32] : (!llvm.ptr, i64, i64) -> !llvm.ptr, !llvm.array<2 x i8>
    %34 = llvm.mlir.constant(0 : index) : i64
    %35 = llvm.mlir.constant(2 : index) : i64
    %36 = llvm.mlir.constant(1 : index) : i64
    llvm.br ^bb1(%34 : i64)
  ^bb1(%37: i64):  // 2 preds: ^bb0, ^bb5
    %38 = llvm.icmp "slt" %37, %35 : i64
    llvm.cond_br %38, ^bb2, ^bb6
  ^bb2:  // pred: ^bb1
    %39 = llvm.mlir.constant(0 : index) : i64
    %40 = llvm.mlir.constant(1 : index) : i64
    %41 = llvm.mlir.constant(1 : index) : i64
    llvm.br ^bb3(%39 : i64)
  ^bb3(%42: i64):  // 2 preds: ^bb2, ^bb4
    %43 = llvm.icmp "slt" %42, %40 : i64
    llvm.cond_br %43, ^bb4, ^bb5
  ^bb4:  // pred: ^bb3
    %44 = llvm.extractvalue %17[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %45 = llvm.add %37, %42 overflow<nsw, nuw> : i64
    %46 = llvm.getelementptr inbounds|nuw %44[%45] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    %47 = llvm.load %46 : !llvm.ptr -> f64
    %48 = llvm.call @printf(%30, %47) vararg(!llvm.func<i32 (ptr, ...)>) : (!llvm.ptr, f64) -> i32
    %49 = llvm.add %42, %41 : i64
    llvm.br ^bb3(%49 : i64)
  ^bb5:  // pred: ^bb3
    %50 = llvm.call @printf(%33) vararg(!llvm.func<i32 (ptr, ...)>) : (!llvm.ptr) -> i32
    %51 = llvm.add %37, %36 : i64
    llvm.br ^bb1(%51 : i64)
  ^bb6:  // pred: ^bb1
    %52 = llvm.extractvalue %17[0] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    llvm.call @free(%52) : (!llvm.ptr) -> ()
    llvm.return
  }
}
