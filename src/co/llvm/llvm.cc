#include "llvm.hh"
#include "lld/Common/Driver.h"

using namespace llvm;
// using namespace llvm::sys;

struct LLVMCtx {
  std::unique_ptr<LLVMContext>  TheContext;
  std::unique_ptr<Module>       TheModule;
  std::unique_ptr<IRBuilder<>>  Builder;
  std::map<std::string, Value*> NamedValues;
};

struct FunInfo {
  std::string              name;
  std::vector<std::string> args;
};

Value* gen_floatlit(LLVMCtx* ctx, double v) {
  return ConstantFP::get(*ctx->TheContext, APFloat(v));
}

Value* gen_int32lit(LLVMCtx* ctx, int32_t v) {
  return ConstantInt::get(*ctx->TheContext, APInt(32, v, /*isSigned*/true));
}

Function* gen_funproto(LLVMCtx* ctx, FunInfo& fn) {
  // Make the function type:  double(double,double) etc.
  std::vector<Type*> ints(fn.args.size(), Type::getInt32Ty(*ctx->TheContext));
  auto returnType = Type::getInt32Ty(*ctx->TheContext);
  FunctionType* FT = FunctionType::get(returnType, ints, false);

  Function* F = Function::Create(FT, Function::ExternalLinkage, fn.name, ctx->TheModule.get());

  // Set names for all arguments.
  unsigned Idx = 0;
  for (auto &arg : F->args())
    arg.setName(fn.args[Idx++]);

  return F;
}

Function* gen_fun(LLVMCtx* ctx, FunInfo& fn) {
  // https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl03.html

  // First, check for an existing function from a previous 'extern' declaration.
  Function* TheFunction = ctx->TheModule->getFunction(fn.name);

  if (!TheFunction)
    TheFunction = gen_funproto(ctx, fn);

  if (!TheFunction)
    return nullptr;

  // Create a new basic block to start insertion into.
  // Note: entry BB is required, but its name can be empty.
  BasicBlock* BB = BasicBlock::Create(*ctx->TheContext, ""/*"entry"*/, TheFunction);
  ctx->Builder->SetInsertPoint(BB);

  // Record the function arguments in the NamedValues map.
  ctx->NamedValues.clear();
  for (auto &Arg : TheFunction->args())
    ctx->NamedValues[std::string(Arg.getName())] = &Arg;

  if (/*Value *RetVal = Body->codegen()*/ true) {
    // Finish off the function.
    // Value* RetVal = gen_floatlit(ctx, 1.23);
    Value* RetVal = gen_int32lit(ctx, 123);
    ctx->Builder->CreateRet(RetVal);

    // Validate the generated code, checking for consistency.
    verifyFunction(*TheFunction);

    return TheFunction;
  }

  // Error reading body, remove function.
  TheFunction->eraseFromParent();
  return nullptr;
}


#define GENERATE_DEBUG_INFO
#define GENERATE_MACHINE_OBJECT

#ifdef GENERATE_DEBUG_INFO
// https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl09.html
struct DebugInfo {
  DICompileUnit*        TheCU;
  DIType*               DblTy;
  std::vector<DIScope*> LexicalBlocks;
  // void emitLocation(ExprAST *AST);
  // DIType *getDoubleTy();
} KSDbgInfo;
#endif /*GENERATE_DEBUG_INFO*/



