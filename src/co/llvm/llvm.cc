#include "llvm.h"
#include "llvm.hh"
#include "lld/Common/Driver.h"

using namespace llvm;
// using namespace llvm::sys;
// using namespace llvm::orc;



// https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl09.html
struct DebugInfo {
  DICompileUnit*        TheCU;
  std::vector<DIScope*> LexicalBlocks;
  DIType*               uint32Type;
  // void emitLocation(ExprAST *AST);
  // DIType *getDoubleTy();
};

struct GenState {
  LLVMContext&                 ctx;
  std::unique_ptr<Module>      mod;
  std::unique_ptr<IRBuilder<>> Builder;
  std::map<std::string,Value*> NamedValues;

  // debug info
  std::unique_ptr<DIBuilder>   DBuilder;
  DebugInfo                    debug;

  // target
  std::unique_ptr<TargetMachine> target;

  // optimizations
  std::unique_ptr<AnalysisManager<Function>> FAM;
  std::unique_ptr<PassManager<Function>> FPM; // function pass manager
};

struct FunInfo {
  std::string              name;
  std::vector<std::string> args;
};

static void print_duration(const char* message, u64 timestart) {
  auto timeend = nanotime();
  char abuf[40];
  auto buflen = fmtduration(abuf, countof(abuf), timeend - timestart);
  fprintf(stderr, "%s %.*s\n", message, buflen, abuf);
  fflush(stderr);
}

static void emit_srcpos(GenState& gs, u32 line, u32 col) {
  if (gs.debug.TheCU) {
    DIScope* Scope;
    if (gs.debug.LexicalBlocks.empty()) {
      Scope = gs.debug.TheCU;
    } else {
      Scope = gs.debug.LexicalBlocks.back();
    }
    gs.Builder->SetCurrentDebugLocation(DILocation::get(Scope->getContext(), line, col, Scope));
  }
}

Value* gen_floatlit(GenState& gs, double v) {
  return ConstantFP::get(gs.ctx, APFloat(v));
}

Value* gen_int32lit(GenState& gs, int32_t v) {
  return ConstantInt::get(gs.ctx, APInt(32, v, /*isSigned*/true));
}

Value* gen_call(GenState& gs, std::string fname, std::vector<Value*> argvals) {
  emit_srcpos(gs, /*line*/1, /*col*/1);

  // Look up the name in the global module table.
  Function* CalleeF = gs.mod->getFunction(fname);
  if (!CalleeF) {
    errlog("gen_call: unknown function %s", fname.c_str());
    return nullptr;
  }

  // If argument mismatch error.
  if (CalleeF->arg_size() != argvals.size()) {
    errlog("gen_call: wrong arg count %zu (expected %zu)", argvals.size(), CalleeF->arg_size());
    return nullptr;
  }

  return gs.Builder->CreateCall(CalleeF, argvals, ""/*"calltmp"*/);
}

Function* gen_funproto(GenState& gs, FunInfo& fn) {
  // Make the function type:  double(double,double) etc.
  std::vector<Type*> ints(fn.args.size(), Type::getInt32Ty(gs.ctx));
  auto returnType = Type::getInt32Ty(gs.ctx);
  FunctionType* FT = FunctionType::get(returnType, ints, false);

  Function* F = Function::Create(FT, Function::ExternalLinkage, fn.name, gs.mod.get());

  // Set names for all arguments.
  unsigned Idx = 0;
  for (auto &arg : F->args())
    arg.setName(fn.args[Idx++]);

  return F;
}

static DIType* debug_get_uint32_t(GenState& gs) {
  if (!gs.debug.uint32Type)
    gs.debug.uint32Type = gs.DBuilder->createBasicType("int", 32, dwarf::DW_ATE_unsigned);
  return gs.debug.uint32Type;
}

static DISubroutineType* debug_create_funtype(GenState& gs, u32 NumArgs, DIFile *Unit) {
  // TODO: cache/intern result
  SmallVector<Metadata*,8> EltTys;
  DIType* uint32Type = debug_get_uint32_t(gs);
  EltTys.push_back(uint32Type); // Add the result type
  for (u32 i = 0, e = NumArgs; i != e; ++i)
    EltTys.push_back(uint32Type);
  return gs.DBuilder->createSubroutineType(gs.DBuilder->getOrCreateTypeArray(EltTys));
}

