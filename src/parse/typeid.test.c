#include "parse.h"

DEF_TEST(typeid_make) {
  char buf[128];
  {
    memset(buf, 0xff, sizeof(buf)); // so we can verify '\0' is written
    usize n = typeid_make(buf, sizeof(buf), kType_i32);
    asserteq(n, 1);
    asserteq(buf[0], TypeCodeEncoding(TC_i32));
    asserteq(buf[1], 0);
    // when buf is short, should still write null terminator and return accurate length
    n = typeid_make(buf, 1, kType_i32);
    asserteq(n, 1);
    asserteq(buf[0], 0);
  }
  {
    ArrayTypeNode t = { .kind = NArrayType, .size = 1337, .elem = kType_i32 };
    usize n = typeid_make(buf, sizeof(buf), &t);
    asserteq(buf[0], TypeCodeEncoding(TC_array));
    assert(memcmp(&buf[1], "1337", 4) == 0);
    asserteq(buf[5], TypeCodeEncoding(TC_arrayEnd));
    asserteq(buf[6], TypeCodeEncoding(TC_i32));
    asserteq(buf[7], 0);
    asserteq(n, 7);
  }
  {
    TupleTypeNode t = { .kind = NTupleType };
    array_init(&t.a, t.a_storage, sizeof(t.a_storage));
    t.a.v[t.a.len++] = kType_i32;
    t.a.v[t.a.len++] = kType_u32;
    usize n = typeid_make(buf, sizeof(buf), &t);
    asserteq(buf[0], TypeCodeEncoding(TC_tuple));
    asserteq(buf[1], TypeCodeEncoding(TC_i32));
    asserteq(buf[2], TypeCodeEncoding(TC_u32));
    asserteq(buf[3], TypeCodeEncoding(TC_tupleEnd));
    asserteq(buf[4], 0);
    asserteq(n, 4);
  }
  {
    StructTypeNode t = { .kind = NStructType };
    array_init(&t.fields, t.fields_storage, sizeof(t.fields_storage));
    FieldNode f1 = { .type = kType_i32 };
    FieldNode f2 = { .type = kType_u32 };
    t.fields.v[t.fields.len++] = &f1;
    t.fields.v[t.fields.len++] = &f2;
    usize n = typeid_make(buf, sizeof(buf), &t);
    asserteq(buf[0], TypeCodeEncoding(TC_struct));
    asserteq(buf[1], TypeCodeEncoding(TC_i32));
    asserteq(buf[2], TypeCodeEncoding(TC_u32));
    asserteq(buf[3], TypeCodeEncoding(TC_structEnd));
    asserteq(buf[4], 0);
    asserteq(n, 4);
  }
}

