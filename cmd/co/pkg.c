#include "coimpl.h"
#include "coparse.h"

void pkg_add_source(Pkg* pkg, Source* src) {
  if (pkg->srclist)
    src->next = pkg->srclist;
  pkg->srclist = src;
}

error pkg_add_file(Pkg* pkg, Mem mem, const char* filename) {
  Source* src = memalloct(mem, Source);
  error err = source_open_file(src, mem, filename);
  if (err) {
    memfree(mem, src);
    return err;
  }
  pkg_add_source(pkg, src);
  return 0;
}

error pkg_add_dir(Pkg* pkg, Mem mem, const char* filename) {
  fs_dir d;
  error err = fs_dir_open(filename, &d);
  if (err)
    return err;

  fs_dirent e;
  error err1;
  while ((err = fs_dir_read(d, &e)) > 0) {
    switch (e.type) {
      case FS_DIRENT_REG:
      case FS_DIRENT_LNK:
      case FS_DIRENT_UNKNOWN:
        if (e.namlen > 3 && e.name[0] != '.' && strcmp(&e.name[e.namlen-2], ".co") == 0) {
          if ((err = pkg_add_file(pkg, mem, e.name)))
            goto end;
        }
        break;
      default:
        break;
    }
  }
end:
  err1 = fs_dir_close(d);
  return err < 0 ? err : err1;
}
