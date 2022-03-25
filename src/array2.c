// // array -- growable typed arrays
// #ifndef CO_IMPL
//   #include "coimpl.h"
//   #define IMPLEMENTATION
// #endif
// //———————————————————————————————————————————————————————————————————————————————————————
// #include "mem2.c"

// #define Array(T) rarray  // for documentation only

// typedef struct rarray {
//   u8* nullable v; // u8 so we get -Wincompatible-pointer-types if we access .v directly
//   u32 len, cap;
// } rarray;

// #define rarray_at(T, a, index)             (((T*)(a)->v) + (index))
// #define rarray_at_safe(T, a, i)            ({safecheck((i)<(a)->len);rarray_at(T,(a),(i));})
// #define rarray_push(T, a, m)               ((T*)_rarray_push((a),(m),sizeof(T)))
// #define rarray_remove(T, a, start, len)    _rarray_remove((a),sizeof(T),(start),(len))
// #define rarray_move(T, a, dst, start, end) _array_move(sizeof(T),(a)->v,(dst),(start),(end))
// #define rarray_free(T, a, m)   if ((a)->v)mem_free((m),(a)->v,(usize)(a)->cap*sizeof(T))
// #define rarray_reserve(T, a, m, addl)      _rarray_reserve((a),(m),sizeof(T),(addl))


// //—————— internal ——————

// bool rarray_grow(rarray* a, Mem2, usize elemsize, u32 addl);
// bool _rarray_reserve(rarray* a, Mem2, usize elemsize, u32 addl);
// void _rarray_remove(rarray* a, u32 elemsize, u32 start, u32 len);

// inline static void* nullable _rarray_push(rarray* a, Mem2 m, u32 elemsize) {
//   if (a->len == a->cap && UNLIKELY(!rarray_grow(a, m, (usize)elemsize, 1)))
//     return NULL;
//   return a->v + elemsize*(a->len++);
// }

// // _array_move moves the chunk [src,src+len) to index dst. For example:
// //   _array_move(z, v, 5, 1, 1+2) = [1  2 3  4 5|6 7 8] ⟹ [1 4 5  2 3  6 7 8]
// //   _array_move(z, v, 1, 4, 4+2) = [1|2 3 4  5 6  7 8] ⟹ [1  5 6  2 3 4 7 8]
// #define _array_move(elemsize, v, dst, start, end) (                                 \
//   (elemsize) == 4 ? _AMOVE_ROTATE(_arotate32,(dst),(start),(end),(u32* const)(v)) : \
//   (elemsize) == 8 ? _AMOVE_ROTATE(_arotate64,(dst),(start),(end),(u64* const)(v)) : \
//                     _AMOVE_ROTATE(_arotatemem,(dst),(start),(end),(elemsize),(v)) )
// #define _AMOVE_ROTATE(f, dst, start, end, args...) (     \
//   ((start)==(dst)||(start)==(end)) ? ((void)0) :         \
//   ((start) > (dst)) ? (f)(args, (dst), (start), (end)) : \
//   (f)(args, (start), (end), (dst)) )

// // arotate rotates the order of v in the range [first,last) in such a way
// // that the element pointed to by "mid" becomes the new "first" element.
// // Assumes first <= mid < last.
// #define arotate(elemsize, v, first, mid, last) (                          \
//   (elemsize) == 4 ? _arotate32((u32* const)(v), (first), (mid), (last)) : \
//   (elemsize) == 8 ? _arotate64((u64* const)(v), (first), (mid), (last)) : \
//   _arotatemem((elemsize), (v), (first), (mid), (last)) )
// void _arotatemem(u32 stride, void* v, u32 first, u32 mid, u32 last);
// void _arotate32(u32* const v, u32 first, u32 mid, u32 last);
// void _arotate64(u64* const v, u32 first, u32 mid, u32 last);

// //———————————————————————————————————————————————————————————————————————————————————————
// #ifdef IMPLEMENTATION // implementation

// // void _rarray_remove(rarray* a, u32 elemsize, u32 start, u32 len) {
// //   if (len == 0)
// //     return;
// //   safecheckf(start+len <= a->len, "end=%u > len=%u", start+len, a->len);
// //   if (start+len < a->len) {
// //     void* dst = a->v + elemsize*start;
// //     void* src = dst + elemsize*len;
// //     memmove(dst, src, elemsize*(a->len - start - len));
// //   }
// //   a->len -= len;
// // }

// // bool rarray_grow(rarray* a, Mem2 m, usize elemsize, u32 addl) {
// //   u32 newcap = a->cap ? (u32)MIN((u64)a->cap * 2, U32_MAX) : MAX(addl, 4);
// //   usize newsize;
// //   if (check_mul_overflow((usize)newcap, elemsize, &newsize))
// //     return false;
// //   void* p2 = mem_resize(m, a->v, a->cap*elemsize, newsize);
// //   if UNLIKELY(!p2)
// //     return false;
// //   a->v = p2;
// //   a->cap = newcap;
// //   return true;
// // }

// // bool _rarray_reserve(rarray* a, Mem2 m, usize elemsize, u32 addl) {
// //   u32 len;
// //   if (check_add_overflow(a->len, addl, &len))
// //     return false;
// //   if (len >= a->cap && UNLIKELY(!rarray_grow(a, m, elemsize, addl)))
// //     return false;
// //   return true;
// // }

// // void _arotatemem(u32 stride, void* v, u32 first, u32 mid, u32 last) {
// //   assert(first <= mid); // if equal (zero length), do nothing
// //   assert(mid < last);
// //   usize tmp[16]; assert(sizeof(u32) <= sizeof(tmp));
// //   u32 next = mid;
// //   while (first != next) {
// //     // swap
// //     memcpy(tmp, v + first*stride, stride); // tmp = v[first]
// //     memcpy(v + first*stride, v + next*stride, stride); // v[first] = v[next]
// //     memcpy(v + next*stride, tmp, stride); // v[next] = tmp
// //     first++;
// //     next++;
// //     if (next == last) {
// //       next = mid;
// //     } else if (first == mid) {
// //       mid = next;
// //     }
// //   }
// // }

// // #define DEF_AROTATE(NAME, T)                                   \
// //   void NAME(T* const v, u32 first, u32 mid, u32 last) { \
// //     assert(first <= mid);                                      \
// //     assert(mid < last);                                        \
// //     u32 next = mid;                                            \
// //     while (first != next) {                                    \
// //       T tmp = v[first]; v[first++] = v[next]; v[next++] = tmp; \
// //       if (next == last) next = mid;                            \
// //       else if (first == mid) mid = next;                       \
// //     }                                                          \
// //   }

// // DEF_AROTATE(_arotate32, u32)
// // DEF_AROTATE(_arotate64, u64)

// #endif //
