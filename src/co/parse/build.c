#include <rbase/rbase.h>
#include "parse.h"

void build_errf(const BuildCtx* ctx, const Source* src, SrcPos pos, const char* format, ...) {
  if (ctx->errh == NULL)
    return;

  va_list ap;
  va_start(ap, format);
  auto msg = str_new(32);
  if (strlen(format) > 0)
    msg = str_appendfmtv(msg, format, ap);
  va_end(ap);

  ctx->errh(src, pos, msg, ctx->userdata);
  str_free(msg);
}
