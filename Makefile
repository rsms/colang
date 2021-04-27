# make V=1 or make VERBOSE=1 to print invocations
Q = $(if $(filter 1,$(V) $(VERBOSE)),,@)

SYSTEM  := $(shell uname -s)
ARCH    := $(shell uname -m)
SRCROOT := $(shell pwd)

RBASE_SRC   := $(wildcard src/rbase/*.c)
RT_SRC      := $(wildcard src/rt/*.c src/rt/exectx/*.c)
RT_TEST_SRC := $(wildcard src/rt-test/*.c)
CO_SRC      := $(wildcard src/co/*.c src/co/*/*.c src/co/llvm/*.cc)

# for both C and C++
COMPILE_FLAGS := \
	-g \
	-MMD \
	-Isrc \
	-ffile-prefix-map=$(SRCROOT)/= \
	-fstrict-aliasing \
	-Wall -Wextra -Wimplicit-fallthrough \
	-Wno-missing-field-initializers -Wno-unused-parameter \
	-Wunused

CFLAGS := \
	-std=c17 \
	$(MORECFLAGS)

CXXFLAGS := \
	-std=c++14 \
	-fvisibility-inlines-hidden \
	-fno-exceptions \
	-fno-rtti \
	-stdlib=libc++ -nostdinc++ -Ilib/libcxx/include

LDFLAGS := $(MORELDFLAGS)


# llvm
LLVM_PREFIX := deps/llvm
# CC          ?= $(LLVM_PREFIX)/bin/clang
# CXX         ?= $(LLVM_PREFIX)/bin/clang++
# AR          ?= $(LLVM_PREFIX)/bin/llvm-ar
# STRIP       ?= $(LLVM_PREFIX)/bin/llvm-strip
LLVM_CONFIG := $(LLVM_PREFIX)/bin/llvm-config
# LLVM components (libraries) to include. See deps/llvm/bin/llvm-config --components
LLVM_COMPONENTS := \
	engine \
	option \
	passes \
	x86codegen \
	aarch64codegen \
	webassemblycodegen
CXXFLAGS += -stdlib=libc++ -nostdinc++ -Ilib/libcxx/include $(shell "$(LLVM_CONFIG)" --cxxflags)
CFLAGS   += $(shell "$(LLVM_CONFIG)" --cflags)
LDFLAGS += \
	-Wl,-no_pie \
	$(shell "$(LLVM_CONFIG)" --ldflags) \
	work/build/libc++.a \
	work/build/libc++abi.a \
	$(shell "$(LLVM_CONFIG)" --system-libs) \
	$(shell "$(LLVM_CONFIG)" --link-static --libs $(LLVM_COMPONENTS)) \
	$(LLVM_PREFIX)/lib/liblld*.a


FLAVOR := release
ifneq ($(DEBUG),)
	FLAVOR := debug
	COMPILE_FLAGS += -DDEBUG
else
	COMPILE_FLAGS += -O3
	LDFLAGS += -dead_strip
	ifeq ($(notdir $(CC)),clang)
		LDFLAGS += -flto
	endif
endif

# enable sanitizer
# make SANITIZE=address
# make SANITIZE=undefined
ifneq ($(SANITIZE),)
	CC  := clang
	CXX := clang++
	ifeq ($(SANITIZE),address)
		FLAVOR := $(FLAVOR)-asan
		COMPILE_FLAGS += \
			-DASAN_ENABLED=1 \
		  -fsanitize=address \
		  -fsanitize-address-use-after-scope \
		  -fno-omit-frame-pointer \
		  -fno-optimize-sibling-calls
		LDFLAGS += -fsanitize=address
		# out local build of clang doesn't include asan libs, so use system compiler
	else ifeq ($(SANITIZE),undefined)
		FLAVOR := $(FLAVOR)-usan
		COMPILE_FLAGS += -g -fsanitize=undefined -fno-sanitize-recover=all
	  LDFLAGS += -fsanitize=undefined
	else ifeq ($(SANITIZE),memory)
		FLAVOR := $(FLAVOR)-msan
		COMPILE_FLAGS += -g -fsanitize=memory -fno-omit-frame-pointer
	  LDFLAGS += -fsanitize=memory
	else
		ERROR
	endif
endif

BUILDDIR := .build/$(FLAVOR)

ifeq ($(SYSTEM),Darwin)
	ifeq ($(CC),cc)
		CC  := clang
		CXX := clang++
	endif
	ifeq ($(ARCH),x86_64)
		RT_SRC += src/rt/exectx/exectx_x86_64_sysv.S
	else
		RT_SRC += UNSUPPORTED_PLATFORM
	endif
	COMPILE_FLAGS += \
		-mmacosx-version-min=10.15 \
	  -fcolor-diagnostics
  LDFLAGS += \
  	-mmacosx-version-min=10.15 \
  	-isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
	ifneq ($(DEBUG),)
		# force_load is required for unit tests to be included (force_load is clang-specific)
		LDFLAGS += -Wl,-force_load,$(BUILDDIR)/rbase.a
	endif
else ifeq ($(SYSTEM),Linux)
	ifeq ($(ARCH),x86_64)
		RT_SRC += src/rt/exectx/exectx_x86_64_sysv.S
	else ifeq ($(ARCH),aarch64)
		RT_SRC += src/rt/exectx/exectx_arm64_aapcs_elf.S
	else
		RT_SRC += UNSUPPORTED_PLATFORM
	endif
	ifneq ($(DEBUG),)
		# whole-archive is required for unit tests to be included (whole-archive is GCC-specific)
		LDFLAGS += -Wl,--whole-archive $(BUILDDIR)/rbase.a -Wl,--no-whole-archive
	endif
endif

# clang-specific options (TODO: fix makefile check to look for *"/clang" || "clang")
ifeq ($(notdir $(CC)),clang)
	COMPILE_FLAGS += \
	  -Wno-nullability-completeness \
	  -Wno-nullability-inferred-on-nested-type
endif

OBJDIR       := $(BUILDDIR)
CO_OBJS      := $(patsubst %,$(OBJDIR)/%.o,$(CO_SRC))
RT_OBJS      := $(patsubst %,$(OBJDIR)/%.o,$(RT_SRC))
RT_TEST_OBJS := $(patsubst %,$(OBJDIR)/%.o,$(RT_TEST_SRC))
RBASE_OBJS   := $(patsubst %,$(OBJDIR)/%.o,$(RBASE_SRC))

PCHDIR       := $(BUILDDIR)/pch
RBASE_H      := src/rbase/rbase.h
LLVM_H       := src/co/llvm/llvm.hh
RBASE_PCH    := $(PCHDIR)/$(RBASE_H).pch
LLVM_PCH     := $(PCHDIR)/$(LLVM_H).pch

all: bin/co bin/rt-test

.PHONY: test_unit
test_unit:
	$(MAKE) DEBUG=1 V=$(V) -j$(shell nproc) bin/co
	R_UNIT_TEST=1 ./bin/co test

.PHONY: test_usan
test_usan:
	$(MAKE) SANITIZE=undefined DEBUG=1 V=$(V) -j$(shell nproc) bin/co
	R_UNIT_TEST=1 ./bin/co build example/hello.w

.PHONY: test_asan
test_asan:
	$(MAKE) SANITIZE=address DEBUG=1 V=$(V) -j$(shell nproc) bin/co
	R_UNIT_TEST=1 ./bin/co build example/hello.w

.PHONY: test_msan
test_msan:
	$(MAKE) SANITIZE=memory DEBUG=1 V=$(V) -j$(shell nproc) bin/co
	R_UNIT_TEST=1 ./bin/co build example/hello.w

bin/co: $(CO_OBJS) $(BUILDDIR)/rbase.a
	@echo "link $@"
	@mkdir -p "$(dir $@)"
	$(Q)$(CC) $(LDFLAGS) -o $@ $^

bin/rt-test: $(RT_TEST_OBJS) $(BUILDDIR)/rbase.a $(BUILDDIR)/rt.a
	@echo "link $@"
	@mkdir -p "$(dir $@)"
	$(Q)$(CC) $(LDFLAGS) -o $@ $^

.PHONY: x
x: $(BUILDDIR)/src/co/llvm/llvm.cc.o
	true

$(BUILDDIR)/rt.a: $(RT_OBJS)
	@echo "ar $@ ($(foreach fn,$^,$(notdir ${fn:.o=})))"
	$(Q)$(AR) rcs $@ $^

$(BUILDDIR)/rbase.a: $(RBASE_OBJS)
	@echo "ar $@ ($(foreach fn,$^,$(notdir ${fn:.o=})))"
	$(Q)$(AR) rcs $@ $^

$(RBASE_PCH): $(RBASE_H)
	@echo "cc $< -> $@"
	@mkdir -p "$(dir $@)"
	$(Q)$(CC) $(COMPILE_FLAGS) $(CFLAGS) -c -o "$@" $<

$(LLVM_PCH): $(LLVM_H)
	@echo "cc $< -> $@"
	@mkdir -p "$(dir $@)"
	$(Q)$(CXX) $(COMPILE_FLAGS) $(CXXFLAGS) -c -o "$@" $<

$(OBJDIR)/%.c.o: %.c | $(RBASE_PCH)
	@echo "cc $<"
	@mkdir -p "$(dir $@)"
	$(Q)$(CC) $(COMPILE_FLAGS) $(CFLAGS) -include $(PCHDIR)/$(RBASE_H) -o $@ -c $<

$(OBJDIR)/%.cc.o: %.cc | $(LLVM_PCH)
	@echo "cxx $<"
	@mkdir -p "$(dir $@)"
	$(Q)$(CXX) $(COMPILE_FLAGS) $(CXXFLAGS) -include $(PCHDIR)/$(LLVM_H) -o $@ -c $<

$(OBJDIR)/%.S.o: %.S
	@echo "cc $<"
	@mkdir -p "$(dir $@)"
	$(Q)$(CC) $(CFLAGS) -o $@ -c $<

$(OBJDIR)/src/co/parse/parse.c.o: $(BUILDDIR)/gen_parselet_map.mark
$(BUILDDIR)/gen_parselet_map.mark: src/co/parse/parse.c
	$(Q)python3 misc/gen_parselet_map.py $< $@

dev:
	./misc/dev.sh -run

clean:
	rm -rf bin/co .build

DEPS := ${RBASE_OBJS:.o=.d} ${CO_OBJS:.o=.d} ${RT_TEST_OBJS:.o=.d}
-include $(DEPS)
-include $(PCHDIR)/$(RBASE_H).d
-include $(PCHDIR)/$(LLVM_H).d


.PHONY: all dev clean
