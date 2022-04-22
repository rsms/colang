// file path functions
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

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

// path_dir writes the directory part of path to dst.
// e.g. "foo/bar/baz" => "foo/bar", "foo" => "."
// Returns a pointer to a null-terminated string of the result (into dst->v),
// or NULL if memory allocation failed.
char* nullable path_dir(Str* dst, const char* filename, usize len);

// path_diroffs returns the position of the end of the directory part of filename.
// E.g. "foo/bar/baz" => 7, "foo/" => 3, "foo" => 0.
// Returns 0 if filename does not contain a directory part.
usize path_dirlen(const char* filename, usize len);

// path_basex returns a pointer to the last path element. E.g. "foo/bar/baz.x" => "baz.x"
// If the path is empty, returns "".
// If the path consists entirely of slashes, returns "/".
const char* path_basex(const char* path);

// path_base appends the last path element to dst. E.g. "foo/bar/baz.x" => "baz.x"
// If the path is empty, returns "".
// If the path consists entirely of slashes, returns "/".
// Returns a pointer to a null-terminated string of the result (into dst->v),
// or NULL if memory allocation failed.
char* nullable path_base(Str* dst, const char* path, usize len);

// path_clean cleans up a pathname, removing superfluous slashes and path components.
// Returns a pointer to a null-terminated string of the result (into dst->v),
// or NULL if memory allocation failed.
char* nullable path_clean(Str* dst, const char* path, usize len);

// path_abs writes an absolute representation of path to dst via path_clean.
// If the path is not absolute it will be joined with the current working directory.
// e.g. "foo/bar" => "/current-working-directory/foo/bar", "/foo/bar" => "/foo/bar".
// Returns a pointer to a null-terminated string of the result (into dst->v),
// or NULL if memory allocation failed.
char* nullable path_abs(Str* dst, const char* filename);

// path_join concatenates path1 + PATH_SEPARATOR + path2 and appends it to dst.
// Returns a pointer to a null-terminated string of the result (into dst->v),
// or NULL if memory allocation failed.
char* nullable path_join(Str* dst, const char* restrict path1, const char* restrict path2);

// path_join appends PATH_SEPARATOR + path to dst.
// Any trailing PATH_SEPARATORs in dst are trimmed before appending path.
// Returns false if memory allocation failed.
bool path_append(Str* dst, const char* path);


END_INTERFACE
