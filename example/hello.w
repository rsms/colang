# x = 4 # unused

fun main {
  a = 2
  b = a
  add(a, b)
}

fun add(x int, y uint) int {
  x + y
}
