# Co programming language


## Building

First time setup:

```
./init.sh
```

Then build & run:

```
./build.sh -debug -w -run=build/debug/co
```

See `./build.sh -h` for more options.


## Source code

### Source organization

- All project sources live in the `src/` directory

- Each source directory represents one major logical package, which is interfaced
  by users via a single "API header" of the same name as the directory.
  For example `src/parse/` & `src/parse/parse.h` for the parser package.
  (Do _not_ include a subdirectory's non-"API header" file from another directory.
   I.e. _do not do this:_ `#include "parse/something.h"`.)

