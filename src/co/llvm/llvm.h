#pragma once
#include "../build.h"

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Target.h>
#include <llvm-c/Initialization.h>
#include <llvm-c/TargetMachine.h>

#ifdef __cplusplus
  #define EXTERN_C extern "C"
#else
  #define EXTERN_C
#endif

// The following enums are copied from include/llvm/ADT/Triple.h and
// MUST BE UPDATED when llvm is updated.
// TODO: automate generating these enums from C++ header.

typedef enum CoLLVMArch {
  CoLLVMArch_unknown,
  CoLLVMArch_arm,            // ARM (little endian): arm, armv.*, xscale
  CoLLVMArch_armeb,          // ARM (big endian): armeb
  CoLLVMArch_aarch64,        // AArch64 (little endian): aarch64
  CoLLVMArch_aarch64_be,     // AArch64 (big endian): aarch64_be
  CoLLVMArch_aarch64_32,     // AArch64 (little endian) ILP32: aarch64_32
  CoLLVMArch_arc,            // ARC: Synopsys ARC
  CoLLVMArch_avr,            // AVR: Atmel AVR microcontroller
  CoLLVMArch_bpfel,          // eBPF or extended BPF or 64-bit BPF (little endian)
  CoLLVMArch_bpfeb,          // eBPF or extended BPF or 64-bit BPF (big endian)
  CoLLVMArch_csky,           // CSKY: csky
  CoLLVMArch_hexagon,        // Hexagon: hexagon
  CoLLVMArch_mips,           // MIPS: mips, mipsallegrex, mipsr6
  CoLLVMArch_mipsel,         // MIPSEL: mipsel, mipsallegrexe, mipsr6el
  CoLLVMArch_mips64,         // MIPS64: mips64, mips64r6, mipsn32, mipsn32r6
  CoLLVMArch_mips64el,       // MIPS64EL: mips64el, mips64r6el, mipsn32el, mipsn32r6el
  CoLLVMArch_msp430,         // MSP430: msp430
  CoLLVMArch_ppc,            // PPC: powerpc
  CoLLVMArch_ppcle,          // PPCLE: powerpc (little endian)
  CoLLVMArch_ppc64,          // PPC64: powerpc64, ppu
  CoLLVMArch_ppc64le,        // PPC64LE: powerpc64le
  CoLLVMArch_r600,           // R600: AMD GPUs HD2XXX - HD6XXX
  CoLLVMArch_amdgcn,         // AMDGCN: AMD GCN GPUs
  CoLLVMArch_riscv32,        // RISC-V (32-bit): riscv32
  CoLLVMArch_riscv64,        // RISC-V (64-bit): riscv64
  CoLLVMArch_sparc,          // Sparc: sparc
  CoLLVMArch_sparcv9,        // Sparcv9: Sparcv9
  CoLLVMArch_sparcel,        // Sparc: (endianness = little). NB: 'Sparcle' is a CPU variant
  CoLLVMArch_systemz,        // SystemZ: s390x
  CoLLVMArch_tce,            // TCE (http://tce.cs.tut.fi/): tce
  CoLLVMArch_tcele,          // TCE little endian (http://tce.cs.tut.fi/): tcele
  CoLLVMArch_thumb,          // Thumb (little endian): thumb, thumbv.*
  CoLLVMArch_thumbeb,        // Thumb (big endian): thumbeb
  CoLLVMArch_x86,            // X86: i[3-9]86
  CoLLVMArch_x86_64,         // X86-64: amd64, x86_64
  CoLLVMArch_xcore,          // XCore: xcore
  CoLLVMArch_nvptx,          // NVPTX: 32-bit
  CoLLVMArch_nvptx64,        // NVPTX: 64-bit
  CoLLVMArch_le32,           // le32: generic little-endian 32-bit CPU (PNaCl)
  CoLLVMArch_le64,           // le64: generic little-endian 64-bit CPU (PNaCl)
  CoLLVMArch_amdil,          // AMDIL
  CoLLVMArch_amdil64,        // AMDIL with 64-bit pointers
  CoLLVMArch_hsail,          // AMD HSAIL
  CoLLVMArch_hsail64,        // AMD HSAIL with 64-bit pointers
  CoLLVMArch_spir,           // SPIR: standard portable IR for OpenCL 32-bit version
  CoLLVMArch_spir64,         // SPIR: standard portable IR for OpenCL 64-bit version
  CoLLVMArch_kalimba,        // Kalimba: generic kalimba
  CoLLVMArch_shave,          // SHAVE: Movidius vector VLIW processors
  CoLLVMArch_lanai,          // Lanai: Lanai 32-bit
  CoLLVMArch_wasm32,         // WebAssembly with 32-bit pointers
  CoLLVMArch_wasm64,         // WebAssembly with 64-bit pointers
  CoLLVMArch_renderscript32, // 32-bit RenderScript
  CoLLVMArch_renderscript64, // 64-bit RenderScript
  CoLLVMArch_ve,             // NEC SX-Aurora Vector Engine
  CoLLVMArch_LAST = CoLLVMArch_ve
} CoLLVMArch;

