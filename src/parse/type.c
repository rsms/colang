#include "../coimpl.h"
#include "type.h"

// Lookup table TypeCode => string encoding char
const char _TypeCodeEncodingMap[TC_END] = {
  #define _(name, encoding, _flags) encoding,
  // IMPORTANT: order of macro invocations must match enum TypeCode
  DEF_TYPE_CODES_BASIC_PUB(_)
  DEF_TYPE_CODES_BASIC(_)
  DEF_TYPE_CODES_PUB(_)
  DEF_TYPE_CODES_ETC(_)
  #undef _
};

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


#if 0

// typeid
//
// Operations needed:
//   typeEquals(a,b) -- a and b are of the same, identical type
//   typeFitsIn(a,b) -- b is subset of a (i.e. b fits in a)
//
//   Note: We do NOT need fast indexing for switches, since a variant is required
//   for switches over variable types:
//     type Thing = Error(str) | Result(u32)
//     switch thing {
//       Error("hello") => ...
//       Error(msg) => ...
//       Result(code) => ...
//     }
//
//   But hey, maybe we do for function dispatch:
//     fun log(msg str) { ... }
//     fun log(level int, msg str) { ... }
//
//   Also, some sort of matching...
//     v = (1, 2.0, true)  // typeof(v) = (int, float, bool)
//     switch v {
//       (id, _, ok) => doThing
//            ^
//          Wildcard
//     }
//     v can also be (1, "lol", true) // => (int, str, bool)
//
// To solve for all of this we use a "type symbol" — a Sym which describes the shape of a type.
//   ((int,float),(bool,int)) = "((23)(12))"
// Syms interned: testing for equality is just a pointer equality check.
// Syms are hashed and can be stored and looked-up in a Scope very effectively.
//

// mktypestr appends a type ID string for n to s
static Str mktypestr(Str s, const Type* n) {
  if (n->kind != NBasicType && n->t.id) {
    // append n's precomputed type id. E.g. "(ii)" for the tuple "(int, int)".
    // However for basic types its faster to just use str_appendc as mktypestr is
    // never called directly for a basic type, as all basic types have precomputed TypeIDs
    // which short-circuits GetTypeID.
    return str_appendn(s, n->t.id, symlen(n->t.id));
  }

  switch (n->kind) {

    case NBasicType:
      return str_appendc(s, TypeCodeEncoding(n->t.basic.typeCode));

    case NRefType:
      s = str_appendc(s, TypeCodeEncoding(TypeCode_ref));
      return mktypestr(s, n->t.ref);

    case NArrayType:
      // TypeCode_array size "x" element_typeid
      s = str_appendc(s, TypeCodeEncoding(TypeCode_array));
      s = str_appendu64(s, (u64)n->t.array.size, 10);
      s = str_appendc(s, 'x');
      return mktypestr(s, n->t.array.subtype);

    case NTupleType:
      s = str_appendc(s, TypeCodeEncoding(TypeCode_tuple));
      for (u32 i = 0; i < n->t.tuple.a.len; i++)
        s = mktypestr(s, (Type*)n->t.tuple.a.v[i]);
      return str_appendc(s, TypeCodeEncoding(TypeCode_tupleEnd));

    case NStructType:
      s = str_appendc(s, TypeCodeEncoding(TypeCode_struct));
      for (u32 i = 0; i < n->t.struc.a.len; i++)
        s = mktypestr(s, ((Type*)n->t.struc.a.v[i])->type);
      return str_appendc(s, TypeCodeEncoding(TypeCode_structEnd));

    case NFunType:
      s = str_appendc(s, TypeCodeEncoding(TypeCode_fun));
      if (n->t.fun.params) {
        s = mktypestr(s, n->t.fun.params->type);
      } else {
        s = str_appendc(s, TypeCodeEncoding(TypeCode_nil));
      }
      if (n->t.fun.result)
        return mktypestr(s, n->t.fun.result);
      return str_appendc(s, TypeCodeEncoding(TypeCode_nil));

    default:
      panic("TODO mktypestr handle %s", NodeKindName(n->kind));
      break;
  }
  return s;
}


Type* InternASTType(Build* b, Type* t) {
  if (t->kind == NBasicType)
    return t;
  auto tid = GetTypeID(b, t);
  auto t2 = SymMapGet(&b->types, tid);
  if (t2)
    return t2;
  SymMapSet(&b->types, tid, t);
  return t;
}


// GetTypeID returns the type Sym identifying n
Sym GetTypeID(Build* b, Type* n) {
  // Note: All built-in non-generic types have predefined type ids
  if (n->t.id)
    return n->t.id;
  auto tmpstr = mktypestr(str_new(128), n);
  n->t.id = symget(b->syms, tmpstr, str_len(tmpstr));
  str_free(tmpstr);
  return n->t.id;
}


bool _TypeEquals(Build* b, Type* x, Type* y) {
  assert(x != NULL);
  assert(y != NULL);
  assertf(NodeIsType(x) || NodeIsMacroParam(x),
    "x is not a type but %s.", NodeKindName(x->kind));
  assertf(NodeIsType(y) || NodeIsMacroParam(y),
    "y is not a type but %s.", NodeKindName(x->kind));
  assert(x != y); // inline TypeEquals func avoids this
  if (x->kind != y->kind)
    return false;
  if (x->kind == NBasicType)
    return x->t.id == y->t.id;
  return GetTypeID(b, x) == GetTypeID(b, y);
}

#endif
