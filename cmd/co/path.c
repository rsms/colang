#include "coimpl.h"
#include "path.h"
#include "sys.h"

bool path_isabs(const char* filename) {
  #ifdef WIN32
    #warning TODO path_isabs for windows
  #endif
  return filename != NULL && filename[0] == PATH_SEPARATOR;
}

const char* path_cwdrel(const char* path) {
  if (!path_isabs(path))
    return path;

  char cwd[512];
  if (sys_getcwd(cwd, sizeof(cwd)) != 0)
    return path;

  usize pathlen = strlen(path);
  usize cwdlen = strlen(cwd);

  // path starts with cwd?
  if (cwdlen != 0 && cwdlen < pathlen && memcmp(cwd, path, cwdlen) == 0)
    path = &path[cwdlen + 1]; // e.g. "/foo/bar/baz" => "bar/baz"

  return path;
}
