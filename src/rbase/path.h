#pragma once
ASSUME_NONNULL_BEGIN

#if WIN32
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

// path_isabs returns true if filename is an absolute path
bool path_isabs(const char* filename);

// path_join returns a new string of path1 + PATH_SEPARATOR + path2
Str path_join(const char* path1, const char* path2);

// path_dir returns the directory part of filename (i.e. "foo/bar/baz" => "foo/bar")
Str path_dir(const char* filename);
char* path_dir_mut(char* filename);

ASSUME_NONNULL_END
