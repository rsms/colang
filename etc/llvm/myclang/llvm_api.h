#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif

typedef enum OSType { // must match llvm::Triple::OSType
  OSUnknown,
  OSAnanas,
  OSCloudABI,
  OSDarwin,
  OSDragonFly,
  OSFreeBSD,
  OSFuchsia,
  OSIOS,
  OSKFreeBSD,
  OSLinux,
  OSLv2,        // PS3
  OSMacOSX,
  OSNetBSD,
  OSOpenBSD,
  OSSolaris,
  OSWin32,
  OSHaiku,
  OSMinix,
  OSRTEMS,
  OSNaCl,       // Native Client
  OSCNK,        // BG/P Compute-Node Kernel
  OSAIX,
  OSCUDA,       // NVIDIA CUDA
  OSNVCL,       // NVIDIA OpenCL
  OSAMDHSA,     // AMD HSA Runtime
  OSPS4,
  OSELFIAMCU,
  OSTvOS,       // Apple tvOS
  OSWatchOS,    // Apple watchOS
  OSMesa3D,
  OSContiki,
  OSAMDPAL,     // AMD PAL Runtime
  OSHermitCore, // HermitCore Unikernel/Multikernel
  OSHurd,       // GNU/Hurd
  OSWASI,       // Experimental WebAssembly OS
  OSEmscripten,
} OSType;

EXTERN_C const char* LLVMGetOSTypeName(OSType os);
EXTERN_C OSType LLVMParseOS(const char* osname);
EXTERN_C OSType LLVMGetHostOSType();

EXTERN_C bool LLVMWriteArchive(
  const char *archive_name, const char** infiles, size_t infilec, OSType os);

EXTERN_C int LLDLinkCOFF(int argc, const char** argv, bool can_exit_early);
EXTERN_C int LLDLinkELF(int argc, const char** argv, bool can_exit_early);
EXTERN_C int LLDLinkMachO(int argc, const char** argv, bool can_exit_early);
EXTERN_C int LLDLinkWasm(int argc, const char** argv, bool can_exit_early);
