#define memcpy __builtin_memcpy

#define SAFE

struct darray          { int* ptr; int len; int cap; };                         // [int]
#ifdef SAFE
 struct darray_ref     { int* ptr; int* ptr_base; int gen; int len; };          // &[int]
 struct darray_ref_mut { int* ptr; int* ptr_base; int gen; int len; int cap; }; // mut&[int]
 //struct ref          { int* ptr; int gen; };                                  // &T

 #define get_gen(ptr) (*(((int*)ptr)-1))
 #define check_gen(ptr,gen) ((void)0) /*TODO assert(get_gen(ref.ptr_base) == gen)*/
 // [int] ⟶ mut&[int]
 #define mk_darray_slice_mut(a, start, end)                                 \
  (struct darray_ref_mut){                                                  \
    .ptr=a.ptr+start, .ptr_base=a.ptr, .gen=get_gen(a.ptr), .len=end-start, \
    .cap=a.cap-start }
 // [int] ⟶ &[int]
 #define mk_darray_slice(a, start, end) \
  (struct darray_ref){                  \
    .ptr=a.ptr+start, .ptr_base=a.ptr, .gen=get_gen(a.ptr), .len=end-start }
 // mut&[int] ⟶ mut&[int]
 #define mk_darray_ref_slice_mut(s, start, end)                         \
  (struct darray_ref_mut){                                              \
    .ptr=s.ptr+start, .ptr_base=s.ptr_base, .gen=s.gen, .len=end-start, \
    .cap=s.cap-start }
 // &[int] ⟶ &[int], mut&[int] ⟶ &[int]
 #define mk_darray_ref_slice(s, start, end) \
  (struct darray_ref){                      \
    .ptr=s.ptr+start, .ptr_base=s.ptr_base, .gen=s.gen, .len=end-start }
 // [int n] ⟶ &[int]
 #define mk_sarray_slice(aptr, start, end) \
  (struct darray_ref){ .ptr=aptr+start, .ptr_base=aptr, .gen=0, .len=end-start }

#else
 struct darray_ref     { int* ptr; int len; };
 struct darray_ref_mut { int* ptr; int len; int cap; };
 //struct ref          { int* ptr; };

 #define check_gen(ptr,gen) ((void)0) /*TODO assert(get_gen(ref.ptr_base) == gen)*/
 // [int] ⟶ mut&[int]
 #define mk_darray_slice_mut(a, start, end)                                 \
  (struct darray_ref_mut){ .ptr=a.ptr+start, .len=end-start, .cap=a.cap-start }
 // [int] ⟶ &[int]
 #define mk_darray_slice(a, start, end) \
  (struct darray_ref){ .ptr=a.ptr+start, .len=end-start }
 // mut&[int] ⟶ mut&[int]
 #define mk_darray_ref_slice_mut(s, start, end)                         \
  (struct darray_ref_mut){ .ptr=s.ptr+start, .len=end-start, .cap=s.cap-start }
 // &[int] ⟶ &[int], mut&[int] ⟶ &[int]
 #define mk_darray_ref_slice(s, start, end) \
  (struct darray_ref){ .ptr=s.ptr+start, .len=end-start }
 // [int n] ⟶ &[int]
 #define mk_sarray_slice(aptr, start, end) \
  (struct darray_ref){ .ptr=aptr+start, .len=end-start }
#endif

struct darray alloc_i32x3() { // fun alloc(T, cap) ⟶ [T]
  static int fakeheap[4];
  fakeheap[0]++; // gen (allocation generation)
  return (struct darray){ .ptr=fakeheap+1, .cap=3, .len=0 };
}

int first(struct darray_ref s) { // fun first(s &[int]) -> s[0]
  check_gen(s.ptr_base, s.gen);
  return s.ptr[0];
}

int main(int argc, const char** argv) {
  // local (stack) array (also applies to global arrays)
  int la1[3] = {10,20,30}; // la1 = [10,20,30] ⟶ [int 3] (pointer to stack data)
  int* as1 = la1;          // as1 = la1[:2] ⟶ mut&[int 2]
  int* as2 = la1+1;        // as2 = la1[1:] ⟶ mut&[int 2]
  int* as3 = as2+1;        // as3 = as2[1:] ⟶ mut&[int 1]

  // heap array
  struct darray ha1 = alloc_i32x3(); // ha1 = alloc(int, 3) ⟶ [int]
  ha1.ptr[ha1.len++] = 1;            // ha1.push(1)
  ha1.ptr[ha1.len++] = 2;            // ha1.push(2)
  ha1.ptr[ha1.len++] = 3;            // ha1.push(3)

  // hs1 = ha1[:2] ⟶ mut&[int]
  struct darray_ref_mut hs1 = mk_darray_slice_mut(ha1, 0, 2);

  // hs2 = ha1[1:] ⟶ mut&[int]
  struct darray_ref_mut hs2 = mk_darray_slice_mut(ha1, 1, ha1.len);

  // const hs3 = ha1[1:] ⟶ &[int]
  struct darray_ref hs3 = mk_darray_slice(ha1, 1, ha1.len);

  // hs4 = hs1[1:] ⟶ mut&[int]
  struct darray_ref_mut hs4 = mk_darray_ref_slice_mut(hs1, 1, ha1.len);

  // copy local array to heap array:
  memcpy(ha1.ptr, la1, 3);  // copy(ha1, la1)

  // v1 = first(&as1)
  //   implicit slice created: mut&[int 2] ⟶ &[int]
  int v1 = first(mk_sarray_slice(as1, 0, 3));

  // Possible conversions:
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
  // Only way to get a [T] is with alloc() and [T n] via local or global var.
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
