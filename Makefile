LLVM_PREFIX := deps/llvm
SYSTEM      := $(shell uname -s)
ARCH        := $(shell uname -m)
CC          := $(LLVM_PREFIX)/bin/clang
CXX         := $(LLVM_PREFIX)/bin/clang++
AR          := $(LLVM_PREFIX)/bin/llvm-ar
STRIP       := $(LLVM_PREFIX)/bin/llvm-strip
LLVM_CONFIG := $(LLVM_PREFIX)/bin/llvm-config
SRCROOT     := $(shell pwd)
RBASE_SRC   := $(wildcard src/rbase/*.c src/rbase/*/*.c)
CO_SRC      := $(wildcard src/co/*.c src/co/*/*.c)

CFLAGS := \
	-std=c17 \
	-g \
	-MMD \
	-Isrc \
	-ffile-prefix-map=$(SRCROOT)/= \
	-fstrict-aliasing \
	-fcolor-diagnostics \
	-Wall \
	-Wunused \
	-Wno-nullability-completeness \
	-Wno-nullability-inferred-on-nested-type \

CXXFLAGS := \
	-std=c++17 \
	-fvisibility-inlines-hidden \
	-fno-exceptions \
	-fno-rtti \

LDFLAGS := -Wl,-rpath,@loader_path/. -Wl,-w -dead_strip

FLAVOR := release
ifneq ($(DEBUG),)
	FLAVOR := debug
	CFLAGS += -DDEBUG
endif

# enable memory sanitizer
ifneq ($(SANITIZE),)
	FLAVOR := $(FLAVOR)-san
	CFLAGS += \
		-DASAN_ENABLED=1 \
	  -fsanitize=address \
	  -fsanitize-address-use-after-scope \
	  -fno-omit-frame-pointer \
	  -fno-optimize-sibling-calls
	LDFLAGS += -fsanitize=address
	# out local build of clang doesn't include asan libs, so use system compiler
	CC  := /usr/bin/clang
	CXX := /usr/bin/clang++
endif

ifeq ($(SYSTEM),Darwin)
	ifeq ($(ARCH),x86_64)
		# see deps/context/build/Jamfile.v2
		RBASE_SRC += \
			src/rbase/sched/exectx/exectx_x86_64_sysv.S
	else ifeq ($(ARCH),arm64)
		RBASE_SRC += \
   	  src/rbase/sched/exectx/init_arm64_aapcs_macho_gas.S \
   	  src/rbase/sched/exectx/switch_arm64_aapcs_macho_gas.S \
      src/rbase/sched/exectx/jump_arm64_aapcs_macho_gas.S
	endif

	CFLAGS += -mmacosx-version-min=10.15
  LDFLAGS += -mmacosx-version-min=10.15 \
  	-isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
endif

BUILDDIR   := .build/$(FLAVOR)
OBJDIR     := $(BUILDDIR)
CO_OBJS    := $(patsubst %,$(OBJDIR)/%.o,$(CO_SRC))
RBASE_OBJS := $(patsubst %,$(OBJDIR)/%.o,$(RBASE_SRC))
RBASE_PCH  := $(BUILDDIR)/rbase.pch
RBASE_PCH_DEPS := $(wildcard src/rbase/*.h src/rbase/*/*.h)

all: bin/co

bin/co: $(CO_OBJS) $(BUILDDIR)/rbase.a | $(RBASE_PCH)
	@echo "link $@ ($(foreach fn,$^,$(notdir ${fn:.o=})))"
	@mkdir -p "$(dir $@)"
	@$(CXX) $(LDFLAGS) $(SKIA_LIB_LDFLAGS) -o $@ $^ $(BUILDDIR)/rbase.a

lib_rbase: $(BUILDDIR)/rbase.a

$(BUILDDIR)/rbase.a: $(RBASE_OBJS) | $(RBASE_PCH)
	@echo "ar $@ ($(foreach fn,$^,$(notdir ${fn:.o=})))"
	@$(AR) rcs $@ $^

$(RBASE_PCH): src/rbase/rbase.h $(RBASE_PCH_DEPS)
	@echo "cc $< -> $@"
	@mkdir -p "$(dir $@)"
	@$(CC) $(CFLAGS) -c -o "$@" src/rbase/rbase.h

$(OBJDIR)/%.c.o: %.c | $(RBASE_PCH)
	@echo "cc $<"
	@mkdir -p "$(dir $@)"
	@$(CC) $(CFLAGS) -include-pch $(RBASE_PCH) -o $@ -c $<

$(OBJDIR)/%.S.o: %.S
	@echo "cc $<"
	@mkdir -p "$(dir $@)"
	@$(CC) $(CFLAGS) -o $@ -c $<

# $(OBJDIR)/%.cc.o: %.cc | $(RBASE_PCH)
# 	@echo "cc $<"
# 	@mkdir -p "$(dir $@)"
# 	@$(CXX) $(CFLAGS) $(CXXFLAGS) -include-pch $(RBASE_PCH) -o $@ -c $<

dev:
	./misc/dev.sh -run

clean:
	rm -rf bin/co .build

DEPS := ${RBASE_OBJS:.o=.d} ${CO_OBJS:.o=.d}
-include $(DEPS)

.PHONY: all clean
