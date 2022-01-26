#include "parse.h"

//BEGIN GENERATED CODE by ast_gen.py

const char* NodeKindName(NodeKind k) {
  // kNodeNameTable[NodeKind] => const char* name
  static const char* const kNodeNameTable[30] = {
    "Bad", "Pkg", "File", "Comment", "BoolLit", "IntLit", "FloatLit", "StrLit",
    "Nil", "Id", "BinOp", "UnaryOp", "Array", "Fun", "Macro", "Call",
    "TypeCast", "Field", "Var", "Ref", "NamedVal", "Selector", "Index", "Slice",
    "If", "BasicType", "ArrayType", "TupleType", "StructType", "FunType"
  };
  return k < 30 ? kNodeNameTable[k] : "?";
}

//END GENERATED CODE
