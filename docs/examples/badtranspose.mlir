// docs/05-dialect.md: the result shape is not a transpose of the input, so
// TransposeOp::verify rejects it. Invalid on purpose.
toy.func @main() {
  %0 = toy.constant dense<[[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]]> : tensor<2x3xf64>
  %1 = toy.transpose(%0 : tensor<2x3xf64>) to tensor<2x3xf64>
  toy.print %1 : tensor<2x3xf64>
  toy.return
}
