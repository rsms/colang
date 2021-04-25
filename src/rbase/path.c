#include "rbase.h"
#include <libgen.h> // dirname

// path_isabs returns true if filename is an absolute path
bool path_isabs(const char* filename) {
  // TODO windows
  size_t z = strlen(filename);
  return z == 0 || filename[0] == PATH_SEPARATOR;
}

Str path_join(const char* path1, const char* path2) {
  size_t len1 = strlen(path1);
  size_t len2 = strlen(path2);
  auto s = str_new(len1 + len2 + 1);
  s = str_append(s, path1, len1);
  s = str_appendc(s, PATH_SEPARATOR);
  s = str_append(s, path2, len2);
  return s;
}

Str path_dir(const char* filename) {
  auto p = strrchr(filename, PATH_SEPARATOR);
  if (p == NULL)
    return str_cpycstr(".");
  return str_cpyn(filename, (u32)((uintptr_t)p - (uintptr_t)filename));
}

char* path_dir_mut(char* filename) {
  return dirname(filename);
}

// // path_abs returns an absolute representation of path. If the path is not absolute it
// // will be joined with the current working directory to turn it into an absolute path.
// char* path_abs(Mem nullable mem, const char* filename);
//
// char* path_abs(Mem nullable mem, const char* filename) {
//   if (path_isabs(filename))
//     return memstrdup(mem, filename);
//   -- WIP --
// }
