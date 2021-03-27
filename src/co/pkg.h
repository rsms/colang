#pragma once
ASSUME_NONNULL_BEGIN

typedef struct Source Source;

// Pkg represents a package; a directory of source files
typedef struct Pkg {
  Mem         mem;     // memory for resources only needed by this package
  const char* dir;     // directory filename
  Source*     srclist; // linked list of sources
} Pkg;

bool PkgAddSource(Pkg* pkg, const char* filename);
bool PkgScanSources(Pkg* pkg);


ASSUME_NONNULL_END
