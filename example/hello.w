thing = 123 + PI
PI = 3141592 as float64

fun main() {
  a = 12
  b = a
  addfn1 = add1
  addfn2 = addfn1
  return addfn2(a, b)
}

fun add1(x int, y uint) int {
  add(x + 1, y)
}

fun add(x int, y uint) int {
  x + (y as int)
}
