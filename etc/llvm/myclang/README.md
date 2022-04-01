myclang is an example program for bundling clang and lld into one executable
and is used to test the llvm build and Co use of clang, lld and llvm.

```sh
../build-llvm.sh
make -j$(nproc) test
```
