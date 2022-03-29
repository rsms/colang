#include "../coimpl.h"
#include "../string.c"
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


static void typeid_append(ABuf* s, const Type* t) {
  if (is_BasicTypeNode(t)) {
    abuf_c(s, TypeCodeEncoding(as_BasicTypeNode(t)->typecode));
    return;
  }

  if (!t->tid) switch (t->kind) {
    case NAliasType:
      MUSTTAIL return typeid_append(s, as_AliasTypeNode(t)->type);

    case NRefType:
      abuf_c(s, TypeCodeEncoding(TC_ref));
      MUSTTAIL return typeid_append(s, assertnotnull(as_RefTypeNode(t)->elem));

    case NArrayType:
      abuf_c(s, TypeCodeEncoding(TC_array));
      abuf_u32(s, as_ArrayTypeNode(t)->size, 10);
      abuf_c(s, TypeCodeEncoding(TC_arrayEnd));
      MUSTTAIL return typeid_append(s, assertnotnull(as_ArrayTypeNode(t)->elem));

    case NTupleType:
      abuf_c(s, TypeCodeEncoding(TC_tuple));
      for (u32 i = 0; i < as_TupleTypeNode(t)->a.len; i++)
        typeid_append(s, as_TupleTypeNode(t)->a.v[i]);
      abuf_c(s, TypeCodeEncoding(TC_tupleEnd));
      return;

    case NStructType:
      abuf_c(s, TypeCodeEncoding(TC_struct));
      for (u32 i = 0; i < as_StructTypeNode(t)->fields.len; i++) {
        FieldNode* field = as_StructTypeNode(t)->fields.v[i];
        typeid_append(s, assertnotnull(field->type));
      }
      abuf_c(s, TypeCodeEncoding(TC_structEnd));
      return;

    case NFunType: {
      auto ft = as_FunTypeNode(t);
      abuf_c(s, TypeCodeEncoding(TC_fun));
      if (ft->params) {
        typeid_append(s, assertnotnull(ft->params->type));
      } else {
        abuf_c(s, TypeCodeEncoding(TC_nil));
      }
      if (!ft->result) {
        abuf_c(s, TypeCodeEncoding(TC_nil));
        return;
      }
      MUSTTAIL return typeid_append(s, ft->result);
    }

    default:
      panic("TODO %s", nodename(t));
      break;
  }

  abuf_append(s, t->tid, symlen(t->tid));
}


usize _typeid_make(char* buf, usize bufsize, const Type* t) {
  ABuf s = abuf_make(buf, bufsize);
  typeid_append(&s, t);
  return abuf_terminate(&s);
}
