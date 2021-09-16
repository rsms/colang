#include "../common.h"
#include "error.h"

typedef struct TLS {
  ErrorValue v;
  char*      msgbuf;  // message storage
  size_t     msgbufz; // size of msgbuf
} TLS;

static thread_local TLS g_err_storage = {0};


Error err_make(int code, const char* message) {
  ErrorValue* v = &g_err_storage.v;
  v->code = code;
  v->message = message;
  return v;
}


static void msgbuf_grow(TLS* st, size_t len) {
  len = MIN(512, align2(len, sizeof(void*)));
  st->msgbuf = memrealloc(MemHeap, st->msgbuf, len);
  st->msgbufz = len;
}


Error err_makefv(int code, const char* fmt, va_list ap) {
  auto st = &g_err_storage;

  // start by making an educated guess for space needed: 2x that of the format + nul
  size_t len = (strlen(fmt) * 2) + 1;

  va_list ap2;
  while (1) {
    if (st->msgbufz < len) {
      msgbuf_grow(st, len);
      if (st->msgbuf == NULL) {
        // memory allocation failure
        return err_make(code, "(err_make failed to memalloc)");
      }
    }
    va_copy(ap2, ap); // copy va_list as we might read it twice
    int n = vsnprintf(st->msgbuf, len, fmt, ap2);
    va_end(ap2);
    if (n < (int)len) {
      // ok; result fit in buf.
      // Theoretically vsnprintf might return -1 on error, but AFAIK no implementation does
      // unless len > INT_MAX, so we are likely fine with ignoring that case here.
      len = (size_t)n;
      break;
    }
    // vsnprintf tells us how much space it needs
    len = (size_t)n + 1;
  }

  st->v.code = code;
  st->v.message = st->msgbuf;
  return &st->v;
}


Error err_makef(int code, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  Error e = err_makefv(code, fmt, ap);
  va_end(ap);
  return e;
}


R_TEST(error) {
  assertnull(ErrorNone);
  assertnotnull(err_make(0, ""));
  Error e = err_makef(123, "hello %u (%s)", 45u, "lol");
  assertcstreq(err_msg(e), "hello 45 (lol)");
}
