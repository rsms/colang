// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "colib.h"


#define IS_SEP(c) ((c) == PATH_SEPARATOR)


char* nullable path_clean(Str* dst, const char* path, usize len) {
  if UNLIKELY(!str_reserve(dst, len + 2)) // +2 for empty case, and \0
    return NULL;

  u32 dst_offs = dst->len;
  u32 r = 0;        // read offset
  u32 w = dst_offs; // write offset
  u32 dotdot = dst_offs; // w offset of most recent ".."

  #define DST_APPEND(c) ( assert(w < dst->cap), dst->v[w++] = c )

  if (len == 0) {
    DST_APPEND('.');
    goto end;
  }

  bool rooted = IS_SEP(path[0]);
  if (rooted) {
    DST_APPEND(PATH_SEPARATOR);
    r = 1;
    dotdot++;
  }

  while (r < len) {
    if (IS_SEP(path[r]) || (path[r] == '.' && (r+1 == len || IS_SEP(path[r+1])))) {
      // empty or "."
      r++;
    } else if (path[r] == '.' && path[r+1] == '.' && (r+2 == len || IS_SEP(path[r+2]))) {
      // ".."
      r += 2;
      if (w > dotdot) {
        // can backtrack
        w--;
        while (w > dotdot && !IS_SEP(dst->v[w]))
          w--;
      } else if (!rooted) {
        // cannot backtrack, but not rooted, so append ".."
        if (w > dst_offs)
          DST_APPEND(PATH_SEPARATOR);
        DST_APPEND('.');
        DST_APPEND('.');
        dotdot = w;
      }
    } else {
      // actual path component; add slash if needed
      if ((rooted && w != dst_offs+1) || (!rooted && w != dst_offs))
        DST_APPEND(PATH_SEPARATOR);
      // copy
      for (; r < len && !IS_SEP(path[r]); r++)
        DST_APPEND(path[r]);
    }
  }

  if (w == dst_offs) // "" => "."
    DST_APPEND('.');

end:
  dst->len = w;
  assert(w+1 < dst->cap);
  dst->v[w+1] = 0;
  return dst->v + dst_offs;
}


bool path_isabs(const char* filename) {
  #ifdef WIN32
    #warning TODO path_isabs for windows
  #endif
  return filename != NULL && IS_SEP(filename[0]);
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


char* nullable path_dir(Str* dst, const char* filename, usize len) {
  u32 dst_offs = dst->len;
  // find last slash
  isize i = slastindexofn(filename, len, PATH_SEPARATOR);
  if (i == -1) { // no directory part in filename
    str_appendc(dst, '.');
  } else {
    // remove trailing slashes
    len = strim_end(filename, (usize)i, PATH_SEPARATOR);
    if (len) {
      str_append(dst, filename, len);
    } else {
      #ifdef WIN32
        str_appendcstr(dst, "C:\\");
      #else
        str_appendc(dst, '/');
      #endif
    }
  }
  return str_cstr(dst) ? dst->v + dst_offs : NULL;
}


usize path_dirlen(const char* filename, usize len) {
  isize i = slastindexofn(filename, len, PATH_SEPARATOR);
  return strim_end(filename, (usize)MAX(0,i), PATH_SEPARATOR);
}


const char* path_basex(const char* path) {
  if (path[0] == 0)
    return path;
  usize len = strlen(path);
  const char* p = &path[len];
  for (; p != path && *(p-1) != PATH_SEPARATOR; p--) {
  }
  return p;
}


char* nullable path_base(Str* dst, const char* path, usize len) {
  // trim trailing slashes
  u32 dst_offs = dst->len;
  usize z = strim_end(path, len, PATH_SEPARATOR);
  if (z == 0) {
    str_appendc(dst, len ? PATH_SEPARATOR : '.');
  } else {
    // find last slash
    isize p = slastindexofn(path, z, PATH_SEPARATOR) + 1; // -1 if not found => 0
    str_append(dst, path + p, z - (usize)p);
  }
  return str_cstr(dst) ? dst->v + dst_offs : NULL;
}


bool path_append(Str* dst, const char* restrict path) {
  // trim trailing slashes from dst
  usize len1 = dst->len;
  dst->len = strim_end(dst->v, dst->len, PATH_SEPARATOR);

  // trim leading slashes from path
  usize pathlen = strlen(path);
  const char* trimmed_path = strim_begin(path, pathlen, PATH_SEPARATOR);
  pathlen -= (usize)(uintptr)(trimmed_path - path);
  if (pathlen == 0) {
    // path was empty or only consisted of PATH_SEPARATORs
    if (dst->len == 0 && len1)
      dst->len++; // undo trimming slash
    return true;
  }

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


char* nullable path_join(Str* dst, const char* restrict a, const char* restrict b) {
  u32 a_len = (u32)strlen(a);
  u32 b_len = (u32)strlen(b);
  assertf(dst->v + dst->cap < a || dst->v > a + a_len, "path1 inside dst");
  assertf(dst->v + dst->cap < b || dst->v > b + b_len, "path2 inside dst");

  if (a_len + b_len == 0)
    return str_cstr(dst) + dst->len;

  const char* input = a;
  u32 inlen = a_len;

  if (a_len == 0) {
    input = b;
    inlen = b_len;
  } else if (a_len) {
    // reserve space for final result produced by path_clean
    u32 reserved_space = a_len + b_len + 2;
    if UNLIKELY(!str_reserve(dst, reserved_space*2))
      return NULL;
    // put input "a/b" in dst->v, after reserved space
    char* tmp = dst->v + dst->len + reserved_space;
    memcpy(tmp, a, a_len);
    tmp[a_len] = PATH_SEPARATOR;
    memcpy(tmp + a_len + 1, b, b_len);
    input = tmp;
    inlen = a_len + 1 + b_len;
  }

  return path_clean(dst, input, inlen);
}


char* nullable path_abs(Str* dst, const char* restrict path) {
  if (path_isabs(path))
    return path_clean(dst, path, strlen(path));
  return path_join(dst, sys_cwd(), path);
}
