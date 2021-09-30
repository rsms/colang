#include "common.h"
#include "build.h"
#include "util/tstyle.h"

#include <sys/stat.h>
#include <sys/mman.h>

static void SourceInit(const Pkg* pkg, Source* src, const char* filename) {
  memset(src, 0, sizeof(Source));
  auto filenamelen = strlen(filename);
  if (filenamelen == 0)
    panic("empty filename");
  if (!strchr(filename, PATH_SEPARATOR)) { // foo.c -> pkgdir/foo.c
    src->filename = path_join(pkg->dir, filename);
  } else {
    src->filename = str_cpy(filename, filenamelen);
  }
  src->pkg = pkg;
}

bool SourceOpen(Source* src, const Pkg* pkg, const char* filename) {
  SourceInit(pkg, src, filename);

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

void SourceInitMem(Source* src, const Pkg* pkg, const char* filename, const char* text, size_t len) {
  SourceInit(pkg, src, filename);
  src->fd = -1;
  src->body = (const u8*)text;
  src->len = len;
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
  if (src->fd > -1) {
    ok = close(src->fd) != 0 && ok;
    src->fd = -1;
  }
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

// -----------------------------------------------------------------------------------------------
// Pkg

void PkgAddSource(Pkg* pkg, Source* src) {
  if (pkg->srclist)
    src->next = pkg->srclist;
  pkg->srclist = src;
}

bool PkgAddFileSource(Pkg* pkg, const char* filename) {
  auto src = memalloct(pkg->mem, Source);
  if (!SourceOpen(src, pkg, filename)) {
    memfree(pkg->mem, src);
    errlog("failed to open %s", filename);
    return false;
  }
  PkgAddSource(pkg, src);
  return true;
}

bool PkgScanSources(Pkg* pkg) {
  assert(pkg->srclist == NULL);
  DIR* dirp = opendir(pkg->dir);
  if (!dirp)
    return false;

  DirEntry e;
  int readdir_status;
  bool ok = true;
  while ((readdir_status = fs_readdir(dirp, &e)) > 0) {
    switch (e.d_type) {
      case DT_REG:
      case DT_LNK:
      case DT_UNKNOWN:
        if (e.d_namlen > 3 && e.d_name[0] != '.' &&
            strcmp(&e.d_name[e.d_namlen-2], ".co") == 0)
        {
          ok = PkgAddFileSource(pkg, e.d_name) && ok;
        }
        break;
      default:
        break;
    }
  }

  return closedir(dirp) == 0 && ok && readdir_status == 0;
}
