# order-independent
β = add(α, 10)
α = add(12, 34)

fun main() int {
  α + β
}

fun add(x int, y uint) int {
  x + (y as int)
}
