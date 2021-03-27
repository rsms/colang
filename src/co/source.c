#include <rbase/rbase.h>
#include "source.h"
#include "pkg.h"

#include <sys/stat.h>
#include <sys/mman.h>

bool SourceOpen(Pkg* pkg, Source* src, const char* filename) {
  memset(src, 0, sizeof(Source));

  auto namelen = strlen(filename);
  if (namelen == 0)
    panic("SourceOpen: empty filename");

  if (!strchr(filename, PATH_SEPARATOR)) { // foo.c -> pkgdir/foo.c
    src->filename = path_join(pkg->mem, pkg->dir, filename);
  } else {
    src->filename = (char*)memdup(pkg->mem, filename, namelen+1);
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
  assert(src->body == NULL);
  src->body = mmap(0, src->len, PROT_READ, MAP_PRIVATE, src->fd, 0);
  if (!src->body)
    return false;
  src->ismmap = true;
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
  memfree(src->pkg, src->filename);
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
