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
    %23 = llvm.mlir.constant(3 : index) : i64
    %24 = llvm.mlir.constant(2 : index) : i64
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
    %40 = llvm.mlir.constant(2 : index) : i64
    %41 = llvm.mlir.constant(3 : index) : i64
    %42 = llvm.mlir.constant(1 : index) : i64
    %43 = llvm.mlir.constant(6 : index) : i64
    %44 = llvm.mlir.zero : !llvm.ptr
    %45 = llvm.getelementptr %44[%43] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    %46 = llvm.ptrtoint %45 : !llvm.ptr to i64
    %47 = llvm.call @malloc(%46) : (i64) -> !llvm.ptr
    %48 = llvm.mlir.poison : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)>
    %49 = llvm.insertvalue %47, %48[0] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %50 = llvm.insertvalue %47, %49[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %51 = llvm.mlir.constant(0 : index) : i64
    %52 = llvm.insertvalue %51, %50[2] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %53 = llvm.insertvalue %40, %52[3, 0] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %54 = llvm.insertvalue %41, %53[3, 1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %55 = llvm.insertvalue %41, %54[4, 0] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %56 = llvm.insertvalue %42, %55[4, 1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %57 = llvm.mlir.constant(0 : index) : i64
    %58 = llvm.mlir.constant(0 : index) : i64
    %59 = llvm.extractvalue %56[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %60 = llvm.mlir.constant(3 : index) : i64
    %61 = llvm.mul %57, %60 overflow<nsw, nuw> : i64
    %62 = llvm.add %61, %58 overflow<nsw, nuw> : i64
    %63 = llvm.getelementptr inbounds|nuw %59[%62] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.store %5, %63 : f64, !llvm.ptr
    %64 = llvm.mlir.constant(0 : index) : i64
    %65 = llvm.mlir.constant(1 : index) : i64
    %66 = llvm.extractvalue %56[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %67 = llvm.mlir.constant(3 : index) : i64
    %68 = llvm.mul %64, %67 overflow<nsw, nuw> : i64
    %69 = llvm.add %68, %65 overflow<nsw, nuw> : i64
    %70 = llvm.getelementptr inbounds|nuw %66[%69] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.store %4, %70 : f64, !llvm.ptr
    %71 = llvm.mlir.constant(0 : index) : i64
    %72 = llvm.mlir.constant(2 : index) : i64
    %73 = llvm.extractvalue %56[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %74 = llvm.mlir.constant(3 : index) : i64
    %75 = llvm.mul %71, %74 overflow<nsw, nuw> : i64
    %76 = llvm.add %75, %72 overflow<nsw, nuw> : i64
    %77 = llvm.getelementptr inbounds|nuw %73[%76] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.store %3, %77 : f64, !llvm.ptr
    %78 = llvm.mlir.constant(1 : index) : i64
    %79 = llvm.mlir.constant(0 : index) : i64
    %80 = llvm.extractvalue %56[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %81 = llvm.mlir.constant(3 : index) : i64
    %82 = llvm.mul %78, %81 overflow<nsw, nuw> : i64
    %83 = llvm.add %82, %79 overflow<nsw, nuw> : i64
    %84 = llvm.getelementptr inbounds|nuw %80[%83] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.store %2, %84 : f64, !llvm.ptr
    %85 = llvm.mlir.constant(1 : index) : i64
    %86 = llvm.mlir.constant(1 : index) : i64
    %87 = llvm.extractvalue %56[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %88 = llvm.mlir.constant(3 : index) : i64
    %89 = llvm.mul %85, %88 overflow<nsw, nuw> : i64
    %90 = llvm.add %89, %86 overflow<nsw, nuw> : i64
    %91 = llvm.getelementptr inbounds|nuw %87[%90] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.store %1, %91 : f64, !llvm.ptr
    %92 = llvm.mlir.constant(1 : index) : i64
    %93 = llvm.mlir.constant(2 : index) : i64
    %94 = llvm.extractvalue %56[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %95 = llvm.mlir.constant(3 : index) : i64
    %96 = llvm.mul %92, %95 overflow<nsw, nuw> : i64
    %97 = llvm.add %96, %93 overflow<nsw, nuw> : i64
    %98 = llvm.getelementptr inbounds|nuw %94[%97] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.store %0, %98 : f64, !llvm.ptr
    %99 = llvm.mlir.constant(0 : index) : i64
    %100 = llvm.mlir.constant(3 : index) : i64
    %101 = llvm.mlir.constant(1 : index) : i64
    llvm.br ^bb1(%99 : i64)
  ^bb1(%102: i64):  // 2 preds: ^bb0, ^bb5
    %103 = llvm.icmp "slt" %102, %100 : i64
    llvm.cond_br %103, ^bb2, ^bb6
  ^bb2:  // pred: ^bb1
    %104 = llvm.mlir.constant(0 : index) : i64
    %105 = llvm.mlir.constant(2 : index) : i64
    %106 = llvm.mlir.constant(1 : index) : i64
    llvm.br ^bb3(%104 : i64)
  ^bb3(%107: i64):  // 2 preds: ^bb2, ^bb4
    %108 = llvm.icmp "slt" %107, %105 : i64
    llvm.cond_br %108, ^bb4, ^bb5
  ^bb4:  // pred: ^bb3
    %109 = llvm.extractvalue %56[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %110 = llvm.mlir.constant(3 : index) : i64
    %111 = llvm.mul %107, %110 overflow<nsw, nuw> : i64
    %112 = llvm.add %111, %102 overflow<nsw, nuw> : i64
    %113 = llvm.getelementptr inbounds|nuw %109[%112] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    %114 = llvm.load %113 : !llvm.ptr -> f64
    %115 = llvm.extractvalue %39[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %116 = llvm.mlir.constant(2 : index) : i64
    %117 = llvm.mul %102, %116 overflow<nsw, nuw> : i64
    %118 = llvm.add %117, %107 overflow<nsw, nuw> : i64
    %119 = llvm.getelementptr inbounds|nuw %115[%118] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.store %114, %119 : f64, !llvm.ptr
    %120 = llvm.add %107, %106 : i64
    llvm.br ^bb3(%120 : i64)
  ^bb5:  // pred: ^bb3
    %121 = llvm.add %102, %101 : i64
    llvm.br ^bb1(%121 : i64)
  ^bb6:  // pred: ^bb1
    %122 = llvm.mlir.constant(0 : index) : i64
    %123 = llvm.mlir.constant(3 : index) : i64
    %124 = llvm.mlir.constant(1 : index) : i64
    llvm.br ^bb7(%122 : i64)
  ^bb7(%125: i64):  // 2 preds: ^bb6, ^bb11
    %126 = llvm.icmp "slt" %125, %123 : i64
    llvm.cond_br %126, ^bb8, ^bb12
  ^bb8:  // pred: ^bb7
    %127 = llvm.mlir.constant(0 : index) : i64
    %128 = llvm.mlir.constant(2 : index) : i64
    %129 = llvm.mlir.constant(1 : index) : i64
    llvm.br ^bb9(%127 : i64)
  ^bb9(%130: i64):  // 2 preds: ^bb8, ^bb10
    %131 = llvm.icmp "slt" %130, %128 : i64
    llvm.cond_br %131, ^bb10, ^bb11
  ^bb10:  // pred: ^bb9
    %132 = llvm.extractvalue %39[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %133 = llvm.mlir.constant(2 : index) : i64
    %134 = llvm.mul %125, %133 overflow<nsw, nuw> : i64
    %135 = llvm.add %134, %130 overflow<nsw, nuw> : i64
    %136 = llvm.getelementptr inbounds|nuw %132[%135] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    %137 = llvm.load %136 : !llvm.ptr -> f64
    %138 = llvm.fmul %137, %137 : f64
    %139 = llvm.extractvalue %22[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %140 = llvm.mlir.constant(2 : index) : i64
    %141 = llvm.mul %125, %140 overflow<nsw, nuw> : i64
    %142 = llvm.add %141, %130 overflow<nsw, nuw> : i64
    %143 = llvm.getelementptr inbounds|nuw %139[%142] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.store %138, %143 : f64, !llvm.ptr
    %144 = llvm.add %130, %129 : i64
    llvm.br ^bb9(%144 : i64)
  ^bb11:  // pred: ^bb9
    %145 = llvm.add %125, %124 : i64
    llvm.br ^bb7(%145 : i64)
  ^bb12:  // pred: ^bb7
    %146 = llvm.mlir.addressof @frmt_spec : !llvm.ptr
    %147 = llvm.mlir.constant(0 : index) : i64
    %148 = llvm.getelementptr %146[%147, %147] : (!llvm.ptr, i64, i64) -> !llvm.ptr, !llvm.array<4 x i8>
    %149 = llvm.mlir.addressof @nl : !llvm.ptr
    %150 = llvm.mlir.constant(0 : index) : i64
    %151 = llvm.getelementptr %149[%150, %150] : (!llvm.ptr, i64, i64) -> !llvm.ptr, !llvm.array<2 x i8>
    %152 = llvm.mlir.constant(0 : index) : i64
    %153 = llvm.mlir.constant(3 : index) : i64
    %154 = llvm.mlir.constant(1 : index) : i64
    llvm.br ^bb13(%152 : i64)
  ^bb13(%155: i64):  // 2 preds: ^bb12, ^bb17
    %156 = llvm.icmp "slt" %155, %153 : i64
    llvm.cond_br %156, ^bb14, ^bb18
  ^bb14:  // pred: ^bb13
    %157 = llvm.mlir.constant(0 : index) : i64
    %158 = llvm.mlir.constant(2 : index) : i64
    %159 = llvm.mlir.constant(1 : index) : i64
    llvm.br ^bb15(%157 : i64)
  ^bb15(%160: i64):  // 2 preds: ^bb14, ^bb16
    %161 = llvm.icmp "slt" %160, %158 : i64
    llvm.cond_br %161, ^bb16, ^bb17
  ^bb16:  // pred: ^bb15
    %162 = llvm.extractvalue %22[1] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    %163 = llvm.mlir.constant(2 : index) : i64
    %164 = llvm.mul %155, %163 overflow<nsw, nuw> : i64
    %165 = llvm.add %164, %160 overflow<nsw, nuw> : i64
    %166 = llvm.getelementptr inbounds|nuw %162[%165] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    %167 = llvm.load %166 : !llvm.ptr -> f64
    %168 = llvm.call @printf(%148, %167) vararg(!llvm.func<i32 (ptr, ...)>) : (!llvm.ptr, f64) -> i32
    %169 = llvm.add %160, %159 : i64
    llvm.br ^bb15(%169 : i64)
  ^bb17:  // pred: ^bb15
    %170 = llvm.call @printf(%151) vararg(!llvm.func<i32 (ptr, ...)>) : (!llvm.ptr) -> i32
    %171 = llvm.add %155, %154 : i64
    llvm.br ^bb13(%171 : i64)
  ^bb18:  // pred: ^bb13
    %172 = llvm.extractvalue %56[0] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    llvm.call @free(%172) : (!llvm.ptr) -> ()
    %173 = llvm.extractvalue %39[0] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    llvm.call @free(%173) : (!llvm.ptr) -> ()
    %174 = llvm.extractvalue %22[0] : !llvm.struct<(ptr, ptr, i64, array<2 x i64>, array<2 x i64>)> 
    llvm.call @free(%174) : (!llvm.ptr) -> ()
    llvm.return
  }
}
