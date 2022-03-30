// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "colib.h"


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


const char* path_base(const char* path) {
  if (path[0] == 0)
    return path;
  usize len = strlen(path);
  const char* p = &path[len];
  for (; p != path && *(p-1) != PATH_SEPARATOR; p--) {
  }
  return p;
}
