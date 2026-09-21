// docs/05-dialect.md: returns a value from a function declaring no results, so
// ReturnOp::verify rejects it. Invalid on purpose.
toy.func @main() {
  %0 = toy.constant dense<5.5> : tensor<f64>
  toy.return %0 : tensor<f64>
}
