#include "../coimpl.h"
#include "../sbuf.h"
#include "typeid.h"


// typeid
//
// Operations needed:
//   type_equals(a,b)  -- a and b are of the same, identical type
//   type_fits_in(a,b) -- b is subset of a (i.e. b fits in a)
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


static void typeid_append(SBuf* s, const Type* t) {
  if (is_BasicTypeNode(t))
    return sbuf_appendc(s, TypeCodeEncoding(as_BasicTypeNode(t)->typecode));

  if (!t->tid) switch (t->kind) {
    case NAliasType:
      MUSTTAIL return typeid_append(s, as_AliasTypeNode(t)->type);

    case NRefType:
      sbuf_appendc(s, TypeCodeEncoding(TC_ref));
      MUSTTAIL return typeid_append(s, assertnotnull(as_RefTypeNode(t)->elem));

    case NArrayType:
      sbuf_appendc(s, TypeCodeEncoding(TC_array));
      sbuf_appendu32(s, as_ArrayTypeNode(t)->size, 10);
      sbuf_appendc(s, TypeCodeEncoding(TC_arrayEnd));
      MUSTTAIL return typeid_append(s, assertnotnull(as_ArrayTypeNode(t)->elem));

    case NTupleType:
      sbuf_appendc(s, TypeCodeEncoding(TC_tuple));
      for (u32 i = 0; i < as_TupleTypeNode(t)->a.len; i++)
        typeid_append(s, as_TupleTypeNode(t)->a.v[i]);
      return sbuf_appendc(s, TypeCodeEncoding(TC_tupleEnd));

    case NStructType:
      sbuf_appendc(s, TypeCodeEncoding(TC_struct));
      for (u32 i = 0; i < as_StructTypeNode(t)->fields.len; i++) {
        FieldNode* field = as_StructTypeNode(t)->fields.v[i];
        typeid_append(s, assertnotnull(field->type));
      }
      return sbuf_appendc(s, TypeCodeEncoding(TC_structEnd));

    case NFunType: {
      auto ft = as_FunTypeNode(t);
      sbuf_appendc(s, TypeCodeEncoding(TC_fun));
      if (ft->params) {
        typeid_append(s, assertnotnull(ft->params->type));
      } else {
        sbuf_appendc(s, TypeCodeEncoding(TC_nil));
      }
      if (!ft->result)
        return sbuf_appendc(s, TypeCodeEncoding(TC_nil));
      MUSTTAIL return typeid_append(s, ft->result);
    }

    default:
      panic("TODO %s", nodename(t));
      break;
  }

  return sbuf_append(s, t->tid, symlen(t->tid));
}


u32 _typeid_make(char* buf, u32 bufsize, const Type* t) {
  SBuf s = SBUF_INITIALIZER(buf, bufsize);
  typeid_append(&s, t);
  return sbuf_terminate(&s);
}
