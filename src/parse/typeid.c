// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
#include "parse.h"


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


bool _typeid_append(Str* s, const Type* t) {
  if (is_BasicTypeNode(t))
    return str_appendc(s, TypeCodeEncoding(as_BasicTypeNode(t)->typecode));

  if (!t->tid) switch (t->kind) {
    case NTypeExpr:
      MUSTTAIL return _typeid_append(s, ((TypeExprNode*)t)->type);

    case NIdType:
      MUSTTAIL return _typeid_append(s, assertnotnull(((IdTypeNode*)t)->target));

    case NRefType:
      str_appendc(s, TypeCodeEncoding(NodeIsConst(t) ? TC_ref : TC_mutref));
      MUSTTAIL return _typeid_append(s, assertnotnull(as_RefTypeNode(t)->elem));

    case NArrayType:
      str_appendc(s, TypeCodeEncoding(TC_array));
      str_appendu64(s, as_ArrayTypeNode(t)->size, 10);
      str_appendc(s, TypeCodeEncoding(TC_arrayEnd));
      MUSTTAIL return _typeid_append(s, assertnotnull(as_ArrayTypeNode(t)->elem));

    case NTupleType:
      str_appendc(s, TypeCodeEncoding(TC_tuple));
      for (u32 i = 0; i < as_TupleTypeNode(t)->a.len; i++)
        _typeid_append(s, as_TupleTypeNode(t)->a.v[i]);
      return str_appendc(s, TypeCodeEncoding(TC_tupleEnd));

    case NStructType:
      str_appendc(s, TypeCodeEncoding(TC_struct));
      for (u32 i = 0; i < as_StructTypeNode(t)->fields.len; i++) {
        FieldNode* field = as_StructTypeNode(t)->fields.v[i];
        _typeid_append(s, assertnotnull(field->type));
      }
      return str_appendc(s, TypeCodeEncoding(TC_structEnd));

    case NFunType: {
      auto ft = as_FunTypeNode(t);
      str_appendc(s, TypeCodeEncoding(TC_fun));
      str_appendc(s, TypeCodeEncoding(TC_tuple));
      for (u32 i = 0; i < ft->params->len; i++)
        _typeid_append(s, assertnotnull(ft->params->v[i]->type));
      str_appendc(s, TypeCodeEncoding(TC_tupleEnd));
      if (!ft->result)
        return str_appendc(s, TypeCodeEncoding(TC_nil));
      MUSTTAIL return _typeid_append(s, ft->result);
    }

    default:
      panic("TODO %s", nodename(t));
      break;
  }

  return str_append(s, t->tid, symlen(t->tid));
}
