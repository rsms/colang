#include <stdio.h>
// #include <stdlib.h> // exit
#include <string.h> // strcmp
#include "llvm_api.h"

const char* prog; // main program name
OSType host_os;

extern int clang_main(int c, const char** v);

OSType OSTypeParse(const char* name) {
  if (strcmp(name, "darwin") == 0)  return OSDarwin;
  if (strcmp(name, "freebsd") == 0) return OSFreeBSD;
  if (strcmp(name, "ios") == 0)     return OSIOS;
  if (strcmp(name, "linux") == 0)   return OSLinux;
  if (strcmp(name, "macosx") == 0)  return OSMacOSX;
  if (strcmp(name, "openbsd") == 0) return OSOpenBSD;
  if (strcmp(name, "win32") == 0)   return OSWin32;
  return OSUnknown;
}

void usage_main(FILE* f) {
  const char* host_os_typename = LLVMGetOSTypeName(host_os);
  fprintf(f,
    "usage: %s <command> [args ...]\n"
    "commands:\n"
    "  cc [args ...]        Clang\n"
    "  as [args ...]        LLVM assembler (same as cc -cc1as)\n"
    "  ar [args ...]        Create object archive\n"
    "  ld [args ...]        Linker for host system (%s)\n"
    "  ld-coff [args ...]   Linker for COFF\n"
    "  ld-elf [args ...]    Linker for ELF\n"
    "  ld-macho [args ...]  Linker for Mach-O\n"
    "  ld-wasm [args ...]   Linker for WebAssembly\n"
    "",
    prog,
    host_os_typename);
}

int ar_main(int argc, const char* argv[argc+1]) {
  OSType os = host_os;
  // TODO: accept --target triple (where we really only parse the os)

  // const char* osname = (strlen(argv[0]) > 2) ? &argv[0][3] : "";
  // OSType os = OSDarwin; // TODO: ifdef ... select host
  // if (strlen(osname) > 0) {
  //   os = OSTypeParse(osname);
  //   if (os == OSUnknown) {
  //     fprintf(stderr,
  //       "%s: unknown archiver %s;"
  //       " expected one of -ar, -ar.darwin, -ar.freebsd, -ar.ios, -ar.linux,"
  //       " -ar.macosx, -ar.openbsd, -ar.win32\n",
  //       parentprog, argv[0]);
  //     return 1;
  //   }
  // }
  // if (argc < 3) {
  //   fprintf(stderr, "usage: %s %s <archive> <file> ...\n", parentprog, argv[0]);
  //   return 1;
  // }
  return LLVMWriteArchive(argv[1], &argv[2], argc-2, os) ? 0 : 1;
}


int main(int argc, const char* argv[argc+1]) {
  prog = argv[0];
  host_os = LLVMGetHostOSType();
  if (argc > 1) {
    argc--;
    argv = &argv[1];
    const char* cmd = argv[0];
    size_t cmdlen = strlen(cmd);
    #define ISCMD(s) (cmdlen == strlen(s) && memcmp(cmd, (s), cmdlen) == 0)

    if ISCMD("cc")
      return clang_main(argc, argv);
    if ISCMD("as") {
      argv[1] = "-cc1as";
      return clang_main(argc, argv);
    }
    if ISCMD("ar")
      return ar_main(argc, argv);
    if ISCMD("ld-macho")
      return LLDLinkMachO(argc, argv, true);
    if ISCMD("ld-elf")
      return LLDLinkELF(argc, argv, true);
    if ISCMD("ld-coff")
      return LLDLinkCOFF(argc, argv, true);
    if ISCMD("ld-wasm")
      return LLDLinkWasm(argc, argv, true);
    if ISCMD("ld") {
      // TODO host
      switch (host_os) {
        case OSDarwin:
        case OSMacOSX:
        case OSIOS:
        case OSTvOS:
        case OSWatchOS:
          return LLDLinkMachO(argc, argv, true);
        case OSWin32:
          return LLDLinkCOFF(argc, argv, true);
        case OSWASI:
        case OSEmscripten:
          return LLDLinkWasm(argc, argv, true);
        // assume the rest uses ELF (this is probably not correct)
        case OSAnanas:
        case OSCloudABI:
        case OSDragonFly:
        case OSFreeBSD:
        case OSFuchsia:
        case OSKFreeBSD:
        case OSLinux:
        case OSLv2:
        case OSNetBSD:
        case OSOpenBSD:
        case OSSolaris:
        case OSHaiku:
        case OSMinix:
        case OSRTEMS:
        case OSNaCl:
        case OSCNK:
        case OSAIX:
        case OSCUDA:
        case OSNVCL:
        case OSAMDHSA:
        case OSPS4:
        case OSELFIAMCU:
        case OSMesa3D:
        case OSContiki:
        case OSAMDPAL:
        case OSHermitCore:
        case OSHurd:
          return LLDLinkELF(argc, argv, true);
        default:
          fprintf(stderr, "%s ld: unsupported host OS %s\n", prog, LLVMGetOSTypeName(host_os));
          return 1;
      }
    }
    if (strstr(cmd, "-h") || strstr(cmd, "help")) {
      usage_main(stdout);
      return 0;
    }
  } // argc > 1
  usage_main(stderr);
  return 1;
}
