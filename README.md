# co2

Build:
- Release bin/co: `make -j$(nproc)` → `bin/co`
- Debug: `make DEBUG=1 -j$(nproc)` → `bin/co-debug`
- With llvm: `make DEBUG=1 LLVM=1 -j$(nproc)` → `bin/co-debug-llvm`
- Runtime test program: `make DEBUG=1 -j$(nproc) rt-test && ./bin/rt-test-debug`
- Verbose make: `make V=1`

Development:
- Live dev: `./misc/dev.sh -run`
- With llvm: `./misc/dev.sh -run -with-llvm`
- Specific test: (prefix) `R_UNIT_TEST=foo ./misc/dev.sh -run`