typedef enum CoLLVMVendor {
  CoLLVMVendor_unknown,
  CoLLVMVendor_Apple,
  CoLLVMVendor_PC,
  CoLLVMVendor_SCEI,
  CoLLVMVendor_Freescale,
  CoLLVMVendor_IBM,
  CoLLVMVendor_ImaginationTechnologies,
  CoLLVMVendor_MipsTechnologies,
  CoLLVMVendor_NVIDIA,
  CoLLVMVendor_CSR,
  CoLLVMVendor_Myriad,
  CoLLVMVendor_AMD,
  CoLLVMVendor_Mesa,
  CoLLVMVendor_SUSE,
  CoLLVMVendor_OpenEmbedded,
  CoLLVMVendor_LAST = CoLLVMVendor_OpenEmbedded
} CoLLVMVendor;

typedef enum CoLLVMOS {
  CoLLVMOS_unknown,
  CoLLVMOS_Ananas,
  CoLLVMOS_CloudABI,
  CoLLVMOS_Darwin,
  CoLLVMOS_DragonFly,
  CoLLVMOS_FreeBSD,
  CoLLVMOS_Fuchsia,
  CoLLVMOS_IOS,
  CoLLVMOS_KFreeBSD,
  CoLLVMOS_Linux,
  CoLLVMOS_Lv2,        // PS3
  CoLLVMOS_MacOSX,
  CoLLVMOS_NetBSD,
  CoLLVMOS_OpenBSD,
  CoLLVMOS_Solaris,
  CoLLVMOS_Win32,
  CoLLVMOS_ZOS,
  CoLLVMOS_Haiku,
  CoLLVMOS_Minix,
  CoLLVMOS_RTEMS,
  CoLLVMOS_NaCl,       // Native Client
  CoLLVMOS_AIX,
  CoLLVMOS_CUDA,       // NVIDIA CUDA
  CoLLVMOS_NVCL,       // NVIDIA OpenCL
  CoLLVMOS_AMDHSA,     // AMD HSA Runtime
  CoLLVMOS_PS4,
  CoLLVMOS_ELFIAMCU,
  CoLLVMOS_TvOS,       // Apple tvOS
  CoLLVMOS_WatchOS,    // Apple watchOS
  CoLLVMOS_Mesa3D,
  CoLLVMOS_Contiki,
  CoLLVMOS_AMDPAL,     // AMD PAL Runtime
  CoLLVMOS_HermitCore, // HermitCore Unikernel/Multikernel
  CoLLVMOS_Hurd,       // GNU/Hurd
  CoLLVMOS_WASI,       // Experimental WebAssembly OS
  CoLLVMOS_Emscripten,
  CoLLVMOS_LAST = CoLLVMOS_Emscripten
} CoLLVMOS;

typedef enum CoLLVMEnvironment {
  CoLLVMEnvironment_unknown,
  CoLLVMEnvironment_GNU,
  CoLLVMEnvironment_GNUABIN32,
  CoLLVMEnvironment_GNUABI64,
  CoLLVMEnvironment_GNUEABI,
  CoLLVMEnvironment_GNUEABIHF,
  CoLLVMEnvironment_GNUX32,
  CoLLVMEnvironment_GNUILP32,
  CoLLVMEnvironment_CODE16,
  CoLLVMEnvironment_EABI,
  CoLLVMEnvironment_EABIHF,
  CoLLVMEnvironment_Android,
  CoLLVMEnvironment_Musl,
  CoLLVMEnvironment_MuslEABI,
  CoLLVMEnvironment_MuslEABIHF,
  CoLLVMEnvironment_MSVC,
  CoLLVMEnvironment_Itanium,
  CoLLVMEnvironment_Cygnus,
  CoLLVMEnvironment_CoreCLR,
  CoLLVMEnvironment_Simulator, // Simulator variants of other systems, e.g., Apple's iOS
  CoLLVMEnvironment_MacABI, // Mac Catalyst variant of Apple's iOS deployment target.
  CoLLVMEnvironment_LAST = CoLLVMEnvironment_MacABI
} CoLLVMEnvironment;

typedef enum CoLLVMObjectFormat {
  CoLLVMObjectFormat_unknown,
  CoLLVMObjectFormat_COFF,
  CoLLVMObjectFormat_ELF,
  CoLLVMObjectFormat_GOFF,
  CoLLVMObjectFormat_MachO,
  CoLLVMObjectFormat_Wasm,
  CoLLVMObjectFormat_XCOFF,
} CoLLVMObjectFormat;