static void debug_gen_fun_entry(GenState& gs, FunInfo& fn, Function* F) {
  if (!gs.DBuilder)
    return;
  // Create a subprogram DIE for this function.
  DIFile* Unit = gs.DBuilder->createFile(
    gs.debug.TheCU->getFilename(), gs.debug.TheCU->getDirectory());
  DIScope* FContext = Unit;
  u32 LineNo = 1;
  u32 ScopeLine = LineNo;
  static DISubroutineType* funtype_2xu32 = nullptr; // FIXME
  if (!funtype_2xu32)
    funtype_2xu32 = debug_create_funtype(gs, F->arg_size(), Unit);
  DISubprogram* SP = gs.DBuilder->createFunction(
    FContext,
    fn.name,
    StringRef(),
    Unit,
    LineNo,
    funtype_2xu32,
    ScopeLine,
    DINode::FlagPrototyped,
    DISubprogram::SPFlagDefinition
  );
  F->setSubprogram(SP);

  // Push the current scope.
  gs.debug.LexicalBlocks.push_back(SP);
}

static void debug_gen_fun_exit(GenState& gs) {
  if (!gs.DBuilder)
    return;
  // Pop off the lexical block for the function
  gs.debug.LexicalBlocks.pop_back();
}

Function* gen_fun(GenState& gs, FunInfo& fn) {
  // https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl03.html

  // First, check for an existing function from a previous 'extern' declaration.
  Function* TheFunction = gs.mod->getFunction(fn.name);
  if (!TheFunction)
    TheFunction = gen_funproto(gs, fn);
  if (!TheFunction)
    return nullptr;

  // generate debug info
  debug_gen_fun_entry(gs, fn, TheFunction);

  // Create a new basic block to start insertion into.
  // Note: entry BB is required, but its name can be empty.
  BasicBlock* BB = BasicBlock::Create(gs.ctx, ""/*"entry"*/, TheFunction);
  gs.Builder->SetInsertPoint(BB);

  // Record the function arguments in the NamedValues map.
  gs.NamedValues.clear();
  for (auto &Arg : TheFunction->args())
    gs.NamedValues[std::string(Arg.getName())] = &Arg;

  if (/*Value* retval = Body->codegen()*/ true) {

    // fake "body code generation"
    Value* retval;
    if (fn.name == "foo") {
      // retval = gen_floatlit(gs, 1.23);
      retval = gen_int32lit(gs, 123);
    } else {
      retval = gen_call(gs, "foo", std::vector<Value*>{
        gen_int32lit(gs, 4),
        gen_int32lit(gs, 2) } );
    }

    // Finish off the function
    gs.Builder->CreateRet(retval);

    debug_gen_fun_exit(gs);

    // Validate the generated code, checking for consistency.
    verifyFunction(*TheFunction);

    // Run the function optimizer
    if (gs.FPM)
      gs.FPM->run(*TheFunction, *gs.FAM);

    return TheFunction;
  }

  // Error reading body, remove function.
  TheFunction->eraseFromParent();

  debug_gen_fun_exit(gs);

  return nullptr;
}


// llvm_gen_module generates a LLVM IR module
static std::unique_ptr<Module> llvm_gen_module(std::string modname, LLVMContext& ctx) {
  dlog("llvm_gen_module");
  auto timestart = nanotime();
  bool include_debug_info = false;

  // GenState* _ctx = new GenState; GenState& gs = *_ctx;
  GenState gs = { .ctx = ctx, };

  // Open a new context and module
  gs.mod = std::make_unique<Module>(modname, ctx);

  // Create a new builder for the module
  gs.Builder = std::make_unique<IRBuilder<>>(ctx);

  // Add debug info
  if (include_debug_info) {
    // Add the current debug info version into the module
    gs.mod->addModuleFlag(Module::Warning, "Debug Info Version", DEBUG_METADATA_VERSION);
    // Darwin only supports dwarf2.
    if (Triple(sys::getProcessTriple()).isOSDarwin())
      gs.mod->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 2);
    gs.DBuilder = std::make_unique<DIBuilder>(*gs.mod);
    // Create the compile unit for the module.
    // Currently down as "fib.ks" as a filename since we're redirecting stdin
    // but we'd like actual source locations.
    gs.debug.TheCU = gs.DBuilder->createCompileUnit(
      dwarf::DW_LANG_C, gs.DBuilder->createFile("foo.w", "."), "Co2 Compiler", 0, "", 0);
  }

  // generate IR
  FunInfo fn1 = { .name = "foo", .args = {"x", "y"} };
  if (auto *FnIR = gen_fun(gs, fn1)) {
    // fprintf(stderr, "Generated function:");
    // FnIR->print(errs());
    // fprintf(stderr, "\n");
  }
  FunInfo fn2 = { .name = "main", .args = {} };
  if (auto *FnIR = gen_fun(gs, fn2)) {
    // fprintf(stderr, "Generated function:");
    // FnIR->print(errs());
    // fprintf(stderr, "\n");
  }

  // Finalize the debug info
  if (gs.DBuilder)
    gs.DBuilder->finalize();

  print_duration("llvm_gen_module", timestart);

  // Print out all of the generated IR
  fprintf(stderr, "—————————————————————— IR ———————————————————————\n");
  gs.mod->print(errs(), nullptr);
  fprintf(stderr, "—————————————————————————————————————————————————\n");

  return std::move(gs.mod);
}


