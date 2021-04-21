#include "rbase.h"
#include <pwd.h> // getpwuid

ASSUME_NONNULL_BEGIN

static size_t _mempagesize = 0;

size_t mempagesize() {
  if (_mempagesize == 0) {
    _mempagesize = (size_t)sysconf(_SC_PAGESIZE);
    if (_mempagesize <= 0)
      _mempagesize = 1024 * 4;
  }
  return _mempagesize;
}

void fmthex(char* out, const u8* indata, int len) {
  const char* hex = "0123456789abcdef";
  for (int i = 0; i < len; i++) {
    out[0] = hex[(indata[i]>>4) & 0xF];
    out[1] = hex[ indata[i]     & 0xF];
    out += 2;
    // sprintf((char*)dst+2*i, "%02x", digest[i]);
  }
}

const char* user_home_dir() {
  struct passwd* pw = getpwuid(getuid());
  if (pw && pw->pw_dir)
    return pw->pw_dir;
  auto home = getenv("HOME");
  if (home)
    return home;
  return "";
}


ASSUME_NONNULL_END
