#include <rbase/rbase.h>
#include "llvm.h"
#include "../parse/parse.h"

#include <llvm-c/Transforms/AggressiveInstCombine.h>
#include <llvm-c/Transforms/Scalar.h>

// make the code more readable by using short name aliases
typedef LLVMValueRef  Value;
typedef LLVMTypeRef   Type;

// B is internal data used during IR construction
typedef struct B {
  LLVMContextRef  ctx;
  LLVMModuleRef   mod;
  LLVMBuilderRef  builder;

  //// debug info
  //std::unique_ptr<DIBuilder>   DBuilder;
  //DebugInfo                    debug;

  // optimization
  LLVMPassManagerRef FPM; // function pass manager

  // target
  LLVMTargetMachineRef target;

  // constants
  LLVMTypeRef t_i32;

} B;


static void print_duration(const char* message, u64 timestart) {
  auto timeend = nanotime();
  char abuf[40];
  auto buflen = fmtduration(abuf, countof(abuf), timeend - timestart);
  fprintf(stderr, "%s %.*s\n", message, buflen, abuf);
  fflush(stderr);
}

static Value build_call(B* b, const char* calleeName) {
  // look up the name in the module table
  Value callee = LLVMGetNamedFunction(b->mod, calleeName);
  if (!callee) {
    errlog("unknown function %s", calleeName);
    return NULL;
  }
  // check argument count
  Value args[] = {
    LLVMConstInt(b->t_i32, 3, /*signext*/false),
    LLVMConstInt(b->t_i32, 4, /*signext*/false),
  };
  u32 nargs = (u32)countof(args);
  if (LLVMCountParams(callee) != nargs) {
    errlog("wrong number of arguments: %u (expected %u)", nargs, LLVMCountParams(callee));
    return NULL;
  }
  return LLVMBuildCall(b->builder, callee, args, nargs, "");
}

static Type build_funtype(B* b) {
  Type returnType = b->t_i32;
  Type paramTypes[] = {b->t_i32, b->t_i32};
  LLVMBool isVarArg = false;
  return LLVMFunctionType(returnType, paramTypes, countof(paramTypes), isVarArg);
}

static Value build_funproto(B* b, const char* name) {
  Type fnt = build_funtype(b);
  Value fn = LLVMAddFunction(b->mod, name, fnt);
  // set argument names (for debugging)
  Value arg1 = LLVMGetParam(fn, 0);
  Value arg2 = LLVMGetParam(fn, 1);
  LLVMSetValueName2(arg1, "x", 1);
  LLVMSetValueName2(arg2, "y", 1);
  return fn;
}

static Value build_fun(B* b, const char* name) {
  // function prototype
  Value fn = build_funproto(b, name);

  // Create a new basic block to start insertion into.
  // Note: entry BB is required, but its name can be empty.
  LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(b->ctx, fn, ""/*"entry"*/);
  LLVMPositionBuilderAtEnd(b->builder, bb);
  // gs.Builder->SetInsertPoint(bb);

  // hard-coded bodies
  if (strcmp(name, "main") == 0) {
    Value v = build_call(b, "foo");
    LLVMBuildRet(b->builder, v);
  } else {
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    // (return (+ (+ arg1 arg2) (+ arg1 arg2)))
    Value add1 = LLVMBuildAdd(b->builder, LLVMGetParam(fn, 0), LLVMGetParam(fn, 1), "");
    Value add2 = LLVMBuildAdd(b->builder, LLVMGetParam(fn, 0), LLVMGetParam(fn, 1), "");
    Value v = LLVMBuildAdd(b->builder, add1, add2, "");
    LLVMBuildRet(b->builder, v);
  }

  // optimize
  if (b->FPM)
    LLVMRunFunctionPassManager(b->FPM, fn);

  // Note: On error, erase the function and return NULL:
  // LLVMEraseGlobalIFunc(fn);
  // return NULL;

  // LLVMDumpValue(fn);
  return fn;
}