#ifdef GENERATE_MACHINE_OBJECT
// https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl08.html
static bool gen_machine_obj(LLVMCtx& ctx, std::string outfile) {
  // Initialize ALL targets (this causes a lot of llvm code to be included in this program)
  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  // select target machine, OS and environment
  auto TargetTriple = sys::getDefaultTargetTriple();
  ctx.TheModule->setTargetTriple(TargetTriple);

  // retrieve target from registry
  std::string Error;
  auto Target = TargetRegistry::lookupTarget(TargetTriple, Error);

  // Print an error and exit if we couldn't find the requested target.
  // This generally occurs if we've forgotten to initialise the
  // TargetRegistry or we have a bogus target triple.
  if (!Target) {
    errlog("TargetRegistry::lookupTarget error: %s", Error.c_str());
    return false;
  }

  // machine features (none, for now)
  auto CPU = "generic";
  auto Features = "";

  TargetOptions opt;
  auto RM = Optional<Reloc::Model>();
  auto TheTargetMachine = Target->createTargetMachine(TargetTriple, CPU, Features, opt, RM);
  ctx.TheModule->setDataLayout(TheTargetMachine->createDataLayout());

  std::error_code EC;
  raw_fd_ostream dest(outfile, EC, sys::fs::OF_None);

  if (EC) {
    errlog("Could not open file: %s", EC.message().c_str());
    return false;
  }

  legacy::PassManager pass;
  auto FileType = CGFT_ObjectFile;

  if (TheTargetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
    errs() << "TheTargetMachine can't emit a file of this type";
    return false;
  }

  pass.run(*ctx.TheModule);
  dest.flush();

  outs() << "Wrote " << outfile << "\n";
  return true;
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

#endif /*GENERATE_MACHINE_OBJECT*/


__attribute__((constructor,used)) static void init() {
  dlog("llvm");

  auto timestart = nanotime();

  LLVMCtx ctx;

  // Open a new context and module
  ctx.TheContext = std::make_unique<LLVMContext>();
  ctx.TheModule = std::make_unique<Module>("meow", *ctx.TheContext);

  // Create a new builder for the module
  ctx.Builder = std::make_unique<IRBuilder<>>(*ctx.TheContext);

  #ifdef GENERATE_DEBUG_INFO
    // Add the current debug info version into the module.
    ctx.TheModule->addModuleFlag(Module::Warning, "Debug Info Version", DEBUG_METADATA_VERSION);
    // Darwin only supports dwarf2.
    if (Triple(sys::getProcessTriple()).isOSDarwin())
      ctx.TheModule->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 2);
    std::unique_ptr<DIBuilder> DBuilder = std::make_unique<DIBuilder>(*ctx.TheModule);
    // Create the compile unit for the module.
    // Currently down as "fib.ks" as a filename since we're redirecting stdin
    // but we'd like actual source locations.
    KSDbgInfo.TheCU = DBuilder->createCompileUnit(
      dwarf::DW_LANG_C, DBuilder->createFile("foo.w", "."), "Co2 Compiler", 0, "", 0);
  #endif

  // generate IR
  FunInfo fn1 = { .name = "foo", .args = {"x", "y"} };
  if (auto *FnIR = gen_fun(&ctx, fn1)) {
    fprintf(stderr, "Generated function:");
    FnIR->print(errs());
    fprintf(stderr, "\n");
  }
  FunInfo fn2 = { .name = "main", .args = {} };
  if (auto *FnIR = gen_fun(&ctx, fn2)) {
    fprintf(stderr, "Generated function:");
    FnIR->print(errs());
    fprintf(stderr, "\n");
  }

  #ifdef GENERATE_DEBUG_INFO
    // Finalize the debug info
    DBuilder->finalize();
  #endif

  // Print out all of the generated code
  fprintf(stderr, "—————————————————————— IR ———————————————————————\n");
  ctx.TheModule->print(errs(), nullptr);

  #ifdef GENERATE_MACHINE_OBJECT
  // assemble into object file
  if (!gen_machine_obj(ctx, "output.o"))
    return;

  // link into executable
  const char* argv[] = {
    "ld64.lld",
    "-o", "output.exe",
    "-arch", "x86_64", // not needed but avoids inference work
    "-sdk_version", "10.15", "-lsystem", "-framework", "Foundation", // required on macos
    "output.o"};
  if (lld_link_macho(countof(argv), argv)) {
    dlog("linker wrote %s", "output.exe");
  } else {
    errlog("linked failed");
  }
  #endif /*GENERATE_MACHINE_OBJECT*/


  // print how much (real) time we spent
  auto timeend = nanotime();
  char abuf[40];
  auto buflen = fmtduration(abuf, countof(abuf), timeend - timestart);
  printf("done in %.*s\n", buflen, abuf);

  // exit(0);
}
