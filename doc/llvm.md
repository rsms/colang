# llvm notes

To see which features and CPUs that LLVM knows about, we can use llc. For example x86:

    deps/llvm/bin/llvm-as < /dev/null | deps/llvm/bin/llc -march=x86 -mattr=help

