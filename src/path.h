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

// path_base returns a pointer to the last path element. E.g. "foo/bar/baz.x" => "baz.x"
// If the path is empty, returns "".
// If the path consists entirely of slashes, returns "/".
const char* path_base(const char* path);


END_INTERFACE
