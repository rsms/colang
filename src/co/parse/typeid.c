#include <rbase/rbase.h>
#include "parse.h"

// See doc/typeid.md

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


// // TypeSymStr returns a human-readable representation of a type symbol.
// // E.g. a tuple (int, int, bool) is represented as "(int, int, bool)".
// // Not thread safe!
// static const char* TypeSymStr(const Sym s) {
//   static Str buf;
//   if (buf == NULL) {
//     buf = sdsempty();
//   } else {
//     sdssetlen(buf, 0);
//   }
//   buf = sdscat(buf, "TODO(TypeSymStr)");
//   // TODO
//   return buf;
// }

// mktypestr appends a type ID string for n to s
static Str mktypestr(Str s, const Node* n) {
  if (n->kind != NBasicType && n->t.id) {
    // append n's precomputed type id. E.g. "(ii)" for a tuple "(int, int)".
    // However for basic types its faster to just use str_appendc as mktypestr is
    // never called directly for a basic type, as all basic types have precomputed TypeIDs
    // which short-circuits GetTypeID.
    return str_append(s, n->t.id, symlen(n->t.id));
  }
  switch (n->kind) {

    case NBasicType:
      s = str_appendc(s, TypeCodeEncoding[n->t.basic.typeCode]);
      break;

    case NTupleType:
      s = str_appendc(s, TypeCodeEncoding[TypeCode_tuple]);
      NodeListForEach(&n->t.tuple, n, {
        s = mktypestr(s, n);
      });
      s = str_appendc(s, TypeCodeEncoding[TypeCode_tupleEnd]);
      break;

    case NFunType: {
      s = str_appendc(s, TypeCodeEncoding[TypeCode_fun]);
      if (n->t.fun.params) {
        s = mktypestr(s, n->t.fun.params);
      } else {
        s = str_appendc(s, TypeCodeEncoding[TypeCode_nil]);
      }
      if (n->t.fun.result) {
        s = mktypestr(s, n->t.fun.result);
      } else {
        s = str_appendc(s, TypeCodeEncoding[TypeCode_nil]);
      }
      break;
    }

    default:
      dlog("TODO mktypestr handle %s", NodeKindName(n->kind));
      assert(!NodeKindIsType(n->kind)); // unhandled type
      break;
  }
  return s;
}


// GetTypeID returns the type Sym identifying n
Sym GetTypeID(BuildCtx* b, Node* n) {
  if (n->t.id != NULL)
    return n->t.id;
  b->tmpbuf = mktypestr(str_setlen(b->tmpbuf, 0), n);
  n->t.id = symget(b->syms, b->tmpbuf, str_len(b->tmpbuf));
  return n->t.id;
}


bool TypeEquals(BuildCtx* b, Node* x, Node* y) {
  assert(x != NULL);
  assert(y != NULL);
  assert(NodeKindIsType(x->kind));
  if (x == y)
    return true;
  if (x->kind != y->kind)
    return false;
  // if (x->kind == NBasicType)
  //   return x->t.id == y->t.id;
  return GetTypeID(b, x) == GetTypeID(b, y);
}


// // index = to TypeCode * from TypeCode
// static TypeConv const basicTypeConvTable[TypeCode_NUM_END * 2] = {
//   0
// };


// TypeConv CheckTypeConversion(BuildCtx* b, Node* fromType, Node* toType, u32 intsize) {
//   assert(toType != NULL);
//   assert(fromType != NULL);
//   assert(NodeKindIsType(toType->kind));
//   assert(NodeKindIsType(fromType->kind));
//   if (TypeEquals(b, toType, fromType)) {
//     return TypeConvLossless;
//   }

//   // TODO
//   return TypeConvImpossible;
// }




// ——————————————————————————————————————————————————————————————————————————————————————————————
// unit test

#if R_UNIT_TEST_ENABLED

// static const char* TypeConvName(TypeConv c) {
//   switch (c) {
//   case TypeConvLossless:   return "Lossless";
//   case TypeConvLossy:      return "Lossy";
//   case TypeConvImpossible: return "Impossible";
//   default:                 return "?";
//   }
// }