std::unique_ptr<TargetMachine> llvm_select_target(Module& module, std::string targetTriple) {
  // // Initialize ALL targets (this causes a lot of llvm code to be included in this program)
  // InitializeAllTargetInfos();
  // InitializeAllTargets();
  // InitializeAllTargetMCs();
  // InitializeAllAsmParsers();
  // InitializeAllAsmPrinters();

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

  // retrieve target from registry
  std::string Error;
  const Target* target = TargetRegistry::lookupTarget(targetTriple, Error);

  // Print an error and exit if we couldn't find the requested target.
  // This generally occurs if we've forgotten to initialise the
  // TargetRegistry or we have a bogus target triple.
  if (!target) {
    errlog("TargetRegistry::lookupTarget error: %s", Error.c_str());
    return nullptr;
  }

  // machine features (none, for now)
  auto CPU = "generic";
  auto Features = "";

  TargetOptions opt;
  auto RM = Optional<Reloc::Model>();
  std::unique_ptr<TargetMachine> targetMachine = std::unique_ptr<TargetMachine>(
    target->createTargetMachine(targetTriple, CPU, Features, opt, RM));
  targetMachine->setO0WantsFastISel(true);

  module.setTargetTriple(targetTriple);
  module.setDataLayout(targetMachine->createDataLayout());

  return targetMachine;
}


// ld64.lld
//
// lld exe names:
//   ld.lld (Unix)
//   ld64.lld (macOS)
//   lld-link (Windows)
//   wasm-ld (WebAssembly)
//
static bool lld_link_macho(int argc, const char **argv) {
  // Note: there's both lld::mach_o and lld::macho -- the latter is a newer linker
  // which is work in progress (as of Feb 2021)
  std::vector<const char*> args(argv, argv + argc);
  // return lld::macho::link(args, can_exit_early, llvm::outs(), llvm::errs()); // WIP
  return lld::mach_o::link(args, /*can_exit_early*/false, llvm::outs(), llvm::errs());
}


// llvm_emit_ir writes LLVM IR to a file
static bool llvm_emit_ir(Module& module, const char* outfile) {
  char* error_message = NULL;
  LLVMModuleRef modref = reinterpret_cast<LLVMModuleRef>(&module);
  if (LLVMPrintModuleToFile(modref, outfile, &error_message) != 0) {
    errlog("LLVMPrintModuleToFile: %s", error_message);
    if (error_message)
      LLVMDisposeMessage(error_message);
    return false;
  }
  dlog("wrote %s", outfile);
  return true;
}

enum CoBuildType {
  CoBuildDebug,    // -O0
  CoBuildOptSmall, // -Oz
  CoBuildOpt,      // -O3
};


