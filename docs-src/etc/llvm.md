# llvm notes

To see which features and CPUs that LLVM knows about, we can use llc. For example x86:

    deps/llvm/bin/llvm-as < /dev/null | deps/llvm/bin/llc -march=x86 -mattr=help

Look into using "memory lifetime markers"

<https://llvm.org/docs/LangRef.html#memory-use-markers>

    declare void @llvm.lifetime.start(i64 <size>, i8* nocapture <ptr>)

Various useful stuff

- [LLVM IR language ref](https://llvm.org/docs/LangRef.html)
- [Orc v2 JIT examples](https://github.com/llvm/llvm-project/tree/llvmorg-13.0.0/llvm/examples/OrcV2Examples)
- [Orc v2 overview](https://llvm.org/docs/ORCv2.html)
- [JIT tutorial](https://llvm.org/docs/tutorial/BuildingAJIT2.html)
