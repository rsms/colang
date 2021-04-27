#include <rbase/rbase.h>
#include "llvm.h"

__attribute__((constructor,used)) static void llvm_init() {
  dlog("llvm_init");
}
