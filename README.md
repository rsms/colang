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

- Within a package, _unified source files_ should be used for major components.
  Smaller components might share a `mypackage/mypackage.c` file.


### Unified Source File

Unified Source File (USF) is a self-contained "single-header library" containing both
the API ("header") and its implementation. USF filenames use the file extension `.c`.
Since changes to the implementation of USF causes recompilation of anything using it,
use USFs for logical components.
For example `string.c` is a "string processing" component containing several different
types of string structs and functions.

USF pros & cons:
- Pro: Makes code portable within the project: just move the `mycomponent.c` file.
  With traditional lib.h,part1.c,partN.c arrangement the effort is much greater.
- Pro: Improves readability of file listings; less clutter/repetition and shorter list.
- Con: Changes to implementation causes all "includers" to require recompilation.
  Because of this, the "package API header" is important.

General outline of a Unified Source File `mycomponent.c`:

```c
// MYCOMPONENT DESCRIPTION
//
// SPDX-License-Identifier: LICENSE IDENTIFIER
// LICENSE COPYRIGHT NOTICE
//
#pragma once
#ifndef CO_IMPL
  #include "coimpl.h"
  #define MYCOMPONENT_IMPLEMENTATION
#endif
// ANY INCLUDES REQUIRED FOR API GOES HERE
BEGIN_INTERFACE
//———————————————————————————————————————————————————————————————————————————————————————

// API GOES HERE
error mycomp_dothing(i32 frob);

//———————————————————————————————————————————————————————————————————————————————————————
END_INTERFACE
#ifdef MYCOMPONENT_IMPLEMENTATION
// ANY INCLUDES REQUIRED FOR IMPLEMENTATION GOES HERE

// IMPLEMENTATION GOES HERE
error mycomp_dothing(i32 frob) {
  if (frob == 0)
    return err_invalid;
  return 0;
}

#endif // MYCOMPONENT_IMPLEMENTATION
```