static void build_module(LLVMModuleRef mod) {
  LLVMContextRef ctx = LLVMGetModuleContext(mod);
  B build = {
    .ctx = ctx,
    .mod = mod,
    .builder = LLVMCreateBuilderInContext(ctx),

    // FPM: Apply per-function optimizations. Set to NULL to disable.
    // Really only useful for JIT as for assembly to asm, obj or bc we apply module-wide opt.
    // .FPM = LLVMCreateFunctionPassManagerForModule(mod),
    .FPM = NULL,

    // constants
    .t_i32 = LLVMInt32TypeInContext(ctx), // note: no disposal needed of built-in types
  };
  B* b = &build;

  // initialize function pass manager (optimize)
  if (b->FPM) {
    // add optimization passes
    LLVMAddInstructionCombiningPass(b->FPM);
    LLVMAddReassociatePass(b->FPM);
    LLVMAddDCEPass(b->FPM);
    LLVMAddGVNPass(b->FPM);
    LLVMAddCFGSimplificationPass(b->FPM);
    // initialize FPM
    LLVMInitializeFunctionPassManager(b->FPM);
  }

  // build a function
  build_fun(b, "foo");
  build_fun(b, "main");

  // verify IR
  #ifdef DEBUG
    char* errmsg;
    bool ok = LLVMVerifyModule(b->mod, LLVMPrintMessageAction, &errmsg) == 0;
    if (!ok) {
      errlog("LLVMVerifyModule: %s", errmsg);
      LLVMDisposeMessage(errmsg);
      goto finish;
    }
  #endif

  // finalize all function passes scheduled in the function pass
  if (b->FPM)
    LLVMFinalizeFunctionPassManager(b->FPM);

  LLVMDumpModule(b->mod);

finish:
  if (b->FPM)
    LLVMDisposePassManager(b->FPM);
  LLVMDisposeBuilder(b->builder);
}


static LLVMTargetRef select_target(const char* triple) {
  // select target
  char* errmsg;
  LLVMTargetRef target;
  if (LLVMGetTargetFromTriple(triple, &target, &errmsg) != 0) {
    // error
    errlog("LLVMGetTargetFromTriple: %s", errmsg);
    LLVMDisposeMessage(errmsg);
    target = NULL;
  } else {
    const char* name = LLVMGetTargetName(target);
    const char* description = LLVMGetTargetDescription(target);
    const char* jit = LLVMTargetHasJIT(target) ? " jit" : "";
    const char* mc = LLVMTargetHasTargetMachine(target) ? " mc" : "";
    const char* _asm = LLVMTargetHasAsmBackend(target) ? " asm" : "";
    dlog("selected target: %s (%s) [abilities:%s%s%s]", name, description, jit, mc, _asm);
  }
  return target;
}

static LLVMTargetMachineRef select_target_machine(
  LLVMTargetRef       target,
  const char*         triple,
  LLVMCodeGenOptLevel optLevel,
  LLVMCodeModel       codeModel)
{
  if (!target)
    return NULL;

  const char* CPU = "";      // "" for generic
  const char* features = ""; // "" for none

  // select host CPU and features (NOT PORTABLE!) when optimizing
  char* hostCPUName = NULL;
  char* hostFeatures = NULL;
  if (optLevel != LLVMCodeGenLevelNone) {
    hostCPUName = LLVMGetHostCPUName();
    hostFeatures = LLVMGetHostCPUFeatures();
    CPU = hostCPUName;
    features = hostFeatures;
  }

  LLVMTargetMachineRef targetMachine =
    LLVMCreateTargetMachine(target, triple, CPU, features, optLevel, LLVMRelocStatic, codeModel);
  if (!targetMachine) {
    errlog("LLVMCreateTargetMachine failed");
    return NULL;
  } else {
    char* triple1 = LLVMGetTargetMachineTriple(targetMachine);
    dlog("selected target machine: %s", triple1);
    LLVMDisposeMessage(triple1);
  }
  if (hostCPUName) {
    LLVMDisposeMessage(hostCPUName);
    LLVMDisposeMessage(hostFeatures);
  }
  return targetMachine;
}

typedef struct StrLink {
  struct StrLink* next;
  Str             str;
} StrLink;

static Str strlink_append(StrLink** head, Str str) {
  auto sl = memalloct(NULL, StrLink);
  sl->str = str;
  sl->next = *head;
  *head = sl;
  return str;
}

static StrLink* nullable strlink_free(StrLink* nullable n) {
  while (n) {
    str_free(n->str);
    auto next = n->next;
    memfree(NULL, n);
    n = next;
  }
  return NULL;
}

