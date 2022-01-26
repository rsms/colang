#include "parse.h"

static const char* const _DiagLevelName[DiagMAX + 1] = {
  "error",
  "warn",
  "note",
};

const char* DiagLevelName(DiagLevel l) {
  return _DiagLevelName[MAX(0, MIN(l, DiagMAX))];
}

Str diag_fmt(const Diagnostic* d, Str s) {
  assert(d->level <= DiagMAX);
  return pos_fmt(&d->build->posmap, d->pos, s,
    "%s: %s", DiagLevelName(d->level), d->message);
}

void diag_free(Diagnostic* d) {
  assert(d->build != NULL);
  memfree(d->build->mem, (void*)d->message);
  memfree(d->build->mem, d);
}