// static void assertConv(const Node* toType, const Node* fromType, TypeConv expected) {
//   u32 intsize = 4; // int=int32, uint=uint32
//   auto actual = CheckTypeConversion((Node*)fromType, (Node*)toType, intsize);
//   if (actual != expected) {
//     printf("CheckTypeConversion(%s <- %s) => %s; expected %s\n",
//       fmtnode(toType), fmtnode(fromType),
//       TypeConvName(actual), TypeConvName(expected));
//     assert(actual == expected);
//   }
// }

R_UNIT_TEST(typeid) {
  // printf("--------------------------------------------------\n");
  auto build = test_build_new();
  auto mem = build->mem;

  #define mknode(t) NewNode(mem, (t))

  // // same types
  // #define X(name) assertConv(Type_##name, Type_##name, TypeConvLossless);
  // TYPE_SYMS(X)
  // #undef X

  // //   fromType -> toType
  // #define LOSSLESS(_) \
  //   _( uint8, int32 ) \
  //   _( int, int64 ) \
  //   _( int, float64 ) \
  //   \
  //   _( int, int32 ) \
  //   _( int, int64 ) \
  //   _( int, float64 ) \
  // /* end LOSSLESS */
  // #define X(fromType,toType)  assertConv(Type_##fromType, Type_##toType, TypeConvLossless);
  // LOSSLESS(X)
  // #undef X
  // #undef LOSSLESS


  // auto scope = ScopeNew(GetGlobalScope()); // defined in ast.h
  // dlog("sizeof(TypeCode_nil) %zu", sizeof(TypeCode_nil));

  // const u8 data[] = { '\t', 1, 2, 3 };
  // auto id = symgeth(data, sizeof(data));
  // dlog("id: %p", id);

  // auto typeNode = mknode(NBasicType);
  // dlog("typeNode: %p", typeNode);

  // const Node* replacedNode = ScopeAssoc(scope, id, typeNode);
  // dlog("ScopeAssoc() => replacedNode: %p", replacedNode);

  // const Node* foundTypeNode = ScopeLookup(scope, id);
  // dlog("ScopeLookup() => foundTypeNode: %p", foundTypeNode);


  // { // build a type Sym from a basic data type. Type_int is a predefined node (sym.h)
  //   auto intType = GetTypeID(Type_int);
  //   dlog("int intType: %p %s", intType, strrepr(intType));
  // }

  { // (int, int, bool) => "(iib)"
    Node* tupleType = mknode(NTupleType);
    NodeListAppend(mem, &tupleType->t.tuple, Type_int);
    NodeListAppend(mem, &tupleType->t.tuple, Type_int);
    NodeListAppend(mem, &tupleType->t.tuple, Type_bool);
    auto id = GetTypeID(build, tupleType);
    // dlog("tuple (int, int, bool) id: %p %s", id, strrepr(id));
    assert(strcmp(id, "(iib)") == 0);
  }

  { // ((int, int), (bool, int), int) => "((ii)(bi)i)"
    Node* t2 = mknode(NTupleType);
    NodeListAppend(mem, &t2->t.tuple, Type_bool);
    NodeListAppend(mem, &t2->t.tuple, Type_int);

    Node* t1 = mknode(NTupleType);
    NodeListAppend(mem, &t1->t.tuple, Type_int);
    NodeListAppend(mem, &t1->t.tuple, Type_int);

    Node* t0 = mknode(NTupleType);
    NodeListAppend(mem, &t0->t.tuple, t1);
    NodeListAppend(mem, &t0->t.tuple, t2);
    NodeListAppend(mem, &t0->t.tuple, Type_int);

    auto id = GetTypeID(build, t0);
    assert(strcmp(id, "((ii)(bi)i)") == 0);

    // create second one that has the same shape
    Node* t2b = mknode(NTupleType);
    NodeListAppend(mem, &t2b->t.tuple, Type_bool);
    NodeListAppend(mem, &t2b->t.tuple, Type_int);

    Node* t1b = mknode(NTupleType);
    NodeListAppend(mem, &t1b->t.tuple, Type_int);
    NodeListAppend(mem, &t1b->t.tuple, Type_int);

    Node* t0b = mknode(NTupleType);
    NodeListAppend(mem, &t0b->t.tuple, t1b);
    NodeListAppend(mem, &t0b->t.tuple, t2b);
    NodeListAppend(mem, &t0b->t.tuple, Type_int);

    // they should be equivalent
    assert(TypeEquals(build, t0, t0b));

    // create third one that has a slightly different shape (bool at end)
    Node* t2c = mknode(NTupleType);
    NodeListAppend(mem, &t2c->t.tuple, Type_bool);
    NodeListAppend(mem, &t2c->t.tuple, Type_int);

    Node* t1c = mknode(NTupleType);
    NodeListAppend(mem, &t1c->t.tuple, Type_int);
    NodeListAppend(mem, &t1c->t.tuple, Type_int);

    Node* t0c = mknode(NTupleType);
    NodeListAppend(mem, &t0c->t.tuple, t1c);
    NodeListAppend(mem, &t0c->t.tuple, t2c);
    NodeListAppend(mem, &t0c->t.tuple, Type_bool);

    // they should be different
    assert( ! TypeEquals(build, t0, t0c));
  }


  { // fun (int,bool) -> int
    Node* params = mknode(NTupleType);
    NodeListAppend(mem, &params->t.tuple, Type_int);
    NodeListAppend(mem, &params->t.tuple, Type_bool);

    Node* result = Type_int;

    Node* f = mknode(NFunType);
    f->t.fun.params = params;
    f->t.fun.result = result;

    auto id = GetTypeID(build, f);
    // dlog("fun (int,bool) -> int id: %p %s", id, strrepr(id));
    assert(strcmp(id, "^(ib)i") == 0);
  }


  { // fun () -> ()
    Node* f = mknode(NFunType);
    auto id = GetTypeID(build, f);
    // dlog("fun () -> () id: %p %s", id, strrepr(id));
    assert(strcmp(id, "^00") == 0);
  }


  { // ( fun(int,bool)->int, fun(int)->bool, fun()->(int,bool) )
    Node* params = mknode(NTupleType);
    NodeListAppend(mem, &params->t.tuple, Type_int);
    NodeListAppend(mem, &params->t.tuple, Type_bool);
    Node* f1 = mknode(NFunType);
    f1->t.fun.params = params;
    f1->t.fun.result = Type_int;

    params = mknode(NTupleType);
    NodeListAppend(mem, &params->t.tuple, Type_int);
    Node* f2 = mknode(NFunType);
    f2->t.fun.params = params;
    f2->t.fun.result = Type_bool;

    auto result = mknode(NTupleType);
    NodeListAppend(mem, &result->t.tuple, Type_int);
    NodeListAppend(mem, &result->t.tuple, Type_bool);
    Node* f3 = mknode(NFunType);
    f3->t.fun.result = result;

    Node* t1 = mknode(NTupleType);
    NodeListAppend(mem, &t1->t.tuple, f1);
    NodeListAppend(mem, &t1->t.tuple, f2);
    NodeListAppend(mem, &t1->t.tuple, f3);

    auto id = GetTypeID(build, t1);
    // dlog("t1 id: %p %s", id, strrepr(id));
    assert(strcmp(id, "(^(ib)i^(i)b^0(ib))") == 0);
  }

  test_build_free(build);
  // printf("--------------------------------------------------\n");
}

#endif /* R_UNIT_TEST_ENABLED */


// // TypeGTEq returns true if L >= R. I.e. R fits in L.
// // Examples:
// //   TypeGTEq( {id int}, {name str; id int} ) => true
// //   TypeGTEq( {name str; id int}, {id int} ) => false (R is missing "name" field)
// bool TypeGTEq(Node* L, Node* R);