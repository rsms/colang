#include "../common.h"
#include "stk_array.h"
#include "str_extras.h"

Str str_fmtpat(Str s, Mem mem, const char* fmt, u32 kvc, const char** kvv) {
  assertf(kvc % 2 == 0, "kvc=%u must be an even number", kvc);
  u32 fmtlen = strlen(fmt); assert_debug(fmtlen < 0x7fffffff);
  s = str_makeroom(s, fmtlen * 2);
  u32 chunk_start = 0;
  u32 keynest = 0; // "{" nesting level

  #define FLUSH_CHUNK(chunk_end) do { \
    if (chunk_end > chunk_start) { \
      s = str_append(s, &fmt[chunk_start], chunk_end - chunk_start); \
      chunk_start = chunk_end; \
    } \
  } while(0)

  for (u32 i = 0; i < fmtlen; i++) {
    again:
    switch (fmt[i]) {
      case '\\': // note: not supported for keys
        FLUSH_CHUNK(i);
        i++;
        if (i < fmtlen)
          s = str_appendc(s, fmt[i]);
        chunk_start = i + 1;
        break;
      case '{':
        keynest++;
        if (keynest == 1) {
          FLUSH_CHUNK(i);
          chunk_start = i + 1;
        }
        break;
      case '}':
        if (keynest > 0) {
          keynest--;
          if (keynest == 0) {
            const char* key = &fmt[chunk_start];
            u32 keylen = i - chunk_start;
            chunk_start = i + 1;
            if (keylen > 0) for (u32 j = 0; j < kvc; j += 2) {
              const char* k = kvv[j];
              if (k[0] == key[0] && strncmp(k, key, keylen) == 0) {
                const char* v = kvv[j + 1];
                s = str_appendcstr(s, v);
                i++;
                goto again;
              }
            }
            s = str_appendcstr(s, "<?");
            s = str_append(s, key, keylen);
            s = str_appendcstr(s, "?>");
          }
        }
        break;
      default:
        break;
    }
  }

  FLUSH_CHUNK(fmtlen);
  #undef FLUSH_CHUNK

  return s;
}

R_TEST(str_fmtpat) {
  const char* kv[] = {
    "var1", "value1",
    "var2", "value2",
  };
  const char* fmt = "foo {var1} bar {var2} \\{var1} {} baz {var3}.";
  Str s = str_fmtpat(str_new(0), MemHeap, fmt, countof(kv), kv);
  assertcstreq(s, "foo value1 bar value2 {var1} <?""?> baz <?var3?>.");
  str_free(s);
}
