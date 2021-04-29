#include <rbase/rbase.h>
#include "llvm.h"
#include "llvm-includes.hh"
#include "lld/Common/Driver.h"
#include "lld/Common/ErrorHandler.h"

using namespace llvm;


static bool llvm_error_to_errmsg(Error err, char** errmsg) {
  assert(err);
  std::string errstr = toString(std::move(err));
  *errmsg = LLVMCreateMessage(errstr.c_str());
  return false;
}


const char* llvm_init_targets() {
  static std::once_flag once;
  static char* hostTriple;
  std::call_once(once, [](){
    #if 1
      // Initialize ALL targets (this causes a lot of llvm code to be included in this program)
      // Note: lld (liblldCOFF.a) requires all targets
      InitializeAllTargetInfos();
      InitializeAllTargets();
      InitializeAllTargetMCs();
      InitializeAllAsmPrinters();
      InitializeAllAsmParsers();
    #else
      // For JIT only, call llvm::InitializeNativeTarget()
      InitializeNativeTarget();

      // Initialize some targets (see llvm/Config/Targets.def)
      #define TARGETS(_) _(AArch64) _(WebAssembly) _(X86)
      #define _(TargetName) \
        LLVMInitialize##TargetName##TargetInfo(); \
        LLVMInitialize##TargetName##Target(); \
        LLVMInitialize##TargetName##TargetMC(); \
        LLVMInitialize##TargetName##AsmPrinter(); \
        /*LLVMInitialize##TargetName##AsmParser();*/
      TARGETS(_)
      #undef _
    #endif

    hostTriple = LLVMGetDefaultTargetTriple();
    // Note: if we ever make this non-static, LLVMDisposeMessage(hostTriple) when done.
  });
  return hostTriple;
}


void llvm_triple_info(
  const char*         triplestr,
  CoLLVMArch*         arch_type,
  CoLLVMVendor*       vendor_type,
  CoLLVMOS*           os_type,
  CoLLVMEnvironment*  environ_type,
  CoLLVMObjectFormat* oformat)
{
  Triple triple(Triple::normalize(triplestr));
  // Triple triple(hostTriple);
  *arch_type    = (CoLLVMArch)triple.getArch();
  *vendor_type  = (CoLLVMVendor)triple.getVendor();
  *os_type      = (CoLLVMOS)triple.getOS();
  *environ_type = (CoLLVMEnvironment)triple.getEnvironment();
  *oformat      = (CoLLVMObjectFormat)triple.getObjectFormat();
}

void llvm_triple_min_version(const char* triple, CoLLVMVersionTuple* r) {
  Triple t(Triple::normalize(triple));
  VersionTuple v = t.getMinimumSupportedOSVersion();
  if (v.empty()) {
    r->major = -1;
    r->minor = -1;
    r->subminor = -1;
    r->build = -1;
  } else {
    r->major    = (int)v.getMajor();
    r->minor    = v.getMinor() == None ? -1 : (int)v.getMinor().getValue();
    r->subminor = v.getSubminor() == None ? -1 : (int)v.getSubminor().getValue();
    r->build    = v.getBuild() == None ? -1 : (int)v.getBuild().getValue();
  }
}


// CoLLVMOS_name returns the canonical name for the OS
const char* CoLLVMOS_name(CoLLVMOS os) {
  return (const char*)Triple::getOSTypeName((Triple::OSType)os).bytes_begin();
}

// CoLLVMArch_name returns the canonical name for the arch
const char* CoLLVMArch_name(CoLLVMArch v) {
  return (const char*)Triple::getArchTypeName((Triple::ArchType)v).bytes_begin();
}

// CoLLVMVendor_name returns the canonical name for the vendor
const char* CoLLVMVendor_name(CoLLVMVendor v) {
  return (const char*)Triple::getVendorTypeName((Triple::VendorType)v).bytes_begin();
}

// CoLLVMEnvironment_name returns the canonical name for the environment
const char* CoLLVMEnvironment_name(CoLLVMEnvironment v) {
  return (const char*)Triple::getEnvironmentTypeName((Triple::EnvironmentType)v).bytes_begin();
}


