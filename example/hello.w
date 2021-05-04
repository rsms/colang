
fun main() nil {
  a = 2
  b = a
  add(a, b)
}

fun add(x int, y uint) int {
  x + (y as int)
}
