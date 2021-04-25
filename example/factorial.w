# <
# <=
# <<
# <<=
# >
# >=
# >>
# >>=

fun main {
  # a, b = 1, 2 + 1
  # z = 20 as int8
  # a = z as int16
  # a = int16(20)
  # b = int64(arg0)
  # k = x / y * z # oops! Right-associate but should be left-associative

  # a = 1 + 2                         # 1  left & right are untyped
  # a = 2 + (1 as uint32)             # 2  left is untyped, right is typed
  # a = (1 as uint32) + 2             # 3  left is typed, right is untyped
  # a = (1 as uint32) + (2 as uint32) # 4  left & right are typed

  # a = 4
  # b = a
  # y = b + 1

  z = if true {
    a = 4  # avoid block elimination while working on ir builder
    y = a + 1
  } else {
    0
  }

  z

  # factorial(start)
}

# fun foo(i int) -> i

# # Factorial function
# fun factorial(n int) int {
#   if n <= 0 {
#     1
#   } else {
#     n * factorial(n - 1)
#   }
# }

# fun factorial(n float32) float32 {
#   if n <= 0.0 {
#     1.0
#   } else {
#     n * factorial(n - 1.0)
#   }
# }

# fun factorial(n int) int {
#   # y = 3
#   # x, y, _ = 1, 2, 3
#   # t = (1, 2, 3)
#   # xs = for x in [1,2,3] { x * 2 }
#   # if n <= 0 1 else n * factorial(n - 1)
#   if n <= 0 {
#     1
#   } else {
#     n * factorial(n - 1)
#   }
# }
