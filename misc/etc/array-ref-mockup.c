/*
autorun array-ref-mockup.c -- \
  "clang -emit-llvm -c -S -o array-ref-mockup.ll array-ref-mockup.c"
*/
#define strlen __builtin_strlen
#define memcpy __builtin_memcpy

struct mut_slice { int len; int cap; int* ptr; }; // mut&[int]
struct imu_slice { int len; int* ptr; };          // &[int]
struct dynarray { int len; int cap; int* ptr; };  // [int]

struct dynarray alloc_i32x3() { // fun alloc(T, cap) ⟶ [T]
  static int fakeheap[3];
  return (struct dynarray){ 0, 3, fakeheap };
}

int first(struct imu_slice s) { // fun look(s &[int]) -> s[0]
  return s.ptr[0];
}

int main(int argc, const char** argv) {
  int a1[3] = {10,20,30}; // really just a pointer to stack data
  // int a2[2] = a1;      // error: array initializer must be an initializer list
  int* s1 = a1;           // s1 = a1[:]  ⟶ mut&[int 3]
  int* s2 = a1+1;         // s1 = a1[1:] ⟶ mut&[int 2]

  // copy local stack array to heap
  struct dynarray ha = alloc_i32x3(); // alloc(int, 3) ⟶ [T]
  ha.len = 3; // ha.len = sa.len
  memcpy(ha.ptr, s1, 3); // copy(ha, s1)

  int v1 = first((struct imu_slice){3,s1}); // v1 = first(&s1)

  // Automatic casts:
  // digraph {
  //   graph [ rankdir="LR" ];
  //
  //   "[T n]" -> "mut&[T n]"
  //   "[T n]" -> "&[T n]"
  //   "mut&[T n]" -> "&[T n]" -> "&[T]"
  //   "mut&[T n]" -> "mut&[T]" -> "&[T]"
  //   "mut&[T n]" -> "&[T]"
  //
  //   "[T]" -> "mut&[T]"
  //
  //   "alloc(T)" -> "[T]"
  //   "var [T n]" -> "[T n]"
  // }
  //
  // Only way to get a [T] is with alloc(), [T n] on stack or global.
  //
  // ┌───────────┐     ┌────────┐     ┌───────────┐     ┌──────┐
  // │ alloc(T)  │ ──▶ │  [T]   │ ──▶ │  mut&[T]  │ ──▶ │ &[T] │
  // └───────────┘     └────────┘     └───────────┘     └──────┘
  //                                    ▲                 ▲  ▲
  //                                    │                 │  │
  // ┌───────────┐     ┌────────┐     ┌───────────┐       │  │
  // │ var [T n] │ ──▶ │ [T n]  │ ──▶ │ mut&[T n] │ ──────┘  │
  // └───────────┘     └────────┘     └───────────┘          │
  //                     │    ┌─────────────┘                │
  //                     ▼    ▼                              │
  //                   ┌────────┐                            │
  //                   │ &[T n] │ ───────────────────────────┘
  //                   └────────┘
  //

  // int d[3] = {10,argc,12};
  // d[1] = 4;
  // return (int)d[1];

  return 0;
}
