#include "rbase.h"
#include <libgen.h> // dirname

// path_isabs returns true if filename is an absolute path
bool path_isabs(const char* filename) {
  // TODO windows
  size_t z = strlen(filename);
  return z == 0 || filename[0] == PATH_SEPARATOR;
}

// strjoinc joins s1 & s2 using glue, allocating memory in mem
static char* strjoinc(Mem mem, const char* s1, const char* s2, char glue) {
  auto z1 = strlen(s1);
  auto z2 = strlen(s2);
  auto joined = (char*)memdup2(mem, s1, z1, z2 + 2);
  joined[z1] = glue;
  z1++;
  memcpy(&joined[z1], s2, z2);
  joined[z1+z2] = '\0';
  return joined;
}

char* path_join(Mem mem, const char* path1, const char* path2) {
  return strjoinc(mem, path1, path2, PATH_SEPARATOR);
}

char* path_dir(Mem nullable mem, const char* filename) {
  auto p = strrchr(filename, PATH_SEPARATOR);
  size_t z = 1;
  if (p)
    z = (size_t)p - (size_t)filename;
  else
    filename = ".";
  return (char*)memdup(mem, filename, z);
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
