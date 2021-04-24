#include <rbase/rbase.h>
#include "../util/tstyle.h"
#include "source.h"

#include <sys/stat.h>
#include <sys/mman.h>

bool SourceOpen(Pkg* pkg, Source* src, const char* filename) {
  memset(src, 0, sizeof(Source));

  auto namelen = strlen(filename);
  if (namelen == 0)
    panic("empty filename");

  if (!strchr(filename, PATH_SEPARATOR)) { // foo.c -> pkgdir/foo.c
    src->filename = path_join(pkg->dir, filename);
  } else {
    src->filename = str_cpyn(filename, namelen+1);
  }

  src->pkg = pkg;
  src->fd = open(src->filename, O_RDONLY);
  if (src->fd < 0)
    return false;

  struct stat st;
  if (fstat(src->fd, &st) != 0) {
    auto _errno = errno;
    close(src->fd);
    errno = _errno;
    return false;
  }
  src->len = (size_t)st.st_size;

  return true;
}

bool SourceOpenBody(Source* src) {
  if (src->body == NULL) {
    src->body = mmap(0, src->len, PROT_READ, MAP_PRIVATE, src->fd, 0);
    if (!src->body)
      return false;
    src->ismmap = true;
  }
  return true;
}

bool SourceCloseBody(Source* src) {
  bool ok = true;
  if (src->body) {
    if (src->ismmap) {
      ok = munmap((void*)src->body, src->len) == 0;
      src->ismmap = false;
    }
    src->body = NULL;
  }
  return ok;
}

bool SourceClose(Source* src) {
  auto ok = SourceCloseBody(src);
  ok = close(src->fd) != 0 && ok;
  src->fd = -1;
  return ok;
}

void SourceDispose(Source* src) {
  str_free(src->filename);
  src->filename = NULL;
}

void SourceChecksum(Source* src) {
  SHA1Ctx sha1;
  sha1_init(&sha1);
  auto chunksize = mem_pagesize();
  auto remaining = src->len;
  auto inptr = src->body;
  while (remaining > 0) {
    auto z = MIN(chunksize, remaining);
    sha1_update(&sha1, inptr, z);
    inptr += z;
    remaining -= z;
  }
  sha1_final(src->sha1, &sha1);
}

// --------------------------------------------------------
// SrcPos

static void computeLineOffsets(Source* s) {
  assert(s->lineoffs == NULL);
  if (!s->body)
    SourceOpenBody(s);

  size_t cap = 256; // best guess for common line numbers, to allocate up-front
  s->lineoffs = (u32*)memalloc(NULL, sizeof(u32) * cap);
  s->lineoffs[0] = 0;

  u32 linecount = 1;
  u32 i = 0;
  while (i < s->len) {
    if (s->body[i++] == '\n') {
      if (linecount == cap) {
        // more lines
        cap = cap * 2;
        s->lineoffs = (u32*)memrealloc(NULL, s->lineoffs, sizeof(u32) * cap);
      }
      s->lineoffs[linecount] = i;
      linecount++;
    }
  }

  s->nlines = linecount;
}


LineCol SrcPosLineCol(SrcPos pos) {
  Source* s = pos.src;
  if (s == NULL) {
    // NoSrcPos
    LineCol lico = { 0, 0 };
    return lico;
  }

  if (!s->lineoffs)
    computeLineOffsets(s);

  if (pos.offs >= s->len) { dlog("pos.offs=%u >= s->len=%zu", pos.offs, s->len); }
  assert(pos.offs < s->len);

  u32 count = s->nlines;
  u32 line = 0;
  u32 debug1 = 10;
  while (count > 0 && debug1--) {
    u32 step = count / 2;
    u32 i = line + step;
    if (s->lineoffs[i] <= pos.offs) {
      line = i + 1;
      count = count - step - 1;
    } else {
      count = step;
    }
  }
  LineCol lico = { line - 1, line > 0 ? pos.offs - s->lineoffs[line - 1] : pos.offs };
  return lico;
}


Str SrcPosFmt(SrcPos pos) {
  const char* filename = "<input>";
  size_t filenameLen = strlen("<input>");
  if (pos.src) {
    filename = pos.src->filename;
    filenameLen = strlen(filename);
  }
  auto l = SrcPosLineCol(pos);
  return str_fmt("%s:%u:%u", filename, l.line + 1, l.col + 1);
}


static const u8* lineContents(Source* s, u32 line, u32* out_len) {
  if (!s->lineoffs)
    computeLineOffsets(s);

  if (line >= s->nlines)
    return NULL;

  auto start = s->lineoffs[line];
  const u8* lineptr = s->body + start;
  if (out_len) {
    if (line + 1 < s->nlines) {
      *out_len = (s->lineoffs[line + 1] - 1) - start;
    } else {
      *out_len = (s->body + s->len) - lineptr;
    }
  }
  return lineptr;
}


Str SrcPosMsg(SrcPos pos, const char* message) {
  auto l = SrcPosLineCol(pos);

  TStyleTable style = TStyle16;

  Str s = str_fmt("%s%s:%u:%u: %s%s\n",
    style[TStyle_bold],
    pos.src ? pos.src->filename : "<input>",
    l.line + 1,
    l.col + 1,
    message ? message : "",
    style[TStyle_none]
  );

  // include line contents
  if (pos.src) {
    u32 linelen;
    auto lineptr = lineContents(pos.src, l.line, &linelen);
    if (lineptr)
      s = str_appendn(s, (const char*)lineptr, linelen);
    s = str_appendc(s, '\n');

    // draw a squiggle (or caret when span is unknown) decorating the interesting range
    if (l.col > 0)
      s = str_appendfill(s, str_len(s) + l.col, ' '); // indentation
    if (pos.span > 0) {
      s = str_appendfill(s, str_len(s) + pos.span + 1, '~'); // squiggle
      s[str_len(s) - 1] = '\n';
    } else {
      s = str_appendcstr(s, "^\n");
    }
  }

  return s;
}