// CoLLVMVersionTuple represents a version. -1 is used to indicate "not applicable."
typedef struct CoLLVMVersionTuple { int major, minor, subminor, build; } CoLLVMVersionTuple;

// llvm_build_and_emit
typedef struct Node Node;
EXTERN_C bool llvm_build_and_emit(Build* build, Node* pkgnode, const char* triple);

// llvm_init_targets initializes target info and returns the default target triplet.
// Safe to call multiple times. Just returns a cached value on subsequent calls.
EXTERN_C const char* llvm_init_targets();

// llvm_triple_info returns structured information about a target triple
EXTERN_C void llvm_triple_info(
  const char*         triple,
  CoLLVMArch*         arch_type,
  CoLLVMVendor*       vendor_type,
  CoLLVMOS*           os_type,
  CoLLVMEnvironment*  environ_type,
  CoLLVMObjectFormat* oformat);

// llvm_triple_min_version returns the minimum supported OS version for a target triple.
// Some platforms have different minimum supported OS versions that varies by the architecture
// specified in the triple. This function returns the minimum supported OS version for this
// triple if one an exists. If the triple doesn't have a minimum version, major is set to -1.
// If a minor, subminor or build is not specificed, their values are set to -1.
EXTERN_C void llvm_triple_min_version(const char* triple, CoLLVMVersionTuple* result);

EXTERN_C const char* CoLLVMOS_name(CoLLVMOS); // canonical name
EXTERN_C const char* CoLLVMArch_name(CoLLVMArch); // canonical name
EXTERN_C const char* CoLLVMVendor_name(CoLLVMVendor); // canonical name
EXTERN_C const char* CoLLVMEnvironment_name(CoLLVMEnvironment); // canonical name

// llvm_optmod applies module-wide optimizations.
// Returns false on error and sets errmsg; caller should dispose it with LLVMDisposeMessage.
EXTERN_C bool llvm_optmod(
  LLVMModuleRef        mod,
  LLVMTargetMachineRef targetm,
  CoOptType            opt,
  bool                 enable_tsan,
  bool                 enable_lto,
  char**               errmsg);

// llvm_emit_bc writes LLVM IR (text) code to filename.
// Returns false on error and sets errmsg; caller should dispose it with LLVMDisposeMessage.
static bool llvm_emit_ir(LLVMModuleRef, const char* filename, char** errmsg);

// llvm_emit_bc writes LLVM bitcode to filename.
// Returns false on error and sets errmsg; caller should dispose it with LLVMDisposeMessage.
EXTERN_C bool llvm_emit_bc(LLVMModuleRef, const char* filename, char** errmsg);

// llvm_emit_mc applies module-wide optimizations (unless CoBuildDebug) and emits machine-specific
// code to asm_outfile and/or bin_outfile.
// Returns false on error and sets errmsg; caller should dispose it with LLVMDisposeMessage.
static bool llvm_emit_mc(
  LLVMModuleRef, LLVMTargetMachineRef, LLVMCodeGenFileType, const char* filename, char** errmsg);

// llvm_write_archive creates an archive (like the ar tool) at arhivefile with filesv.
// Returns false on error and sets errmsg; caller should dispose it with LLVMDisposeMessage.
EXTERN_C bool llvm_write_archive(
  const char*  arhivefile,
  const char** filesv,
  u32          filesc,
  CoLLVMOS     os,
  char**       errmsg);

// CoLLDOptions specifies options for an invocation of lld_link
typedef struct CoLLDOptions {
  const char*          targetTriple; // target machine triple
  CoOptType            opt;     // optimization level
  const char* nullable outfile; // output file. NULL for no output
  u32                  infilec; // input file count
  const char**         infilev; // input file array
} CoLLDOptions;

// lld_link links objects, archives and shared libraries together into a library or executable.
// It is a high-level interface to the target-specific linker implementations.
// Always sets errmsg; on success it contains warning messages (if any.)
// Caller must always call LLVMDisposeMessage on errmsg.
// Returns true on success.
EXTERN_C bool lld_link(CoLLDOptions* options, char** errmsg);


// -----------------------------------------------------------------------------------------------
// implementations

inline static bool llvm_emit_ir(LLVMModuleRef M, const char* filename, char** errmsg) {
  return LLVMPrintModuleToFile(M, filename, errmsg) == 0;
}

inline static bool llvm_emit_mc(
  LLVMModuleRef        M,
  LLVMTargetMachineRef T,
  LLVMCodeGenFileType  FT,
  const char*          filename,
  char**               errmsg)
{
  // Note: Filename argument to LLVMTargetMachineEmitToFile is incorrectly typed as mutable
  // "char*" in llvm-c/TargetMachine.h. It's really "const char*" as is evident by looking at
  // the implementation in llvm/lib/Target/TargetMachineC.cpp.
  return LLVMTargetMachineEmitToFile(T, M, (char*)filename, FT, errmsg) == 0;
}
