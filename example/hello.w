
fun main() {
  a = 2
  b = a
  addfn = add
  return addfn(a, b)
}

fun add(x int, y uint) int {
  x + (y as int)
}

# This crashes parse.c
# fun main() nil {
#   addfn = add
#   return addfn(1, 2)
# }
# fun add(x, y int) int {
#   x + y
# }
