#include "../common.h"
#include "llvm.h"
#include "llvm-includes.hh"
/*
using llvm::DataLayout;
using llvm::LLVMContext;
using llvm::JITEvaluatedSymbol;
using llvm::StringRef;
using llvm::SectionMemoryManager;

using namespace llvm::orc;


class CoJITImpl {
public:

  ExecutionSession         ES;
  RTDyldObjectLinkingLayer objectLayer;
  IRCompileLayer           compileLayer;

  DataLayout        DL;
  MangleAndInterner mangle;
  ThreadSafeContext ctx;

  CoJITImpl(JITTargetMachineBuilder JTMB, DataLayout DL)
      : objectLayer(ES, []() { return std::make_unique<SectionMemoryManager>(); })
      , compileLayer(ES, objectLayer, std::make_unique<ConcurrentIRCompiler>(std::move(JTMB)))
      , DL(std::move(DL)), mangle(ES, this->DL)
      , ctx(std::make_unique<LLVMContext>())
  {
    ES.getMainJITDylib().addGenerator(
      cantFail(DynamicLibrarySearchGenerator::GetForCurrentProcess(
        DL.getGlobalPrefix())));
  }

  static llvm::Expected<std::unique_ptr<CoJITImpl>> Create() {
    auto JTMB = JITTargetMachineBuilder::detectHost();

    if (!JTMB)
      return JTMB.takeError();

    auto DL = JTMB->getDefaultDataLayoutForTarget();
    if (!DL)
      return DL.takeError();

    return std::make_unique<CoJITImpl>(std::move(*JTMB), std::move(*DL));
  }

  const DataLayout& getDataLayout() const { return DL; }

  LLVMContext& getContext() { return *ctx.getContext(); }

  // void addModule(std::unique_ptr<Module> M) {
  //   cantFail(compileLayer.add(ES.getMainJITDylib(),
  //                             ThreadSafeModule(std::move(M), ctx)));
  // }

  llvm::Expected<JITEvaluatedSymbol> lookup(StringRef name) {
    return ES.lookup({ &ES.getMainJITDylib() }, mangle(name.str()));
  }
};


static void jit_init_llvm() {
  static r_sync_once_flag once;
  r_sync_once(&once, {
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
  });
}


CoJIT* jit_create(Error* err) {
  jit_init_llvm();
  auto j = new CoJITImpl();
  return reinterpret_cast<CoJIT*>(j);
}


void jit_dispose(CoJIT* jit) {
  auto j = reinterpret_cast<CoJITImpl*>(jit);
  delete j;
}


void jit_addmodule(CoJIT* jit, LLVMModuleRef M) {
  CoJITImpl& j = *reinterpret_cast<CoJITImpl*>(jit);
  llvm::Module* module = llvm::unwrap(M);
  cantFail(j.compileLayer.add(
    j.ES.getMainJITDylib(),
    ThreadSafeModule(std::move(module), j.ctx)));
}
*/