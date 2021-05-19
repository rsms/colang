#include "../common.h"
#include "llvm.h"
#include "llvm-includes.hh"
#include <sstream>

// DEBUG_LLD_INVOCATION: define to print arguments used for linker invocations to stderr
//#define DEBUG_LLD_INVOCATION

using namespace llvm;


// See lld/Common/Driver.h for lld api
// See lld/tools/lld/lld.cpp for the lld program main
// lld exe names:
//   ld.lld (Unix)
//   ld64.lld (macOS)
//   lld-link (Windows)
//   wasm-ld (WebAssembly)


// Sooooo lld's memory may become corrupt and requires a restart of the program.
// _lld_is_corrupt tracks this state. See lld::safeLldMain in lld/tools/lld/lld.cpp
static const char* _lld_is_corrupt = NULL;


static void _set_lld_is_corrupt(int errcode) {
  if (_lld_is_corrupt == NULL) {
    size_t bufcap = 128;
    char* buf = (char*)malloc(bufcap);
    snprintf(buf, bufcap, "lld crashed with exception code %d", errcode);
    _lld_is_corrupt = buf;
  }
}

typedef bool(*LinkFun)(
  llvm::ArrayRef<const char*> args, bool canExitEarly,
  llvm::raw_ostream &stdoutOS, llvm::raw_ostream &stderrOS);

// _link is a helper wrapper for calling the various object-specific linker functions
// It has been adapted from lld::safeLldMain in lld/tools/lld/lld.cpp
// Always sets errmsg
static bool _link(LinkFun linkf, llvm::ArrayRef<const char*> args, char** errmsg) {
  if (_lld_is_corrupt) {
    *errmsg = LLVMCreateMessage(_lld_is_corrupt);
    return false;
  }

  #ifdef DEBUG_LLD_INVOCATION
  {
    bool first = true;
    std::string s;
    for (auto& arg : args) {
      if (first) {
        first = false;
      } else {
        s += "' '";
      }
      s += arg;
    }
    // Note: std::ostringstream with std::copy somehow adds an extra empty item at the end.
    fprintf(stderr, "invoking lld: '%s'\n", s.c_str());
  }
  #endif

  // stderr
  std::string errstr;
  raw_string_ostream errout(errstr);

  bool ok = false;
  {
    // The crash recovery is here only to be able to recover from arbitrary
    // control flow when fatal() is called (through setjmp/longjmp or __try/__except).
    llvm::CrashRecoveryContext crc;
    // llvm::errs()
    const bool exitEarly = false;
    if (!crc.RunSafely([&]() { ok = linkf(args, exitEarly, llvm::outs(), errout); }))
      _set_lld_is_corrupt(crc.RetCode);
  }

  // Cleanup memory and reset everything back in pristine condition. This path
  // is only taken when LLD is in test, or when it is used as a library.
  llvm::CrashRecoveryContext crc;
  if (!crc.RunSafely([&]() { lld::errorHandler().reset(); })) {
    // The memory is corrupted beyond any possible recovery
    _set_lld_is_corrupt(crc.RetCode);
  }

  auto errs = errout.str();
  if (!ok && errs.size() == 0 && _lld_is_corrupt) {
    *errmsg = LLVMCreateMessage(_lld_is_corrupt);
  } else {
    *errmsg = LLVMCreateMessage(errs.c_str());
  }
  return ok;
}


static LinkFun select_linkfn(Triple& triple, const char** cliname) {
  switch (triple.getObjectFormat()) {
    case Triple::COFF:  *cliname = "lld-link"; return lld::coff::link;
    case Triple::ELF:   *cliname = "ld.lld";   return lld::elf::link;
    case Triple::MachO: *cliname = "ld64.lld"; return lld::mach_o::link;
    case Triple::Wasm:  *cliname = "wasm-ld";  return lld::wasm::link;

    case Triple::GOFF:  // ?
    case Triple::XCOFF: // ?
    case Triple::UnknownObjectFormat:
      break;
  }
  return NULL;
}


