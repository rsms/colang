# This example uses a `ns name {...}` syntax for setting the namespace scope

fun main {
  b1 = Buffer("hello")
  b1.len()  # => 5
  b2 Buffer
  b2.len()  # => 0  since it's zero initialized
} # b1.drop() and b2.drop() called here

struct Buffer {  # defining a struct also defines a namespace
  data []byte
  len  uint
}

fun Buffer(s str) {
  Buffer {
    data = memcopy(s)
    len = s.len()
  }
}

# We can explicitly declare stuff with namespaces:
fun Buffer.drop(b) {
  # called when the buffer drops out of scope, Rust style
  if b.cap > 0
    memfree(b.data)
}

# enter the namespace "struct Buffer"
ns Buffer {
  # Now things declared here are implicitly in the "Buffer" namespace

  fun cap(&b) -> b.data.cap()             # == fun Buffer.cap(&b) uint
  fun at(&b, uint index) -> b.data[index] # == fun Buffer.at(&b, uint index) byte

  fun append(mut &b, c byte) nil {
    if b.cap - b.len == 0 {
      b.data = memrealloc(b.data, b.cap * 2)
    }
    b.data[b.len] = c
    b.len++
  }

  # We can even make nested struct types this way; Buffer.View
  struct View {
    buf  &Buffer
    offs uint
  }

  fun View(&b, offs uint) {  # == Buffer.View(&b, offs uint) View
    assert(offs < b.len)
    View { buf = b; offs = offs }
  }

  ns View {
    fun len(&v) -> v.buf.len() - v.offs
    fun cap(&v) -> v.buf.cap() - v.offs
    fun buffer(&v) &Buffer -> &v.buf
  }

  # we can enter arbitrary namespaces. (This may be a bad idea.)
  ns foo {
    # Buffer.foo
    ns bar.baz {
      # Buffer.foo.bar.baz
      name = "fred"               # Buffer.foo.bar.baz.nametrix
      fun add(x, y int) -> x + y  # Buffer.foo.bar.baz.add(int,int) -> int
        # [?] It's a little weird that this fun does not take a Buffer "self" arg... but,
        #     if it did, it would be confusing as hell since e.g. Buffer.View.cap doesn't.
    }
  }
}