bool llvm_optmod(
  LLVMModuleRef        M,
  LLVMTargetMachineRef T,
  CoOptType            opt,
  bool                 enable_tsan,
  bool                 enable_lto,
  char**               errmsg)
{
  Module& module = *unwrap(M);
  TargetMachine& targetMachine = *reinterpret_cast<TargetMachine*>(T);

  module.setTargetTriple(targetMachine.getTargetTriple().str());
  module.setDataLayout(targetMachine.createDataLayout());

  bool is_debug = opt == CoOptNone;

  // pass performance debugging
  bool enable_time_report = false;
  TimePassesIsEnabled = enable_time_report; // global llvm variable

  // Pipeline configurations
  PipelineTuningOptions pipelineOpt;
  pipelineOpt.LoopUnrolling = !is_debug;
  pipelineOpt.SLPVectorization = !is_debug;
  pipelineOpt.LoopVectorization = !is_debug;
  pipelineOpt.LoopInterleaving = !is_debug;
  pipelineOpt.MergeFunctions = !is_debug;

  // Instrumentations
  // https://github.com/ziglang/zig/blob/52d871844c643f396a2bddee0753d24ff7/src/zig_llvm.cpp#L190
  PassInstrumentationCallbacks instrCallbacks;
  StandardInstrumentations std_instrumentations(false);
  std_instrumentations.registerCallbacks(instrCallbacks);

  // optimization pass builder
  const bool debugLogging = false;
  Optional<PGOOptions> pgo = None;
  PassBuilder passBuilder(debugLogging, &targetMachine, pipelineOpt, pgo, &instrCallbacks);
  LoopAnalysisManager     loopAM;
  FunctionAnalysisManager functionAM;
  CGSCCAnalysisManager    cgsccAM;
  ModuleAnalysisManager   moduleAM;

  // Register the AA manager first so that our version is the one used
  functionAM.registerPass([&] { return passBuilder.buildDefaultAAPipeline(); });

  // Register TargetLibraryAnalysis
  Triple targetTriple(module.getTargetTriple());
  auto tlii = std::make_unique<TargetLibraryInfoImpl>(targetTriple);
  functionAM.registerPass([&] { return TargetLibraryAnalysis(*tlii); });

  // Initialize AnalysisManagers
  passBuilder.registerModuleAnalyses(moduleAM);
  passBuilder.registerCGSCCAnalyses(cgsccAM);
  passBuilder.registerFunctionAnalyses(functionAM);
  passBuilder.registerLoopAnalyses(loopAM);
  passBuilder.crossRegisterProxies(loopAM, functionAM, cgsccAM, moduleAM);

  // type alias for convenience
  using OptimizationLevel = typename PassBuilder::OptimizationLevel;

  // IR verification
  #ifdef DEBUG
  // Verify the input
  passBuilder.registerPipelineStartEPCallback([](ModulePassManager& mpm, OptimizationLevel OL) {
    mpm.addPass(VerifierPass());
  });
  // Verify the output
  passBuilder.registerOptimizerLastEPCallback([](ModulePassManager& mpm, OptimizationLevel OL) {
    mpm.addPass(VerifierPass());
  });
  #endif

  // Passes specific for release build
  if (!is_debug) {
    passBuilder.registerPipelineStartEPCallback([](ModulePassManager& mpm, OptimizationLevel OL) {
      mpm.addPass(createModuleToFunctionPassAdaptor(AddDiscriminatorsPass()));
    });
  }

  // Thread sanitizer
  if (enable_tsan) {
    passBuilder.registerOptimizerLastEPCallback([](ModulePassManager& mpm, OptimizationLevel OL) {
      mpm.addPass(ThreadSanitizerPass());
    });
  }

  // Initialize ModulePassManager
  ModulePassManager MPM;
  OptimizationLevel optLevel;
  switch (opt) {
    case CoOptNone:       optLevel = OptimizationLevel::O0; break;
    case CoOptSmall:      optLevel = OptimizationLevel::Oz; break;
    case CoOptAggressive: optLevel = OptimizationLevel::O3; break;
  }
  if (optLevel == OptimizationLevel::O0) {
    MPM = passBuilder.buildO0DefaultPipeline(optLevel, enable_lto);
  } else if (enable_lto) {
    MPM = passBuilder.buildLTOPreLinkDefaultPipeline(optLevel);
  } else {
    MPM = passBuilder.buildPerModuleDefaultPipeline(optLevel);
  }

  // run passes
  MPM.run(module, moduleAM);

  // print perf information
  if (enable_time_report)
    TimerGroup::printAll(errs());

  return true;
}


