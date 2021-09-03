#include "libbn.h"
// #include <shell-interface.h> // for ShellExternalInterface
// #include <wasm-interpreter.h> // in deps/binaryen/src

#ifdef CO_BN_STATICLIB
int cobn_dummy = 1; // silence linker warning about no exported symbols
#endif

// using namespace wasm;

// struct ExtInterface : ModuleInstanceBase::ExternalInterface {
//   virtual void init(Module& wasm, SubType& instance) override {
//     dlog("ExtInterface.init");
//   }
//   virtual void importGlobals(GlobalManager& globals, Module& wasm) override {
//     dlog("ExtInterface.importGlobals");
//   }
//   virtual Literals callImport(Function* import, LiteralList& arguments) override {
//     dlog("ExtInterface.callImport");
//   }
//   virtual Literals callTable(Name tableName, Index index, Signature sig, LiteralList& arguments, Type result, SubType& instance) override {
//     dlog("ExtInterface.callTable");
//   }
//   virtual bool growMemory(Address oldSize, Address newSize) override {
//     dlog("ExtInterface.growMemory");
//   }
//   virtual void trap(const char* why) override {
//     dlog("ExtInterface.trap");
//   }
//   virtual void hostLimit(const char* why) override {
//     dlog("ExtInterface.hostLimit");
//   }
//   virtual void throwException(const WasmException& exn) override {
//     dlog("ExtInterface.throwException");
//   }
// };

// int bn_mod_interpret(BinaryenModuleRef module) {
//   ShellExternalInterface interface;
//   ModuleInstance instance(*(Module*)module, &interface, {});
//   return 0;
// }
