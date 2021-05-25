fun main() {
  a = 2
  b = a
  addfn1 = add
  addfn2 = addfn1
  return addfn2(a, b)
}

fun add(x int, y uint) int {
  x + (y as int)
}
