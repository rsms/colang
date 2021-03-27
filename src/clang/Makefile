LLVM_PREFIX := ../../deps/llvm
SYSTEM      := $(shell uname -s)

CC          := $(LLVM_PREFIX)/bin/clang
CXXC        := $(LLVM_PREFIX)/bin/clang++
STRIP       := $(LLVM_PREFIX)/bin/llvm-strip
LLVM_CONFIG := $(LLVM_PREFIX)/bin/llvm-config

ifeq ($(SYSTEM),Darwin)
  LS_SHLIBS := $(LLVM_PREFIX)/bin/llvm-objdump --macho --dylibs-used
else
  LS_SHLIBS := ldd  # fixme (note: only used by "test" target)
endif

_FLAGS   := -Oz -g -I$(LLVM_PREFIX)/include
CFLAGS   := $(_FLAGS) -std=c17
CXXFLAGS := $(_FLAGS) -stdlib=libc++ -nostdinc++ -std=c++14 -I../../lib/libcxx/include

CSRCS   := $(wildcard *.c)
CXXSRCS := $(wildcard *.cc)

OBJDIR  := .obj
COBJS   := $(patsubst %,$(OBJDIR)/%.o,$(CSRCS))
CXXOBJS := $(patsubst %,$(OBJDIR)/%.o,$(CXXSRCS))
OBJS    := $(COBJS) $(CXXOBJS)

myclang: $(OBJS)
	$(CC) -flto \
		-L"$(LLVM_PREFIX)"/lib -L../../deps/zlib/lib -Wl,-no_pie \
		-o $@ $^ \
		../../work/build/libc++.a \
		../../work/build/libc++abi.a \
		"$(LLVM_PREFIX)"/lib/libclang*.a \
		"$(LLVM_PREFIX)"/lib/liblld*.a \
		$(shell "$(LLVM_CONFIG)" --libfiles) \
		$(shell "$(LLVM_CONFIG)" --system-libs)

myclang-stripped: myclang
	$(STRIP) -o $@ $<

$(OBJDIR)/%.c.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/%.cc.o: %.cc
	$(CXXC) -static $(CXXFLAGS) -c -o $@ $<

$(OBJS): | $(OBJDIR)
$(OBJDIR):
	mkdir $(OBJDIR)

test: myclang
	@# Note: "PATH=" to ensure myclang is not using any system programs like ld
	PATH= ./myclang -o test/test test/main.c
	./test/test
	$(LS_SHLIBS) myclang
	$(LS_SHLIBS) test/test

info:
	@echo "OBJDIR   $(OBJDIR)"
	@echo "COBJS    $(COBJS)"
	@echo "CXXOBJS  $(CXXOBJS)"
	@echo "CFLAGS   $(CFLAGS)"
	@echo "CXXFLAGS $(CXXFLAGS)"

clean:
	rm -rf $(OBJDIR) myclang myclang-stripped

.PHONY: clean info test
