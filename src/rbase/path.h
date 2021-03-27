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
char* path_join(Mem nullable mem, const char* path1, const char* path2);

// path_dir returns the directory part of filename (i.e. "foo/bar/baz" => "foo/bar")
char* path_dir(Mem nullable mem, const char* filename);
char* path_dir_mut(char* filename);
// #define path_dir(mem, filename) _Generic((filename), \
//   const char*:        path_dir_copy(mem, filename), \
//   char*:              path_dir_mut(filename) \
// )

ASSUME_NONNULL_END
