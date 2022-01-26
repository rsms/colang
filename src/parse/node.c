#include "parse.h"

const char* TypeKindName(TypeKind tk) {
  switch ((enum TypeKind)TF_Kind(tk)) {
    case TF_KindVoid:    return "void";
    case TF_KindBool:    return "boolean";
    case TF_KindInt:     return "integer";
    case TF_KindF16:     return "16-bit floating-point number";
    case TF_KindF32:     return "32-bit floating-point number";
    case TF_KindF64:     return "64-bit floating-point number";
    case TF_KindFunc:    return "function";
    case TF_KindStruct:  return "struct";
    case TF_KindArray:   return "array";
    case TF_KindPointer: return "pointer";
    case TF_KindVector:  return "vector";
  }
  return "?";
}