// build_args selects the linker function and adds args according to options and triple.
// This does not add options.infilev but it does add options.outfile (if not null) as that flag is
// linker-dependent.
// tmpstrings is a list of std::strings that should outlive the call to the returned LinkFun.
static LinkFun build_args(
  CoLLDOptions&             options,
  Triple&                   triple,
  std::vector<const char*>& args,
  std::vector<std::string>& tmpstrings)
{
  auto mktmpstr = [&](std::string&& s) {
    tmpstrings.emplace_back(s);
    return tmpstrings.back().c_str();
  };

  // select link function
  const char* arg0 = "";
  LinkFun linkfn = select_linkfn(triple, &arg0);
  if (!linkfn)
    return NULL;
  args.emplace_back(arg0);

  // common arguments
  // (See lld flavors' respective CLI help output, e.g. deps/llvm/bin/lld -flavor ld.lld -help)
  if (triple.getObjectFormat() == Triple::COFF) {
    // Windows (flavor=lld-link, flagstyle="/flag")
    // TODO consider adding "/machine:" which seems similar to "-arch"
    if (options.outfile)
      args.emplace_back(mktmpstr(std::string("/out:") + options.outfile));
  } else {
    // Rest of the world (flavor=!lld-link, flagstyle="-flag")
    auto archname = (const char*)Triple::getArchTypeName(triple.getArch()).bytes_begin();
    args.emplace_back("-arch"); args.emplace_back(archname);
    if (options.outfile) {
      args.emplace_back("-o"); args.emplace_back(options.outfile);
    }
  }

  // linker flavor-specific arguments
  switch (triple.getObjectFormat()) {
    case Triple::COFF:
      // flavor=lld-link
    dlog("TODO: COFF-specific args");
      break;
    case Triple::ELF:
    case Triple::Wasm:
      // flavor=ld.lld
      args.emplace_back("--no-pie");
      args.emplace_back(options.opt == CoOptNone ? "--lto-O0" : "--lto-O3");
      break;
    case Triple::MachO:
      // flavor=ld64.lld
      args.emplace_back("-static");
      args.emplace_back("-no_pie");
      if (options.opt != CoOptNone) {
        // optimize
        args.emplace_back("-dead_strip"); // Remove unreference code and data
        // TODO: look into -mllvm "Options to pass to LLVM during LTO"
      }
      break;
    case Triple::GOFF:  // ?
    case Triple::XCOFF: // ?
    case Triple::UnknownObjectFormat:
      return NULL;
  }

  // OS-specific arguments
  switch (triple.getOS()) {
    case Triple::Darwin:
    case Triple::MacOSX:
      // flavor=ld64.lld
      args.emplace_back("-sdk_version"); args.emplace_back("10.15");
      args.emplace_back("-lsystem"); // macOS's "syscall API"
      // args.emplace_back("-framework"); args.emplace_back("Foundation");
      break;
    case Triple::IOS:
    case Triple::TvOS:
    case Triple::WatchOS: {
      // flavor=ld64.lld
      CoLLVMVersionTuple mv;
      llvm_triple_min_version(options.targetTriple, &mv);
      dlog("TODO min version: %d, %d, %d, %d", mv.major, mv.minor, mv.subminor, mv.build);
      // + arg "-ios_version_min" ...
      break;
    }
    default: {
      auto osname = (const char*)Triple::getOSTypeName(triple.getOS()).bytes_begin();
      dlog("TODO: triple.getOS()=%s", osname);
      break;
    }
  }

  return linkfn;
}


bool lld_link(CoLLDOptions* optionsptr, char** errmsg) {
  CoLLDOptions& options = *optionsptr;
  Triple triple(Triple::normalize(options.targetTriple));

  // arguments to linker and temporary string storage for them
  std::vector<const char*> args;
  std::vector<std::string> tmpstrings;

  // select linker function and build arguments
  LinkFun linkfn = build_args(options, triple, args, tmpstrings);
  if (!linkfn) {
    *errmsg = LLVMCreateMessage("linking not supported for provided target");
    return false;
  }

  // add input files
  for (u32 i = 0; i < options.infilec; i++)
    args.emplace_back(options.infilev[i]);

  // invoke linker
  return _link(linkfn, args, errmsg);
}


// object-specific linker functions for exporting as C API:

// // lld_link_macho links objects, archives and shared libraries together into a Mach-O executable.
// // If exitearly is true, this function calls exit(1) on error instead of returning false.
// // Always sets errmsg; on success it contains warning messages (if any.)
// // Caller must always call LLVMDisposeMessage on errmsg.
// // Returns true on success.
// bool lld_link_macho(int argc, const char** argv, char** errmsg) {
//   // Note: there's both lld::mach_o and lld::macho -- the latter is a newer linker
//   // which is work in progress (April 2021.)
//   // return _link(lld::macho::link, "ld64.lld", argc, argv); // WIP
//   std::vector<const char*> args(argv, argv + argc);
//   args.insert(args.begin(), "ld64.lld");
//   return _link(lld::mach_o::link, args, errmsg);
// }

// // lld_link_coff links objects, archives and shared libraries together into a COFF executable.
// // See lld_link_macho for details
// bool lld_link_coff(int argc, const char** argv, char** errmsg) {
//   std::vector<const char*> args(argv, argv + argc);
//   args.insert(args.begin(), "lld-link");
//   return _link(lld::coff::link, args, errmsg);
// }

// // lld_link_elf links objects, archives and shared libraries together into a ELF executable.
// // See lld_link_macho for details
// bool lld_link_elf(int argc, const char** argv, char** errmsg) {
//   std::vector<const char*> args(argv, argv + argc);
//   args.insert(args.begin(), "ld.lld");
//   return _link(lld::elf::link, args, errmsg);
// }

// // lld_link_wasm links objects, archives and shared libraries together into a WASM module.
// // See lld_link_macho for details
// bool lld_link_wasm(int argc, const char** argv, char** errmsg) {
//   std::vector<const char*> args(argv, argv + argc);
//   args.insert(args.begin(), "wasm-ld");
//   return _link(lld::wasm::link, args, errmsg);
// }
