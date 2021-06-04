#include <rbase/rbase.h>
#include "tmpstr.h"

// _tmpstr holds per-thread temporary string buffers for use by fmtnode and fmtast.
static thread_local struct {
  u32 index;   // next buffer index (effective index = index % TMPSTR_MAX_CONCURRENCY)
  Str bufs[TMPSTR_MAX_CONCURRENCY];
} _tmpstr = {0};


Str* tmpstr_get() {
  u32 bufindex = _tmpstr.index % TMPSTR_MAX_CONCURRENCY;
  _tmpstr.index++;
  Str s = _tmpstr.bufs[bufindex];
  if (!s) {
    _tmpstr.bufs[bufindex] = str_new(64);
  } else {
    str_trunc(s);
  }
  return &_tmpstr.bufs[bufindex];
}
