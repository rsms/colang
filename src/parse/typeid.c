#include "../coimpl.h"
#include "../sbuf.h"
#include "typeid.h"

#include "../test.h"
#include "universe.h"


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

    case NRefType:
      sbuf_appendc(s, TypeCodeEncoding(TC_ref));
      MUSTTAIL return typeid_append(s, as_RefTypeNode(t)->elem);

    case NArrayType:
      sbuf_appendc(s, TypeCodeEncoding(TC_array));
      sbuf_appendu32(s, as_ArrayTypeNode(t)->size, 10);
      sbuf_appendc(s, ']');
      MUSTTAIL return typeid_append(s, as_ArrayTypeNode(t)->elem);

    // case NTupleType:
    //   s = str_appendc(s, TypeCodeEncoding(TC_tuple));
    //   for (u32 i = 0; i < t->t.tuple.a.len; i++)
    //     s = mktypestr(s, (Type*)t->t.tuple.a.v[i]);
    //   return str_appendc(s, TypeCodeEncoding(TC_tupleEnd));

    // case NStructType:
    //   s = str_appendc(s, TypeCodeEncoding(TC_struct));
    //   for (u32 i = 0; i < t->t.struc.a.len; i++)
    //     s = mktypestr(s, ((Type*)t->t.struc.a.v[i])->type);
    //   return str_appendc(s, TypeCodeEncoding(TC_structEnd));

    // case NFunType:
    //   s = str_appendc(s, TypeCodeEncoding(TC_fun));
    //   if (t->t.fun.params) {
    //     s = mktypestr(s, t->t.fun.params->type);
    //   } else {
    //     s = str_appendc(s, TypeCodeEncoding(TC_nil));
    //   }
    //   if (t->t.fun.result)
    //     return mktypestr(s, t->t.fun.result);
    //   return str_appendc(s, TypeCodeEncoding(TC_nil));

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


DEF_TEST(typeid_make) {
  char buf[128];
  {
    memset(buf, 0xff, sizeof(buf)); // so we can verify '\0' is written
    u32 n = typeid_make(buf, sizeof(buf), kType_i32);
    asserteq(n, 1);
    asserteq(buf[0], TypeCodeEncoding(TC_i32));
    asserteq(buf[1], 0);
    // when buf is short, should still write null terminator and return accurate length
    n = typeid_make(buf, 1, kType_i32);
    asserteq(n, 1);
    asserteq(buf[0], 0);
  }
  {
    ArrayTypeNode arrayt = { .kind = NArrayType, .size = 1337, .elem = kType_i32 };
    u32 n = typeid_make(buf, sizeof(buf), &arrayt);
    asserteq(n, 7);
    assert(memcmp(buf, "[1337]", 6) == 0);
    asserteq(buf[6], TypeCodeEncoding(TC_i32));
    asserteq(buf[n], 0);
  }

}
