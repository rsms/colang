# co2

## Building

Initial setup: `./init.sh` will install the following into `deps/`:
- [ckit](https://github.com/rsms/ckit) build tool and rbase library
- [ckit-jemalloc](https://github.com/rsms/ckit-jemalloc) memory allocator
- [zlib](https://zlib.net) static library
- [llvm+clang](https://llvm.org) tools and static libraries

Build:
- Optimized with assertions: `ckit build -safe`
- Optimized without assertions: `ckit build -fast`
- Unoptimized with all checks enabled: `ckit build`
- RT test program: _Not yet supported with the new cmake setup_
- Verbose build: `ckit build -v`

Development:
- Live main program: `ckit watch -rsh="{BUILD}/co build example/hello.w" co`
- Live testing: `ckit watch test`
- Live testing of a specific test: `ckit watch test scan`

Note: debug builds have the following checks and features enabled:
- All assertions (both "safe" and "debug")
- [Clang address sanitizer](https://clang.llvm.org/docs/AddressSanitizer.html)
- [Clang undefined-behavior sanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
- `R_TEST` unit tests (main executable runs all tests on start)


## Old makefile-based build (defunct)

Build:
- Release bin/co: `make -j$(nproc)` → `bin/co`
- Debug: `make DEBUG=1 -j$(nproc)` → `bin/co-debug`
- With llvm: `make DEBUG=1 LLVM=1 -j$(nproc)` → `bin/co-debug-llvm`
- Runtime test program: `make DEBUG=1 -j$(nproc) rt-test && ./bin/rt-test-debug`
- Verbose make: `make V=1`

Development:
- Live dev: `./misc/dev.sh -run -asan`
- Live dev with asan: (requires clang) `CC=deps/llvm/bin/clang ./misc/dev.sh -run -asan`
- With llvm: `./misc/dev.sh -run -with-llvm`
- Specific test: (prefix) `R_UNIT_TEST=foo ./misc/dev.sh -run`
