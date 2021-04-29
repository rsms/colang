#include <rbase/rbase.h>
#include "types.h"

// Lookup table TypeCode => string encoding char
const char TypeCodeEncoding[TypeCode_MAX] = {
  #define I_ENUM(name, encoding, _flags) encoding,
  TYPE_CODES(I_ENUM)
  #undef  I_ENUM
};


// #if DEBUG
const char* _TypeCodeName[TypeCode_MAX] = {
  #define I_ENUM(name, _encoding, _flags) #name,
  TYPE_CODES(I_ENUM)
  #undef  I_ENUM
};


const TypeCodeFlag TypeCodeFlagMap[TypeCode_MAX] = {
  #define I_ENUM(_name, _encoding, flags) flags,
  TYPE_CODES(I_ENUM)
  #undef  I_ENUM
};

const char* CTypeName(CType ct) {
  switch (ct) {
  case CType_INVALID: return "INVALID";
  case CType_int:     return "int";
  case CType_rune:    return "rune";
  case CType_float:   return "float";
  case CType_str:     return "str";
  case CType_bool:    return "bool";
  case CType_nil:     return "nil";
  }
  return "?";
}


// const char* TypeCodeName(TypeCode tc) {
//   assert(tc > 0 && tc < TypeCode_MAX);
//   return _TypeCodeName[tc];
// }
// #else
//   // compact names where a string is formed from encoding chars + sentinels bytes.
//   // E.g. "b\01\02\03\04\05\06\07\08\0f\0F\0..." Index is *2 that of TypeCode.
//   static const char _TypeCodeName[TypeCode_MAX * 2] = {
//     #define I_ENUM(_, enc) enc, 0,
//     TYPE_CODES(I_ENUM)
//     #undef  I_ENUM
//   };
//   const char* TypeCodeName(TypeCode tc) {
//     assert(tc > 0 && tc < TypeCode_MAX);
//     return &_TypeCodeName[tc * 2];
//   }
// #endif
