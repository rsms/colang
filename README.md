# Co programming language

## Building

Initial setup: `./init.sh` will install the following into `deps/`:
- [ckit](https://github.com/rsms/ckit) build tool and rbase library
- [ckit-jemalloc](https://github.com/rsms/ckit-jemalloc) memory allocator
- [zlib](https://zlib.net) static library
- [llvm+clang](https://llvm.org) tools and static libraries

Build:
- Unoptimized with all checks enabled: `ckit build co`
- Optimized with assertions:           `ckit build -safe co`
- Optimized without assertions:        `ckit build -fast co`
- RT test program:                     `ckit watch -r co-rt-test`
- Verbose build:                       `ckit build -v`
- Build everything:                    `ckit build`

Development and testing:
- Run all tests:            `./test.sh`
- Run all tests live:       `./test.sh -w`
- Run unit test with prefix "scan":      `ckit test scan`
- Run unit test with prefix "scan" live: `ckit watch test scan`
- Live main program:        `ckit watch -rsh="{BUILD}/co build example/hello.co" co`

Note: debug builds have the following checks and features enabled:
- All assertions (both "safe" and "debug")
- [Clang address sanitizer](https://clang.llvm.org/docs/AddressSanitizer.html)
- [Clang undefined-behavior sanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
- `R_TEST` unit tests (main executable runs all tests on start)