// lld_link is a high-level interface to the lld_link_* functions
static bool lld_link(
  const char* targetTriple,
  CoOptType opt,
  const char* exefile,
  u32 objfilesc, const char** objfilesv,
  char** errmsg)
{
  bool ok = false;
  StrLink* tmpbufs = NULL;

  CoLLVMArch         arch_type;
  CoLLVMVendor       vendor_type;
  CoLLVMOS           os_type;
  CoLLVMEnvironment  environ_type;
  CoLLVMObjectFormat oformat;
  llvm_triple_info(targetTriple, &arch_type, &vendor_type, &os_type, &environ_type, &oformat);

  // select link function
  bool(*linkfn)(int argc, const char** argv, char** errmsg);
  switch (oformat) {
    case CoLLVMObjectFormat_COFF:  linkfn = lld_link_coff; break; // lld-link
    case CoLLVMObjectFormat_ELF:   linkfn = lld_link_elf; break; // ld.lld
    case CoLLVMObjectFormat_MachO: linkfn = lld_link_macho; break; // ld64.lld
    case CoLLVMObjectFormat_Wasm:  linkfn = lld_link_wasm; break; // wasm-ld
    case CoLLVMObjectFormat_GOFF:  // ?
    case CoLLVMObjectFormat_XCOFF: // ?
    case CoLLVMObjectFormat_unknown:
      *errmsg = LLVMCreateMessage("linking not supported for provided target");
      goto end;
  }

  // arguments to linker function
  const u32 max_addl_args = 32; // max argc we add in addition to objfilesc
  u32 argc = 0;
  const char** argv = memalloc_raw(NULL, sizeof(void*) * (max_addl_args + objfilesc));

  // common arguments accepted by all lld flavors
  // (See their respective CLI help output, e.g. deps/llvm/bin/ld.lld -help)
  if (oformat == CoLLVMObjectFormat_COFF) {
    // windows-style "/flag"
    argv[argc++] = strlink_append(&tmpbufs, str_fmt("/out:%s", exefile));
    // TODO consider adding "/machine:" which seems similar to "-arch"
  } else {
    // rest of the world "-flag"
    argv[argc++] = "-o"; argv[argc++] = exefile;
    argv[argc++] = "-arch"; argv[argc++] = CoLLVMArch_name(arch_type);
    argv[argc++] = "-static";
  }

  // set linker-flavor specific arguments
  switch (oformat) {
    case CoLLVMObjectFormat_COFF: // lld-link
      break;
    case CoLLVMObjectFormat_ELF: // ld.lld
    case CoLLVMObjectFormat_Wasm: // wasm-ld
      argv[argc++] = "--no-pie";
      argv[argc++] = opt == CoOptNone ? "--lto-O0" : "--lto-O3";
      break;
    case CoLLVMObjectFormat_MachO: // ld64.lld
      argv[argc++] = "-no_pie";
      if (opt != CoOptNone) {
        // optimize
        argv[argc++] = "-dead_strip"; // Remove unreference code and data
        // TODO: look into -mllvm "Options to pass to LLVM during LTO"
      }
      break;
    case CoLLVMObjectFormat_GOFF:  // ?
    case CoLLVMObjectFormat_XCOFF: // ?
    case CoLLVMObjectFormat_unknown:
      *errmsg = LLVMCreateMessage("linking not supported for provided target");
      goto end;
  }

  // OS-specific arguments
  switch (os_type) {
    case CoLLVMOS_Darwin:
    case CoLLVMOS_MacOSX:
      argv[argc++] = "-sdk_version"; argv[argc++] = "10.15";
      argv[argc++] = "-lsystem"; // macOS's "syscall API"
      // argv[argc++] = "-framework"; argv[argc++] = "Foundation";
      break;
    case CoLLVMOS_IOS:
    case CoLLVMOS_TvOS:
    case CoLLVMOS_WatchOS: {
      // TODO
      CoLLVMVersionTuple minver;
      llvm_triple_min_version(targetTriple, &minver);
      dlog("minver: %d, %d, %d, %d", minver.major, minver.minor, minver.subminor, minver.build);
      // + arg "-ios_version_min" ...
      break;
    }
    default:
      break;
  }

  // add input arguments
  for (u32 i = 0; i < objfilesc; i++) {
    argv[argc++] = objfilesv[i];
  }

  ok = linkfn(argc, argv, errmsg);
  memfree(NULL, argv);
  if (ok) {
    // link function always sets errmsg
    size_t errlen = strlen(*errmsg);
    // print linker warnings
    if (errlen > 0)
      fwrite(*errmsg, errlen, 1, stderr);
    LLVMDisposeMessage(*errmsg);
    *errmsg = NULL;
  }

end:
  strlink_free(tmpbufs);
  return ok;
}


