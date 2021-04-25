# comment


# const lol int = 5
# var foo Foo
# var red, green int = 4, 5
# var x int = 8
# var A, B, C int
# var a, b int = 1, 2
# var r, g, b = 255, 128, 5
# r, g, b = 255, 128, (g = 5)

# fun lol(int, int32, Foo) int64
# var f fun(int, int32) int64

const start = 5

fun main {
  # var x = 1
  factorial(start)
}

fun factorial(n int) int {
  if n == 0 {
    1
  } else {
    n * factorial(n - 1)
  }
}

# fun multiply(x, y int, z int32) int {
#   if x > y {
#     x * y * z
#   } else if x == 0 {
#     return 8
#   } else {
#     x / y * z
#   }
# }



# z = { x = 6; 5 * x }

# # r, g, b = 255, 128, g = 5  # invalid:
# # (Assign =
# #   (ExprList
# #     (Ident r)
# #     (Ident g)
# #     (Ident b))
# #   (Assign =
# #     (ExprList
# #       (Int 255)
# #       (Int 128)
# #       (Ident g))
# #     (Int 5)))

# 4 + # let's add four
# 5 + # and five to
# 6   # six
# foo + bar * baz

# oändlig  # C3 A4
# 😀 = 1337

# # !$lol; int # another comment
# # foo * bar + 8
# # const lol, foo, bar = 9, 7, 0
# # var cat = 6
# # x ++
# # y --
# # 3 * 9
# # -1 + 5

# fun multiply (x, y int, z i32) int {
#   x * y * z
# }

# # multiply = (x, y int, z i32) -> {
# #   x * y * z
# # }

# # fun map<T>(c Collection<T>, f (T,int)->str) str
# # fun map<T>(c Collection<T>, f fun(T,int)str) str

# # names = map(entries, (entry, index) -> entry.name)
