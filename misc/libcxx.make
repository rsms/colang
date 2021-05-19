DEPSDIR := @DEPSDIR@
DESTDIR := @DESTDIR@
LIBCXX_OBJS := @LIBCXX_OBJS@
LIBCXXABI_OBJS := @LIBCXXABI_OBJS@

LIBCXX_OBJDIR := libcxx
LIBCXXABI_OBJDIR := libcxxabi

LIBCXX_OBJS := $(foreach fn,$(LIBCXX_OBJS),$(LIBCXX_OBJDIR)/$(fn))
LIBCXXABI_OBJS := $(foreach fn,$(LIBCXXABI_OBJS),$(LIBCXXABI_OBJDIR)/$(fn))

CXXC := $(DEPSDIR)/llvm/bin/clang++
AR   := $(DEPSDIR)/llvm/bin/llvm-ar

CFLAGS := \
	-Wall -nostdinc++ -fvisibility-inlines-hidden -std=c++14 -Wno-user-defined-literals \
	-DNDEBUG \
	-D_LIBCXXABI_DISABLE_VISIBILITY_ANNOTATIONS \
	-D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS \
	-I$(DESTDIR)/include \
	-I$(DESTDIR)/libcxxabi/include

ifneq ($(MUSL_ABI),)
	CFLAGS += -D_LIBCPP_HAS_MUSL_LIBC
endif

ifneq ($(TARGET_SUPPORTS_FPIC),)
	CFLAGS += -fPIC
endif

CFLAGS_LIBCPP := \
	-D_LIBCPP_BUILDING_LIBRARY \
	-D_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER \
	-DLIBCXX_BUILDING_LIBCXXABI

CFLAGS_LIBCPPABI := \
	-DHAVE___CXA_THREAD_ATEXIT_IMPL \
	-D_LIBCPP_DISABLE_EXTERN_TEMPLATE \
	-D_LIBCPP_ENABLE_CXX17_REMOVED_UNEXPECTED_FUNCTIONS \
	-D_LIBCXXABI_BUILDING_LIBRARY

all: $(DESTDIR)/lib/libc++.a $(DESTDIR)/lib/libc++abi.a

$(DESTDIR)/lib/libc++.a: $(LIBCXX_OBJS)
	@mkdir -p $(dir $@)
	$(AR) -r $@ $^

$(DESTDIR)/lib/libc++abi.a: $(LIBCXXABI_OBJS)
	@mkdir -p $(dir $@)
	$(AR) -r $@ $^

$(LIBCXX_OBJDIR)/%.o: $(DESTDIR)/libcxx/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXXC) $(CFLAGS) $(CFLAGS_LIBCPP) -c -o $@ $<

$(LIBCXXABI_OBJDIR)/%.o: $(DESTDIR)/libcxxabi/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXXC) $(CFLAGS) $(CFLAGS_LIBCPPABI) -c -o $@ $<

# $(LIBCXX_OBJS): | $(LIBCXX_OBJDIR)
# $(LIBCXXABI_OBJS): | $(LIBCXXABI_OBJDIR)
# $(LIBCXX_OBJDIR):
# 	mkdir $(LIBCXX_OBJDIR)
# $(LIBCXXABI_OBJDIR):
# 	mkdir $(LIBCXXABI_OBJDIR)

Makefile:
	@true