// llvm_emit_target applies optimizations and writes target-specific files
// Notes:
// - When LTO is enabled LLVM bitcode is written to bin_outfile rather than object MC
// - When LTO is disabled only one of asm_outfile and bin_outfile can be written, not both.
static bool llvm_emit_target(
  Module&              module,
  TargetMachine&       targetMachine,
  const char* nullable asm_outfile,
  const char* nullable bin_outfile
) {
  dlog("llvm_emit_target");

  auto timestart = nanotime();

  CoBuildType buildType = CoBuildDebug;
  bool enable_tsan = false;
  bool enable_lto  = false; // if enabled, write LLVM bitcode to bin_outfile, else object MC
  bool enable_time_report = false;

  bool is_debug = buildType == CoBuildDebug;
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
  PassBuilder passBuilder(
    /*DebugLogging*/false, &targetMachine, pipelineOpt, /*PGOOpt*/None, &instrCallbacks);
  using OptimizationLevel = typename PassBuilder::OptimizationLevel;

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
  switch (buildType) {
    case CoBuildDebug:    optLevel = OptimizationLevel::O0; break;
    case CoBuildOptSmall: optLevel = OptimizationLevel::Oz; break;
    case CoBuildOpt:      optLevel = OptimizationLevel::O3; break;
  }
  if (optLevel == OptimizationLevel::O0) {
    MPM = passBuilder.buildO0DefaultPipeline(optLevel, enable_lto);
  } else if (enable_lto) {
    MPM = passBuilder.buildLTOPreLinkDefaultPipeline(optLevel);
  } else {
    MPM = passBuilder.buildPerModuleDefaultPipeline(optLevel);
  }

  // Optimization phase
  MPM.run(module, moduleAM);

  // Machine code-generation pass
  // Unfortunately we don't have new PM for code generation
  // IMPORTANT: dest_* ostreams must be ordered before PassManager variables on stack as the
  // PassManager's destructor depends on the ostreams to still be valid.
  std::unique_ptr<raw_fd_ostream> dest_asm;
  std::unique_ptr<raw_fd_ostream> dest_bin;
  if (asm_outfile) {
    std::error_code EC;
    dest_asm.reset(new(std::nothrow) raw_fd_ostream(asm_outfile, EC, sys::fs::F_None));
    if (EC) {
      errlog("raw_fd_ostream: %s", (const char *)StringRef(EC.message()).bytes_begin());
      return false;
    }
  }
  if (bin_outfile) {
    std::error_code EC;
    dest_bin.reset(new(std::nothrow) raw_fd_ostream(bin_outfile, EC, sys::fs::F_None));
    if (EC) {
      errlog("raw_fd_ostream: %s", (const char *)StringRef(EC.message()).bytes_begin());
      return false;
    }
  }

  // Generate object MC (LTO disabled) or LLVM bitcode (LTO enabled)
  if (dest_bin) {
    legacy::PassManager codegenPM;
    codegenPM.add(createTargetTransformInfoWrapperPass(targetMachine.getTargetIRAnalysis()));
    if (!enable_lto) {
      // object machine code
      if (targetMachine.addPassesToEmitFile(codegenPM, *dest_bin, nullptr, CGFT_ObjectFile) != 0) {
        errlog("TargetMachine can't emit an object file");
        return false;
      }
    }
    codegenPM.run(module);
    if (enable_lto) {
      // LLVM bitcode
      WriteBitcodeToFile(module, *dest_bin);
    }
    dest_bin->flush();
    dlog("wrote %s", bin_outfile);
  }

  // Generate assembly code
  if (dest_asm) {
    // must use a separate PassManager when outputting both obj and asm or llvm trips an assertion
    // "LLVM ERROR: '_xyz' label emitted multiple times to assembly file"
    legacy::PassManager codegenPM;
    codegenPM.add(createTargetTransformInfoWrapperPass(targetMachine.getTargetIRAnalysis()));
    if (targetMachine.addPassesToEmitFile(codegenPM, *dest_asm, nullptr, CGFT_AssemblyFile) != 0) {
      errlog("TargetMachine can't emit an assembly file");
      return false;
    }
    codegenPM.run(module);
    dest_bin->flush();
    dlog("wrote %s", asm_outfile);
  }

  // print perf information
  if (enable_time_report)
    TimerGroup::printAll(errs());
  print_duration("llvm_emit_target", timestart);
  return true;
}


__attribute__((constructor,used)) static void llvm_cxx_init() {
  dlog("llvm_cxx_init");
  auto timestart = nanotime();

  auto ctx = std::make_unique<LLVMContext>();

  // generate IR code from AST
  auto module = llvm_gen_module("meow", *ctx);
  if (!module)
    return;

  // select target machine
  auto target = llvm_select_target(*module, sys::getDefaultTargetTriple());
  if (!target)
    return;
  dlog("target: %s", target->getTargetTriple().str().c_str());

  // generate machine code
  // if (!llvm_emit_target(*module, *target, /*"out.asm"*/nullptr, "out.o"))
  if (!llvm_emit_target(*module, *target, "out.asm", "out.o"))
    return;

  { // write LLVM IR to file
    auto timestart = nanotime();
    llvm_emit_ir(*module, "out.ll");
    print_duration("llvm_emit_ir", timestart);
  }

  // link into executable
  const char* argv[] = {
    "ld64.lld",
    "-o", "out.exe",
    "-arch", "x86_64", // not needed but avoids inference work
    "-sdk_version", "10.15", "-lsystem", "-framework", "Foundation", // required on macos
    "out.o"};
  if (lld_link_macho(countof(argv), argv)) {
    dlog("linker wrote %s", "out.exe");
  } else {
    errlog("linked failed");
  }

  print_duration("done in", timestart);
  exit(0);
}
