
fun hello(s str) {
  s1 = Buf(5){}     # comptime => struct{data [5]byte; len uint}{{0,0,0,0,0},0}
  s3 = Buf("hello") # comptime => struct{data [5]byte; len uint}{"hello",5}
  s2 = Buf(s)       # runtime
  l1 = s1.len()     # comptime => 5
  l2 = s2.len()     # comptime => 5
  l3 = s3.len()     # runtime
  l2.at(0)          # comptime => 'h'
  l2.at(9)          # comptime error: out of bounds
  l3.at(0)          # runtime
  l2.at(9)          # runtime panic: out of bounds
}

interface ByteArray {
  data []byte
  len  uint
}

fun ByteArray.at(a, index uint) byte -> a.data[index]
fun ByteArray.at(comptime a, comptime index uint) byte -> a.data[index]

fun Buf(comptime size uint) Type {
  struct {
    data [size]byte  # array, not slice
    len  uint
  }
}

fun Buf(comptime a [uint]byte) ByteArray {
  Buf(len(a)) {
    data = a
    len  = len(a)
  }
}

fun Buf(s str) ByteArray {
  st = struct {
    data []byte  # slice, not array
    len  uint
  }{
    data = memalloc(len(s))
    len  = len(s)
  }
  memcopy(st.data, s)
  st
}
