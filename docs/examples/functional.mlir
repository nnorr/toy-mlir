// docs/05-dialect.md: ranked operands with an unranked result, which forces the
// binary-op printer's functional form. Hand-written because no .toy program
// reaches this state at a point where the IR is printed.
toy.func @f(%a: tensor<2x3xf64>) {
  %0 = "toy.mul"(%a, %a) : (tensor<2x3xf64>, tensor<2x3xf64>) -> tensor<*xf64>
  toy.return
}
