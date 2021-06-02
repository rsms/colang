# thing = 123 + PI
# PI = 3141592 as float64

fun main() {
  a = 12
  b = a
  return add1(a, b)
}

fun add1(x int, y uint) int {
  add(x + 1, y)
}

fun add(x int, y uint) int {
  x + (y as int)
}
