#include <rbase/rbase.h>
#include "pkg.h"
#include "source.h"

bool PkgAddSource(Pkg* pkg, const char* filename) {
  auto src = memalloct(pkg->mem, Source);
  if (!SourceOpen(pkg, src, filename)) {
    memfree(pkg->mem, src);
    errlog("failed to open %s", filename);
    return false;
  }
  // add source to package's list of source files
  if (pkg->srclist)
    src->next = pkg->srclist;
  pkg->srclist = src;
  return true;
}

bool PkgScanSources(Pkg* pkg) {
  assert(pkg->srclist == NULL);
  DIR* dirp = opendir(pkg->dir);
  if (!dirp)
    return -1;

  DirEntry e;
  int readdir_status;
  bool ok = true;
  while ((readdir_status = fs_readdir(dirp, &e)) > 0) {
    switch (e.d_type) {
      case DT_REG:
      case DT_LNK:
      case DT_UNKNOWN:
        if (e.d_namlen > 3 && e.d_name[0] != '.' && strcmp(&e.d_name[e.d_namlen-2], ".c") == 0)
          ok = PkgAddSource(pkg, e.d_name) && ok;
        break;
      default:
        break;
    }
  }

  return closedir(dirp) == 0 && ok && readdir_status == 0;
}
