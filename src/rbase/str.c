#include "rbase.h"

// // use the global memory allocator for strings
// #define s_malloc(z)    memalloc(NULL, (z))
// #define s_realloc(p,z) memrealloc(NULL, (p), (z))
// #define s_free(p)      memfree(NULL, (p))


// Str* StrNewLen(Mem mem, const void *init, size_t initlen) {
//   memcpy(s, init, initlen);
//   s[initlen] = '\0';
//   return s;
// }
