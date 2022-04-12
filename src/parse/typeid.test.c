#include "parse.h"

DEF_TEST(typeid_append) {
  mem_ctx_set_scope(mem_mkalloc_null()); // no heap allocations
  char buf[128];
  {
    Str s = str_make(buf, sizeof(buf));
    assert(typeid_append(&s, kType_i32));
    asserteq(buf[0], TypeCodeEncoding(TC_i32));
    asserteq(s.len, 1);
  }
  {
    Str s = str_make(buf, sizeof(buf));
    ArrayTypeNode t = { .kind = NArrayType, .size = 1337, .elem = kType_i32 };
    assert(typeid_append(&s, &t));
    asserteq(buf[0], TypeCodeEncoding(TC_array));
    assert(memcmp(&buf[1], "1337", 4) == 0);
    asserteq(buf[5], TypeCodeEncoding(TC_arrayEnd));
    asserteq(buf[6], TypeCodeEncoding(TC_i32));
    asserteq(s.len, 7);
  }
  {
    Str s = str_make(buf, sizeof(buf));
    TupleTypeNode t = { .kind = NTupleType };
    array_init(&t.a, t.a_storage, sizeof(t.a_storage));
    t.a.v[t.a.len++] = kType_i32;
    t.a.v[t.a.len++] = kType_u32;
    assert(typeid_append(&s, &t));
    asserteq(buf[0], TypeCodeEncoding(TC_tuple));
    asserteq(buf[1], TypeCodeEncoding(TC_i32));
    asserteq(buf[2], TypeCodeEncoding(TC_u32));
    asserteq(buf[3], TypeCodeEncoding(TC_tupleEnd));
    asserteq(s.len, 4);
  }
  {
    Str s = str_make(buf, sizeof(buf));
    StructTypeNode t = { .kind = NStructType };
    array_init(&t.fields, t.fields_storage, sizeof(t.fields_storage));
    FieldNode f1 = { .type = kType_i32 };
    FieldNode f2 = { .type = kType_u32 };
    t.fields.v[t.fields.len++] = &f1;
    t.fields.v[t.fields.len++] = &f2;
    assert(typeid_append(&s, &t));
    asserteq(buf[0], TypeCodeEncoding(TC_struct));
    asserteq(buf[1], TypeCodeEncoding(TC_i32));
    asserteq(buf[2], TypeCodeEncoding(TC_u32));
    asserteq(buf[3], TypeCodeEncoding(TC_structEnd));
    asserteq(s.len, 4);
  }
}

