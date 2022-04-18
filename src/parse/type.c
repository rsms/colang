// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "parse.h"


// Lookup table TypeCode => string encoding char
const char _TypeCodeEncodingMap[TC_END] = {
  #define _(name, encoding, _flags) encoding,
  // IMPORTANT: order of macro invocations must match enum TypeCode
  DEF_TYPE_CODES_BASIC_PUB(_)
  0, // TC_NUM_END
  DEF_TYPE_CODES_BASIC(_)
  0, // TC_BASIC_END
  DEF_TYPE_CODES_PUB(_)
  DEF_TYPE_CODES_ETC(_)
  #undef _
};

const char* TypeKindName(TypeKind tk) {
  switch ((enum TypeKind)TF_Kind(tk)) {
    case TF_KindVoid:     return "void";
    case TF_KindBool:     return "boolean";
    case TF_KindInt:      return "integer";
    case TF_KindF16:      return "16-bit floating-point number";
    case TF_KindF32:      return "32-bit floating-point number";
    case TF_KindF64:      return "64-bit floating-point number";
    case TF_KindF128:     return "128-bit floating-point number";
    case TF_KindFunc:     return "function";
    case TF_KindStruct:   return "struct";
    case TF_KindArray:    return "array";
    case TF_KindPointer:  return "pointer";
    case TF_KindVector:   return "vector";
    case TF_KindType:     return "type";
    case TF_KindTemplate: return "template";
  }
  return "?";
}
