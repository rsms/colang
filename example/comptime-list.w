
fun main() nil {
  ints [10]int32
  xs = List(int) {  # typeof(xs) => struct { items [int]; len uint }
    items = &ints
    len = 0
  }
}

fun List(comptime T Type) Type {
  struct {
    items [T]
    len   uint
  }
}

# List(int) is equivalent to this:
type IntList struct {
  items [int]
  len   uint
}
