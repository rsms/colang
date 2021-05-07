# This example shows a byte buffer type and is _inspired_ by Rust's
# memory mamangement. Rust's Ownership Rules are as follows:
# - Each value in Rust has a variable that’s called its owner.
# - There can only be one owner at a time.
# - When the owner goes out of scope, the value will be dropped.
# However, this example does not use references and mutable references
# like Rust does. Instead it uses a simpler less restrictive model:
# - Moving a thing is explicitly defined by an ampersand on an argument
# - Without an ampersand things are referenced/borrowed

fun main {
  b1 = Buffer("hello")
  b1.len()  # => 5
  b2 = b1   # value of b1 moved to b2
  # b1 is invalid here
  log("first byte of buffer: %v", first_byte(b2))
  print_buf(b2)  # b2 moves into print_buf
}

fun first_byte(a ByteArray) byte -> a[0]

fun set_first_byte(a mut ByteArray, b byte) {
  a[0] = b
}

fun print_buf(b &Buffer) {  # & here means b takes over ownership
  for i = 0; i < b.len(); i++ {
    print("b[%v] = %v", i, b[i])
  }
} # b.drop() called here

interface ByteArray {
  [uint index] byte
}

type Buffer {
  data []byte
  len  uint
  cap  uint
}

fun Buffer(s str) {
  .len = s.len()
  .cap = .len
  .data = memalloc(.len)
  memcopy(.data, s)
}

fun Buffer.drop() {
  # called when the buffer drops out of scope, Rust style
  memfree(.data)
}

fun Buffer.len() -> .len
fun Buffer.cap() -> .cap

# implementing [uint]->byte makes Buffer the shape of ByteArray
fun Buffer.[uint index] -> .data[index]

fun Buffer.append(b byte) nil {
  if .cap - .len == 0 {
    .data = memrealloc(.data, .cap * 2)
  }
  .data[.len] = b
  .len++
}
