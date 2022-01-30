#include "coimpl.h"
#include "sbuf.h"
#include "test.h"
#include "str.h" // strfmtu64

void sbuf_append(SBuf* s, const char* p, usize len) {
  usize z = MIN(len, (uintptr)(s->lastp - s->p));
  memcpy(s->p, p, z);
  s->p += z;
  if (check_add_overflow(s->len, len, &s->len))
    s->len = USIZE_MAX;
}


void sbuf_appendu32(SBuf* s, u32 v, u32 base) {
  char buf[32];
  u32 n = strfmtu32(buf, v, base);
  return sbuf_append(s, buf, n);
}


DEF_TEST(sbuf) {
  {
    char buf[6];
    SBuf s = SBUF_INITIALIZER(buf, sizeof(buf));
    sbuf_appendc(&s, 'a');
    sbuf_appendc(&s, 'b');
    sbuf_appendc(&s, 'c');
    sbuf_terminate(&s);
    asserteq(s.len, 3);
    asserteq(strlen(buf), 3);
  }
  {
    char buf[3];
    SBuf s = SBUF_INITIALIZER(buf, sizeof(buf));
    sbuf_appendc(&s, 'a');
    sbuf_appendc(&s, 'b');
    sbuf_appendc(&s, 'c');
    asserteq(buf[2], 'c');
    sbuf_appendc(&s, 'd');
    asserteq(buf[2], 'd');
    sbuf_terminate(&s);
    asserteq(s.len, 4);
    asserteq(strlen(buf), 2);
    asserteq(buf[0], 'a');
    asserteq(buf[1], 'b');
    asserteq(buf[2], 0);
  }
  {
    char buf[6];
    SBuf s = SBUF_INITIALIZER(buf, sizeof(buf));
    sbuf_append(&s, "abcd", 4);
    sbuf_append(&s, "efgh", 4);
    sbuf_append(&s, "ijkl", 4);
    sbuf_terminate(&s);
    asserteq(s.len, 12);
    asserteq(strlen(buf), 5);
    assert(memcmp(buf, "abcde\0", 6) == 0);
  }
  { // overflow
    char buf[6];
    SBuf s = SBUF_INITIALIZER(buf, sizeof(buf));
    s.len = USIZE_MAX - 1;
    sbuf_append(&s, "abc", 3);
    sbuf_terminate(&s);
    asserteq(s.len, USIZE_MAX);
    asserteq(strlen(buf), 3);
    assert(memcmp(buf, "abc\0", 4) == 0);
  }
}
