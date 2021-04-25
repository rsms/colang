const start = 5
const one = 1

var foo int
const no = false
const t = int
const f = fun -> 1

fun main {
  var x = true
  factorial(start)
}

# Factorial function
fun factorial(n int) t {
  const zero = 0
  if n == zero {
    one
  } else {
    n * factorial(n - 1)
  }
}
