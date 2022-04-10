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

  const char* cwd = sys_cwd();

  // path starts with cwd?
  usize cwdlen = strlen(cwd);
  if (strlen(path) > cwdlen && path[cwdlen] == PATH_SEPARATOR &&
      shasprefixn(path, strlen(path), cwd, cwdlen))
  {
    path = &path[cwdlen + 1]; // e.g. "/foo/bar/baz" => "bar/baz"
  }

  return path;
}


bool path_dir(Str* dst, const char* filename, usize len) {
  // find last slash
  isize i = slastindexofn(filename, len, PATH_SEPARATOR);
  if (i == -1) // no directory part in filename
    return str_appendc(dst, '.');

  // remove trailing slashes
  len = strim_end(filename, (usize)i, PATH_SEPARATOR);
  if (len == 0) {
    #ifdef WIN32
      return str_appendcstr(dst, "C:\\");
    #else
      return str_appendc(dst, '/');
    #endif
  }

  return str_append(dst, filename, len);
}


usize path_dirlen(const char* filename, usize len) {
  isize i = slastindexofn(filename, len, PATH_SEPARATOR);
  return strim_end(filename, (usize)MAX(0,i), PATH_SEPARATOR);
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


bool path_append(Str* dst, const char* restrict path) {
  // trim trailing slashes from dst
  dst->len = strim_end(dst->v, dst->len, PATH_SEPARATOR);

  // trim leading slashes from path
  usize pathlen = strlen(path);
  const char* trimmed_path = strim_begin(path, pathlen, PATH_SEPARATOR);
  pathlen -= (usize)(uintptr)(trimmed_path - path);
  if (pathlen == 0) // path was empty or only consisted of PATH_SEPARATORs
    return true;

  bool ok = true;
  usize dstlen_orig = dst->len;

  // append separator
  if (dst->len)
    ok += str_appendc(dst, PATH_SEPARATOR);

  // append path
  ok += str_append(dst, trimmed_path, pathlen);

  // trim trailing slashes
  dst->len = strim_end(dst->v, dst->len, PATH_SEPARATOR);

  if UNLIKELY(!ok)
    dst->len = dstlen_orig; // undo
  return ok;
}


bool path_join(Str* dst, const char* restrict a, const char* restrict b) {
  // start with a and then append b
  bool ok = str_appendcstr(dst, a);
  return ok && path_append(dst, b);
}


bool path_abs(Str* dst, const char* restrict filename) {
  if (path_isabs(filename)) {
    str_appendcstr(dst, filename);
    return true;
  }
  return path_join(dst, sys_cwd(dst), filename);
}
