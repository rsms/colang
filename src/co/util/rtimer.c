#include <rbase/rbase.h>
#include "tmpstr.h"
#include "tstyle.h"
#include "rtimer.h"

void rtimer_start(RTimer* rt) {
  getrusage(RUSAGE_SELF, &rt->ru);
}

u64 rtimer_duration(RTimer* rt) {
  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);
  u64 ns = (u64)(ru.ru_utime.tv_sec - rt->ru.ru_utime.tv_sec) * 1000000000;
  return ns + (u64)(ru.ru_utime.tv_usec - rt->ru.ru_utime.tv_usec) * 1000; // libc with usec
  // return ns + (u64)(ru.ru_utime.tv_nsec - rt->ru.ru_utime.tv_nsec); // libc with nsec
}

Str rtimer_duration_str(RTimer* rt, Str s) {
  u64 duration = rtimer_duration(rt);
  char buf[40];
  auto buflen = fmtduration(buf, countof(buf), duration);
  return str_append(s, buf, buflen);
}

void rtimer_log(RTimer* rt, const char* fmt, ...) {
  u64 duration = rtimer_duration(rt);

  Str* sp = tmpstr_get();
  Str s = *sp;
  auto style = TSTyleForStderr();
  s = str_appendcstr(s, style[TStyle_lightpurple]);
  s = str_appendcstr(s, "◔ ");

  char durbuf[40];
  auto durbuflen = (u32)fmtduration(durbuf, countof(durbuf), duration);
  s = str_append(s, durbuf, durbuflen);

  // pad
  const char* spaces = "      ";
  const u32 spaceslen = (u32)strlen(spaces);
  if (durbuflen < spaceslen)
    s = str_append(s, spaces, spaceslen - durbuflen);

  s = str_appendc(s, ' ');

  va_list ap;
  va_start(ap, fmt);
  s = str_appendfmtv(s, fmt, ap);
  va_end(ap);

  s = str_appendcstr(s, style[TStyle_none]);
  s = str_appendc(s, '\n');
  fwrite(s, str_len(s), 1, stderr);
  *sp = s; // store back
}