bool llvm_emit_bc(LLVMModuleRef M, const char* filename, char** errmsg) {
  // Note: llvm-c provides a simplified function LLVMWriteBitcodeToFile

  // WriteBitcodeToFile documentation:
  //   Write the specified module to the specified raw output stream.
  //
  //   For streams where it matters, the given stream should be in "binary"
  //   mode.
  //
  //   If ShouldPreserveUseListOrder, encode the use-list order for each
  //   Value in M.  These will be reconstructed exactly when M is
  //   deserialized.
  //
  //   If Index is supplied, the bitcode will contain the summary index
  //   (currently for use in ThinLTO optimization).
  //
  //   genHash enables hashing the Module and including the hash in the
  //   bitcode (currently for use in ThinLTO incremental build).
  //
  //   If ModHash is non-null, when genHash is true, the resulting
  //   hash is written into ModHash. When genHash is false, that value
  //   is used as the hash instead of computing from the generated bitcode.
  //   Can be used to produce the same module hash for a minimized bitcode
  //   used just for the thin link as in the regular full bitcode that will
  //   be used in the backend.
  //
  Module& module = *unwrap(M);
  std::error_code EC;
  raw_fd_ostream dest(filename, EC, sys::fs::OF_None);
  if (EC) {
    *errmsg = LLVMCreateMessage(EC.message().c_str());
    return false;
  }
  bool preserveUseListOrder = false;
  const ModuleSummaryIndex* index = nullptr;
  bool genHash = false;
  ModuleHash* modHash = nullptr;
  WriteBitcodeToFile(module, dest, preserveUseListOrder, index, genHash, modHash);
  dest.flush();
  return true;
}


bool llvm_write_archive(
  const char* arhivefile, const char** filesv, u32 filesc, CoLLVMOS os, char** errmsg)
{
  object::Archive::Kind kind;
  switch (os) {
    case CoLLVMOS_Win32:
      // For some reason llvm-lib passes K_GNU on windows.
      // See lib/ToolDrivers/llvm-lib/LibDriver.cpp:168 in libDriverMain
      kind = object::Archive::K_GNU;
      break;
    case CoLLVMOS_Linux:
      kind = object::Archive::K_GNU;
      break;
    case CoLLVMOS_MacOSX:
    case CoLLVMOS_Darwin:
    case CoLLVMOS_IOS:
      kind = object::Archive::K_DARWIN;
      break;
    case CoLLVMOS_OpenBSD:
    case CoLLVMOS_FreeBSD:
      kind = object::Archive::K_BSD;
      break;
    default:
      kind = object::Archive::K_GNU;
  }
  bool deterministic = true;
  SmallVector<NewArchiveMember, 4> newMembers;
  for (u32 i = 0; i < filesc; i += 1) {
    Expected<NewArchiveMember> newMember = NewArchiveMember::getFile(filesv[i], deterministic);
    Error err = newMember.takeError();
    if (err)
      return llvm_error_to_errmsg(std::move(err), errmsg);
    newMembers.push_back(std::move(*newMember));
  }
  bool writeSymtab = true;
  bool thin = false;
  Error err = writeArchive(arhivefile, newMembers, writeSymtab, kind, deterministic, thin);
  if (err)
    return llvm_error_to_errmsg(std::move(err), errmsg);
  return false;
}
