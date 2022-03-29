// file path functions
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
#ifndef CO_IMPL
  #include "coimpl.h"
  #define PATH_IMPLEMENTATION
#endif
BEGIN_INTERFACE
//———————————————————————————————————————————————————————————————————————————————————————

#ifdef WIN32
  #define PATH_SEPARATOR     '\\'
  #define PATH_SEPARATOR_STR "\\"
  #define PATH_DELIMITER     ';'
  #define PATH_DELIMITER_STR ";"
#else
  #define PATH_SEPARATOR     '/'
  #define PATH_SEPARATOR_STR "/"
  #define PATH_DELIMITER     ':'
  #define PATH_DELIMITER_STR ":"
#endif

// path_cwdrel returns path relative to the current working directory,
// or path verbatim if path is outside the working directory.
const char* path_cwdrel(const char* path);

// path_isabs returns true if path is an absolute path
bool path_isabs(const char* path);

// path_base returns a pointer to the last path element. E.g. "foo/bar/baz.x" => "baz.x"
// If the path is empty, returns "".
// If the path consists entirely of slashes, returns "/".
const char* path_base(const char* path);

//———————————————————————————————————————————————————————————————————————————————————————
END_INTERFACE
#ifdef PATH_IMPLEMENTATION

#include "sys.c" // sys_getcwd

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

#endif // PATH_IMPLEMENTATION