bool llvm_build_and_emit(Build* build, const char* triple) {
  dlog("llvm_build_and_emit");
  bool ok = false;
  auto timestart = nanotime();

  LLVMContextRef ctx = LLVMContextCreate();
  LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("hello", ctx);

  build_module(mod);

  // select target and emit machine code
  const char* hostTriple = llvm_init_targets();
  if (!triple)
    triple = hostTriple; // default to host
  LLVMTargetRef target = select_target(triple);
  LLVMCodeGenOptLevel optLevel =
    (build->opt == CoOptNone ? LLVMCodeGenLevelNone : LLVMCodeGenLevelAggressive);
  LLVMCodeModel codeModel =
    (build->opt == CoOptSmall ? LLVMCodeModelSmall : LLVMCodeModelDefault);

  // LLVMCodeGenOptLevel optLevel = LLVMCodeGenLevelAggressive;
  LLVMTargetMachineRef targetm = select_target_machine(target, triple, optLevel, codeModel);
  if (!targetm)
    goto end;

  // set target
  LLVMSetTarget(mod, triple);
  LLVMTargetDataRef dataLayout = LLVMCreateTargetDataLayout(targetm);
  LLVMSetModuleDataLayout(mod, dataLayout);

  char* errmsg;

  // optimize module
  bool enable_tsan = false;
  bool enable_lto = false; // if enabled, write LLVM bitcode to bin_outfile, else object MC
  auto opt_timestart = nanotime();
  if (!llvm_optmod(mod, targetm, build->opt, enable_tsan, enable_lto, &errmsg)) {
    errlog("llvm_optmod: %s", errmsg);
    LLVMDisposeMessage(errmsg);
    goto end;
  }
  print_duration("llvm_optmod", opt_timestart);

  // emit
  const char* obj_file = "out1.o";
  const char* asm_file = "out1.asm";
  const char* bc_file  = "out1.bc";
  const char* ir_file  = "out1.ll";
  const char* exe_file = "out1.exe";

  // emit machine code (object)
  if (obj_file) {
    auto timestart = nanotime();
    if (!llvm_emit_mc(mod, targetm, LLVMObjectFile, obj_file, &errmsg)) {
      errlog("llvm_emit_mc: %s", errmsg);
      LLVMDisposeMessage(errmsg);
      obj_file = NULL; // skip linking
    } else {
      dlog("wrote %s", obj_file);
    }
    print_duration("emit_mc obj", timestart);
  }

  // emit machine code (assembly)
  if (asm_file) {
    double timestart = nanotime();
    if (!llvm_emit_mc(mod, targetm, LLVMAssemblyFile, asm_file, &errmsg)) {
      errlog("llvm_emit_mc: %s", errmsg);
      LLVMDisposeMessage(errmsg);
    } else {
      dlog("wrote %s", asm_file);
    }
    print_duration("emit_mc asm", timestart);
  }

  // emit LLVM bitcode
  if (bc_file) {
    double timestart = nanotime();
    if (!llvm_emit_bc(mod, bc_file, &errmsg)) {
      errlog("llvm_emit_bc: %s", errmsg);
      LLVMDisposeMessage(errmsg);
    } else {
      dlog("wrote %s", bc_file);
    }
    print_duration("llvm_emit_bc", timestart);
  }

  // emit LLVM IR
  if (ir_file) {
    double timestart = nanotime();
    if (!llvm_emit_ir(mod, ir_file, &errmsg)) {
      errlog("llvm_emit_ir: %s", errmsg);
      LLVMDisposeMessage(errmsg);
    } else {
      dlog("wrote %s", ir_file);
    }
    print_duration("llvm_emit_ir", timestart);
  }

  // link executable
  if (exe_file && obj_file) {
    double timestart = nanotime();
    const char* inputv[] = { obj_file };
    if (!lld_link(triple, build->opt, exe_file, countof(inputv), inputv, &errmsg)) {
      errlog("lld_link: %s", errmsg);
      LLVMDisposeMessage(errmsg);
    } else {
      dlog("wrote %s", exe_file);
    }
    // const char* argv[] = {
    //   "-o", exe_file,
    //   "-arch", "x86_64", // not needed but avoids inference work
    //   "-sdk_version", "10.15", "-lsystem", "-framework", "Foundation", // required on macos
    //   obj_file,
    // };
    // if (!lld_link_macho(countof(argv), argv, /*can_exit_early*/false)) {
    //   errlog("lld_link_macho failed");
    // } else {
    //   dlog("wrote %s", exe_file);
    // }
    print_duration("link", timestart);
  }

  ok = true;

end:
  LLVMDisposeModule(mod);
  LLVMContextDispose(ctx);
  print_duration("llvm_build_and_emit", timestart);
  return ok;
}

// __attribute__((constructor,used)) static void llvm_init() {
//   // optimization level
//   Build build = {
//     .opt = CoOptAggressive,
//   };
//   if (!llvm_build_and_emit(&build, /*target=host*/NULL)) {
//     //
//   }
//   // exit(0);
// }
