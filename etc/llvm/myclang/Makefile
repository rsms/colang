DEPSDIR     := ../../../deps
LLVM_PREFIX := $(DEPSDIR)/llvm
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

COMPILE_FLAGS := -Oz -g \
	-I$(LLVM_PREFIX)/include \
	-I$(DEPSDIR)/zlib/include \
	-I$(DEPSDIR)/openssl/include \
	-I$(DEPSDIR)/xc/include \
	-I$(DEPSDIR)/libxml2/include \
	-I$(DEPSDIR)/xar/include \

LDFLAGS := -flto -Wl,-no_pie \
	$(shell "$(LLVM_CONFIG)" --ldflags) \
	-L$(DEPSDIR)/zlib/lib \
	-L$(DEPSDIR)/openssl/lib \
	-L$(DEPSDIR)/xc/lib \
	-L$(DEPSDIR)/libxml2/lib \
	-L$(DEPSDIR)/xar/lib \
	\
	$(shell "$(LLVM_CONFIG)" --system-libs) \
	-lxar -lcrypto -lz -llzma -lxml2

CFLAGS   := $(COMPILE_FLAGS) -std=c17
CXXFLAGS := $(COMPILE_FLAGS) -stdlib=libc++ -std=c++14 -fno-exceptions -fno-rtti \
	-nostdinc++ -I$(LLVM_PREFIX)/include/c++/v1

CSRCS   := $(wildcard *.c)
CXXSRCS := $(wildcard *.cc)

OBJDIR  := .obj
COBJS   := $(patsubst %,$(OBJDIR)/%.o,$(CSRCS))
CXXOBJS := $(patsubst %,$(OBJDIR)/%.o,$(CXXSRCS))
OBJS    := $(COBJS) $(CXXOBJS)

myclang: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ \
		$^ \
		"$(LLVM_PREFIX)"/lib/libc++.a \
		"$(LLVM_PREFIX)"/lib/libc++abi.a \
		"$(LLVM_PREFIX)"/lib/libclang*.a \
		"$(LLVM_PREFIX)"/lib/liblld*.a \
		$(shell "$(LLVM_CONFIG)" --libfiles)

myclang-stripped: myclang
	$(STRIP) -o $@ $<

myclang-stripped.xz: myclang-stripped
	xz -zc -T0 $^ > $@


test: myclang
	@# Note: "PATH=" to ensure myclang is not using any system programs like ld
	PATH= ./myclang cc -c -o test/test.o test/test.c
	PATH= ./myclang ld -arch $(shell uname -m) -platform_version macos 10.15 10.15 -lSystem.B \
		-o test/test test/test.o
	./test/test

	ln -sf myclang ld
	PATH= ./myclang cc \
		-fuse-ld="$(PWD)/ld" \
		-Wl,-platform_version,macos,10.15,10.15 \
		test/test.c -o test/test

	./test/test
	@echo dylibs used:
	@$(LS_SHLIBS) myclang
	@$(LS_SHLIBS) test/test


info:
	@echo "OBJDIR   $(OBJDIR)"
	@echo "COBJS    $(COBJS)"
	@echo "CXXOBJS  $(CXXOBJS)"
	@echo "CFLAGS   $(CFLAGS)"
	@echo "CXXFLAGS $(CXXFLAGS)"
	@echo "llvm system libs: $(shell "$(LLVM_CONFIG)" --system-libs)"

clean:
	rm -rf $(OBJDIR) myclang myclang-stripped


$(OBJDIR)/%.c.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/%.cc.o: %.cc
	$(CXXC) -static $(CXXFLAGS) -c -o $@ $<

$(OBJS): | $(OBJDIR)
$(OBJDIR):
	mkdir $(OBJDIR)


.PHONY: clean info test
