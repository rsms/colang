#include "../common.h"
#include "../parse/parse.h"
#include "../util/ptrmap.h"
#include "bn.h"
#include "libbn.h"

typedef struct BNBuilder BNBuilder;


typedef enum {
  BNPrimType_none,
  BNPrimType_i32,
  BNPrimType_u32,
  BNPrimType_i64,
  BNPrimType_u64,
  BNPrimType_f32,
  BNPrimType_f64,
} BNPrimType;


struct BNBuilder {
  Build*            build;
  BinaryenModuleRef module;
  PtrMap            funmap; // function => fully-qualified name
  Str               strings;

  // cache
  BinaryenExpressionRef i32_0; // i32(0), false
  BinaryenExpressionRef i32_1; // i32(1), true
  BinaryenExpressionRef i64_0; // i64(0)
  BinaryenExpressionRef i64_1; // i64(1)
  BinaryenType          noneType; // == BinaryenTypeNone()
};


static void BNBuilderDispose(BNBuilder* b) {
  if (PtrMapIsInit(&b->funmap))
    PtrMapDispose(&b->funmap);
  BinaryenModuleDispose(b->module);
  str_free(b->strings);
}

// stubs
static BinaryenExpressionRef bn_expr(BNBuilder* b, Node* n);
static BinaryenType bn_type(BNBuilder* b, Type* nullable nt);
static bool bn_add_toplevel(BNBuilder* b, Node* n);
static bool writefile(const char* filename, const void* data, size_t size);


// bn_fun_fqname builds a function's fully qualified name (module global)
static const char* bn_fun_fqname(BNBuilder* b, Node* n) {
  asserteq_debug(n->kind, NFun);

  uintptr_t strOffset = (uintptr_t)PtrMapGet(&b->funmap, n);
  if (strOffset == 0) {
    strOffset = (uintptr_t)str_len(b->strings);

    if (b->build->debug) {
      // "name^typeid"
      b->strings = str_makeroom(b->strings, symlen(n->fun.name) + symlen(n->type->t.id) + 1);
      b->strings = str_append(b->strings, n->fun.name, symlen(n->fun.name));
      b->strings = str_append(b->strings, n->type->t.id, symlen(n->type->t.id));
    } else {
      // "f1F"
      u32 index = PtrMapLen(&b->funmap);
      b->strings = str_makeroom(b->strings, 1 + 10 /* 'f' + u32max */);
      b->strings = str_appendc(b->strings, 'f');
      b->strings = str_appendu64(b->strings, (u64)index, 62);
    }

    b->strings = str_appendc(b->strings, '\0'); // explicit terminator
    PtrMapSet(&b->funmap, n, (void*)strOffset);
  }

  return &b->strings[strOffset];
}


inline static BNPrimType bn_prim_type(Type* basicType) {
  // table maps numberic TypeCodes to BNPrimType
  static BNPrimType table[] = {
    [TypeCode_bool] = BNPrimType_i32, // bool

    [TypeCode_i8]  = BNPrimType_i32, // i8
    [TypeCode_i16] = BNPrimType_i32, // i16
    [TypeCode_i32] = BNPrimType_i32, // i32

    [TypeCode_u8]  = BNPrimType_u32, // u8
    [TypeCode_u16] = BNPrimType_u32, // u16
    [TypeCode_u32] = BNPrimType_u32, // u32

    [TypeCode_i64] = BNPrimType_i64,
    [TypeCode_u64] = BNPrimType_u64,

    [TypeCode_float32] = BNPrimType_f32,
    [TypeCode_float64] = BNPrimType_f64,

    // wasm32
    [TypeCode_int]   = BNPrimType_i32,
    [TypeCode_uint]  = BNPrimType_u32,
    [TypeCode_isize] = BNPrimType_i32,
    [TypeCode_usize] = BNPrimType_u32,

    // wasm64
    //[TypeCode_int]   = BNPrimType_i64,
    //[TypeCode_uint]  = BNPrimType_u64,
    //[TypeCode_isize] = BNPrimType_i64,
    //[TypeCode_usize] = BNPrimType_u64,

    [TypeCode_nil] = BNPrimType_none,
  };

  asserteq_debug(basicType->kind, NBasicType);
  return table[basicType->t.basic.typeCode];
}


static BinaryenType bn_basic_type(BNBuilder* b, Type* nt) {
  asserteq_debug(nt->kind, NBasicType);
  switch (bn_prim_type(nt)) {
    case BNPrimType_none: return b->noneType;

    case BNPrimType_i32:
    case BNPrimType_u32: return BinaryenTypeInt32();

    case BNPrimType_i64:
    case BNPrimType_u64: return BinaryenTypeInt64();

    case BNPrimType_f32: return BinaryenTypeFloat32();
    case BNPrimType_f64: return BinaryenTypeFloat64();
  }
}


static BinaryenType bn_tuple_type(BNBuilder* b, Type* nullable tt) {
  if (!tt)
    return b->noneType;

  assert_debug(tt->kind == NTupleType);
  BinaryenType valueTypesBuf[16];
  BinaryenType* valueTypes = valueTypesBuf;

  u32 nparams = tt->t.list.a.len;
  if (nparams > countof(valueTypesBuf))
    valueTypes = (BinaryenType*)memalloc(MemHeap, sizeof(BinaryenType) * (size_t)nparams);

  for (u32 i = 0; i < nparams; i++) {
    Type* paramType = (Node*)tt->t.list.a.v[i];
    valueTypes[i] = bn_type(b, paramType);
  }

  BinaryenType bt = BinaryenTypeCreate(valueTypes, nparams);

  if (valueTypes != valueTypesBuf)
    memfree(MemHeap, valueTypes);

  return bt;
}


static BinaryenType bn_type(BNBuilder* b, Type* nullable nt) {
  if (nt) switch (nt->kind) {
    case NBasicType: return bn_basic_type(b, nt);
    case NTupleType: return bn_tuple_type(b, nt);
    default:
      panic("TODO node kind %s", NodeKindName(nt->kind));
      break;
  }
  return b->noneType;
}


static BinaryenExpressionRef bn_expr_block(BNBuilder* b, Node* n) {
  asserteq_debug(n->kind, NBlock);

  if (n->array.a.len == 0) {
    assert(n->type == Const_nil); // empty block must have void/nil type
    return BinaryenNop(b->module);
  }

  if (n->array.a.len == 1)
    return bn_expr(b, (Node*)n->array.a.v[0]);

  BinaryenExpressionRef childrenBuf[32];
  BinaryenExpressionRef* children = childrenBuf;
  if (n->array.a.len > countof(childrenBuf)) {
    children = (BinaryenExpressionRef*)memalloc(
      MemHeap, sizeof(BinaryenExpressionRef) * (size_t)n->array.a.len);
  }

  u32 numChildren = 0;
  for (u32 i = 0; i < n->array.a.len; i++) {
    Node* cn = (Node*)n->array.a.v[i];
    // we can simply skip noop in blocks (unused Co expressions have type Type_ideal)
    if (cn->type != Type_ideal) {
      children[numChildren] = bn_expr(b, (Node*)n->array.a.v[i]);
      numChildren++;
    }
  }

  BinaryenExpressionRef block;
  if (R_UNLIKELY(numChildren == 0)) {
    // block without any expressions becomes a noop
    block = BinaryenNop(b->module);
  } else if (numChildren == 1) {
    // block with just one expression can be reduced to the one expression
    block = children[0];
  } else {
    const char* name = NULL;
    BinaryenType blockType = bn_type(b, n->type);
    block = BinaryenBlock(b->module, name, children, numChildren, blockType);
  }

  if (children != childrenBuf)
    memfree(MemHeap, children);

  return block;
}


static BinaryenOp bn_select_binop(BNBuilder* b, Tok coOp, Type* tn, Node* origin) {
  switch (bn_prim_type(tn)) {
    case BNPrimType_none:
      build_errf(b->build, NodePosSpan(origin), "invalid operand type %s", fmtnode(tn));
      break;
    case BNPrimType_i32:
      switch (coOp) {
        case TStar    : return BinaryenMulInt32();
        case TSlash   : return BinaryenDivSInt32();
        case TPlus    : return BinaryenAddInt32();
        case TMinus   : return BinaryenSubInt32();
        case TLt      : return BinaryenLtSInt32();
        case TLEq     : return BinaryenLeSInt32();
        case TGt      : return BinaryenGtSInt32();
        case TGEq     : return BinaryenGeSInt32();
        case TEq      : return BinaryenEqInt32();
        case TNEq     : return BinaryenNeInt32();
        case TPercent : return BinaryenRemSInt32();
        case TAnd     : return BinaryenAndInt32();
        case TPipe    : return BinaryenOrInt32();
        case TShl     : return BinaryenShlInt32();
        case TShr     : return BinaryenShrSInt32();
        case THat     : return BinaryenXorInt32(); // ^
        default: break;
      }
      break;
    case BNPrimType_i64:
      switch (coOp) {
        case TStar    : return BinaryenMulInt64();
        case TSlash   : return BinaryenDivSInt64();
        case TPlus    : return BinaryenAddInt64();
        case TMinus   : return BinaryenSubInt64();
        case TLt      : return BinaryenLtSInt64();
        case TLEq     : return BinaryenLeSInt64();
        case TGt      : return BinaryenGtSInt64();
        case TGEq     : return BinaryenGeSInt64();
        case TEq      : return BinaryenEqInt64();
        case TNEq     : return BinaryenNeInt64();
        case TPercent : return BinaryenRemSInt64();
        case TAnd     : return BinaryenAndInt64();
        case TPipe    : return BinaryenOrInt64();
        case TShl     : return BinaryenShlInt64();
        case TShr     : return BinaryenShrSInt64();
        case THat     : return BinaryenXorInt64(); // ^
        default: break;
      }
      break;

    case BNPrimType_u32:
      switch (coOp) {
        case TStar    : return BinaryenMulInt32();
        case TSlash   : return BinaryenDivUInt32();
        case TPlus    : return BinaryenAddInt32();
        case TMinus   : return BinaryenSubInt32();
        case TLt      : return BinaryenLtUInt32();
        case TLEq     : return BinaryenLeUInt32();
        case TGt      : return BinaryenGtUInt32();
        case TGEq     : return BinaryenGeUInt32();
        case TEq      : return BinaryenEqInt32();
        case TNEq     : return BinaryenNeInt32();
        case TPercent : return BinaryenRemUInt32();
        case TAnd     : return BinaryenAndInt32();
        case TPipe    : return BinaryenOrInt32();
        case TShl     : return BinaryenShlInt32();
        case TShr     : return BinaryenShrUInt32();
        case THat     : return BinaryenXorInt32(); // ^
        default: break;
      }
      break;
    case BNPrimType_u64:
      switch (coOp) {
        case TStar    : return BinaryenMulInt64();
        case TSlash   : return BinaryenDivUInt64();
        case TPlus    : return BinaryenAddInt64();
        case TMinus   : return BinaryenSubInt64();
        case TLt      : return BinaryenLtUInt64();
        case TLEq     : return BinaryenLeUInt64();
        case TGt      : return BinaryenGtUInt64();
        case TGEq     : return BinaryenGeUInt64();
        case TEq      : return BinaryenEqInt64();
        case TNEq     : return BinaryenNeInt64();
        case TPercent : return BinaryenRemUInt64();
        case TAnd     : return BinaryenAndInt64();
        case TPipe    : return BinaryenOrInt64();
        case TShl     : return BinaryenShlInt64();
        case TShr     : return BinaryenShrUInt64();
        case THat     : return BinaryenXorInt64(); // ^
        default: break;
      }
      break;

    case BNPrimType_f32:
      switch (coOp) {
        case TStar  : return BinaryenMulFloat32();
        case TSlash : return BinaryenDivFloat32();
        case TPlus  : return BinaryenAddFloat32();
        case TMinus : return BinaryenSubFloat32();
        case TLt    : return BinaryenLtFloat32();
        case TLEq   : return BinaryenLeFloat32();
        case TGt    : return BinaryenGtFloat32();
        case TGEq   : return BinaryenGeFloat32();
        case TEq    : return BinaryenEqFloat32();
        case TNEq   : return BinaryenNeFloat32();
        default: break;
      }
      break;
    case BNPrimType_f64:
      switch (coOp) {
        case TStar  : return BinaryenMulFloat64();
        case TSlash : return BinaryenDivFloat64();
        case TPlus  : return BinaryenAddFloat64();
        case TMinus : return BinaryenSubFloat64();
        case TLt    : return BinaryenLtFloat64();
        case TLEq   : return BinaryenLeFloat64();
        case TGt    : return BinaryenGtFloat64();
        case TGEq   : return BinaryenGeFloat64();
        case TEq    : return BinaryenEqFloat64();
        case TNEq   : return BinaryenNeFloat64();
        default: break;
      }
      break;
  }
  return BinaryenAddInt32();
}


static BinaryenExpressionRef bn_expr_binop(BNBuilder* b, Node* n) {
  asserteq_debug(n->kind, NBinOp);
  assertnotnull_debug(n->op.left);
  assertnotnull_debug(n->op.right);

  // evaluate operands left to right
  BinaryenExpressionRef left = bn_expr(b, n->op.left);
  BinaryenExpressionRef right = bn_expr(b, n->op.right);

  // select operation based on type of dominant operand
  Type* domop = n->op.left;
  if (domop->type->kind != NBasicType) {
    build_errf(b->build, NodePosSpan(n->op.left),
      "unexpected dominant type %s in binop", NodeKindName(domop->type->kind));
    return BinaryenNop(b->module);
  }
  BinaryenOp op = bn_select_binop(b, n->op.op, domop->type, domop);

  return BinaryenBinary(b->module, op, left, right);
}


static BinaryenExpressionRef bn_expr_call(BNBuilder* b, Node* n) {
  asserteq_debug(n->kind, NCall);
  Node* recv = n->call.receiver;

  if (NodeIsType(recv))
    panic("TODO: type call");

  if (recv->kind != NFun) {
    if (recv->kind == NId && recv->ref.target && recv->ref.target->kind == NFun) {
      // common case of function referenced by name. e.g.
      //   fun foo(x int) { ... }
      //   foo(123)
      recv = recv->ref.target;
    } else {
      // function is a value
      panic("TODO: BinaryenCallIndirect");
    }
  }

  // note: binaryen API requires all functions to be named and uses names instead
  // of pointers or handles to reference functions.
  const char* recvName = bn_fun_fqname(b, recv);

  // build input arguments
  Node* argstuple = n->call.args;
  BinaryenExpressionRef argsBuf[16];
  BinaryenExpressionRef* args = argsBuf;
  BinaryenIndex numArgs = 0;
  if (argstuple) {
    numArgs = argstuple->array.a.len;
    if (numArgs > countof(argsBuf)) {
      args = (BinaryenExpressionRef*)memalloc(
        MemHeap, sizeof(BinaryenExpressionRef) * (size_t)numArgs);
    }

    // arguments are evaluated left to right
    for (u32 i = 0; i < argstuple->array.a.len; i++) {
      Node* argnode = (Node*)argstuple->array.a.v[i];
      args[i] = bn_expr(b, argnode);
    }
  }

  BinaryenType returnType = bn_type(b, n->type);
  BinaryenExpressionRef value = BinaryenCall(b->module, recvName, args, numArgs, returnType);

  if (args != argsBuf)
    memfree(MemHeap, args);

  return value;
}


static BinaryenExpressionRef bn_expr_id(BNBuilder* b, Node* n) {
  asserteq_debug(n->kind, NId);

  if (n->ref.target->kind == NLet) {
    // variable
    panic("TODO");
  }

  // else: type, builtin etc
  return bn_expr(b, (Node*)n->ref.target);
}


static BinaryenExpressionRef bn_expr_arg(BNBuilder* b, Node* n) {
  asserteq_debug(n->kind, NArg);
  return BinaryenLocalGet(b->module, n->field.index, bn_type(b, n->type));
}


static BinaryenExpressionRef bn_expr_ret(BNBuilder* b, Node* n) {
  asserteq_debug(n->kind, NReturn);
  return BinaryenReturn(b->module, n->op.left ? bn_expr(b, n->op.left) : NULL);
}


static BinaryenExpressionRef bn_expr_constnum(BNBuilder* b, Node* n) {
  asserteq_debug(n->kind, NIntLit);

  switch (bn_prim_type(n->type)) {
    case BNPrimType_none: break;

    case BNPrimType_i32:
    case BNPrimType_u32:
      return n->val.i ? BinaryenConst(b->module, BinaryenLiteralInt32((i32)n->val.i)) : b->i32_0;

    case BNPrimType_i64:
    case BNPrimType_u64:
      return n->val.i ? BinaryenConst(b->module, BinaryenLiteralInt64((i64)n->val.i)) : b->i64_0;

    case BNPrimType_f32:
      return BinaryenConst(b->module, BinaryenLiteralFloat32((float)n->val.f));

    case BNPrimType_f64:
      return BinaryenConst(b->module, BinaryenLiteralFloat64(n->val.f));
  }

  return b->i32_0;
}


static BinaryenExpressionRef bn_expr(BNBuilder* b, Node* n) {
  // AST should be fully typed
  assertf_debug(NodeIsType(n) || n->type != NULL, "n = %s %s", NodeKindName(n->kind), fmtnode(n));

  if (n->type == Type_ideal) {
    // This means the expression is unused. It does not necessarily mean its value is unused,
    // so it would not be accurate to issue diagnostic warnings at this point.
    // For example:
    //
    //   fun foo {
    //     x = 1    # <- the NLet node is unused but its value (NIntLit 3) ...
    //     bar(x)   # ... is used by this NCall node.
    //   }
    //
    //dlog("skip unused %s", fmtnode(n));
    return BinaryenNop(b->module);
  }

  switch (n->kind) {
    case NBlock:  return bn_expr_block(b, n);
    case NBinOp:  return bn_expr_binop(b, n);
    case NId:     return bn_expr_id(b, n);
    case NArg:    return bn_expr_arg(b, n);
    case NReturn: return bn_expr_ret(b, n);
    case NCall:   return bn_expr_call(b, n);

    case NBoolLit:
    case NFloatLit:
    case NIntLit: return bn_expr_constnum(b, n);

    case NLet:
    case NIf:
    case NTypeCast:
    case NFun:
    case NArray:
    case NIndex:
    case NStrLit:
    case NNil:
    case NAssign:
    case NField:
    case NPrefixOp:
    case NPostfixOp:
    case NTuple:
    case NSelector:
    case NSlice:
    case NFunType:
    case NBasicType:
    case NTupleType:
    case NArrayType:
      panic("TODO ast_add_expr kind %s", NodeKindName(n->kind));
      break;

    case NFile:
    case NPkg:
    case NNone:
    case NBad:
    case _NodeKindMax:
      build_errf(b->build, NodePosSpan(n), "invalid AST node %s", NodeKindName(n->kind));
      break;
  }
  return BinaryenNop(b->module);
}


static bool bn_add_fun(BNBuilder* b, Node* n) { // n->kind==NFun
  asserteq_debug(n->kind, NFun);
  assertnotnull_debug(n->fun.body); // must have a body (not be just a declaration)
  assertnotnull_debug(n->fun.name); // functions must be named
  assertnotnull_debug(n->type);
  asserteq_debug(n->type->kind, NFunType);

  // TODO: if function has been generated, return it instead of generating it again
  // BinaryenFunctionRef funref = BinaryenGetFunction(b->module, n->fun.name);

  auto funType = n->type->t.fun;

  // input parameters
  BinaryenType paramsType = bn_tuple_type(b, funType.params);

  // output results
  BinaryenType resultsType = bn_type(b, funType.result);

  // body
  BinaryenExpressionRef body;
  if (n->fun.body) {
    body = bn_expr(b, n->fun.body);

    // Since wasm is a stack machine, "returning values" is implicit and so a function
    // that does not return any value must make sure the stack is relatively empty at exit.
    // Here we insert "drop" when the effective expression of the body produces values on
    // the stack, when the function does not return any values.
    if (funType.result == Type_nil && BinaryenExpressionGetType(body) != b->noneType) {
      // case: function does not return a value and the last expression is not void.
      if (BinaryenTypeArity(BinaryenExpressionGetType(body)) > 1) {
        // block body -- drop the last expression's result
        BinaryenIndex index = BinaryenBlockGetNumChildren(body) - 1;
        BinaryenExpressionRef lastExpr = BinaryenBlockGetChildAt(body, index);
        BinaryenBlockSetChildAt(body, index, BinaryenDrop(b->module, lastExpr));
        BinaryenExpressionSetType(body, b->noneType);
      } else {
        // single-expression body -- drop its values
        body = BinaryenDrop(b->module, body);
      }
    }
  } else {
    body = BinaryenNop(b->module);
  }

  // vars (TODO what is this?)
  BinaryenType* varTypes = NULL;
  BinaryenIndex numVarTypes = 0;

  const char* name = bn_fun_fqname(b, n);

  // Create the add function
  // Note: no additional local variables
  // Note: no basic blocks here, we are an AST. The function body is just an expression node.
  BinaryenFunctionRef fn = BinaryenAddFunction(
    b->module, name, paramsType, resultsType, varTypes, numVarTypes, body);

  if (strcmp(n->fun.name, "main") == 0)
    BinaryenSetStart(b->module, fn);

  // TODO: maybe use n->type->t.id

  return true;
}


static bool bn_add_file(BNBuilder* b, Node* n) { // n->kind==NFile
  asserteq_debug(n->kind, NFile);
  #if DEBUG
    auto src = build_get_source(b->build, n->pos);
    dlog("bn_add_file %s", src ? src->filename : "(unknown)");
  #endif
  for (u32 i = 0; i < n->array.a.len; i++) {
    if (!bn_add_toplevel(b, (Node*)n->array.a.v[i]))
      return false;
  }
  return true;
}


static bool bn_add_toplevel(BNBuilder* b, Node* n) {
  switch (n->kind) {
    case NFile: return bn_add_file(b, n);
    case NFun:  return bn_add_fun(b, n);

    case NLet:
      // top-level let bindings which are not exported can be ignored.
      // All let bindings are resolved already, so they only concern IR if their data is exported.
      // Since exporting is not implemented, just ignore top-level let for now.
      return true;

    default:
      build_errf(b->build, NodePosSpan(n), "invalid top-level AST node %s", NodeKindName(n->kind));
      break;
  }
  return false;
}


static bool ast_add_pkg(BNBuilder* b, Node* n) { // n->kind==NPkg
  PtrMapInit(&b->funmap, n->array.a.len, b->build->mem);
  for (u32 i = 0; i < n->array.a.len; i++) {
    Node* filenode = (Node*)n->array.a.v[i];
    if (!bn_add_file(b, filenode))
      return false;
  }
  return true;
}


static bool bn_build_mod(BNBuilder* b, Node* n) {
  if (n->kind != NPkg) {
    build_errf(b->build, NodePosSpan(n), "expected pkg, got %s", NodeKindName(n->kind));
    return false;
  }
  return ast_add_pkg(b, n);
}


static void bn_opt_mod(BNBuilder* b) {
  // note: binaryen uses global state for optimization config :-(
  if (b->build->opt == CoOptNone) {
    BinaryenSetOptimizeLevel(0); // -O0
    BinaryenSetShrinkLevel(0);   // -O0
  } else {
    BinaryenSetOptimizeLevel(3); // -O3
    BinaryenSetShrinkLevel(b->build->opt == CoOptSmall ? 2 : 1); // -Oz | -Os
  }
  BinaryenModuleOptimize(b->module);
}


bool bn_codegen(Build* build, Node* pkgnode) {
  dlog("bn_init");
  bool ok = true;

  // enable/disable inclusion of debug info (binaryen uses global state for this)
  BinaryenSetDebugInfo(build->debug);
  BinaryenSetFastMath(build_is_unsafe(build));

  BNBuilder b = {
    .build = build,
    .module = BinaryenModuleCreate(),
    .strings = str_new(128),
  };

  b.strings = str_appendc(b.strings, '0'); // string index 0 is invalid

  b.i32_0 = BinaryenConst(b.module, BinaryenLiteralInt32(0));
  b.i32_1 = BinaryenConst(b.module, BinaryenLiteralInt32(1));
  b.i64_0 = BinaryenConst(b.module, BinaryenLiteralInt64(0));
  b.i64_1 = BinaryenConst(b.module, BinaryenLiteralInt64(1));
  b.noneType = BinaryenTypeNone();

  bn_build_mod(&b, pkgnode);

  // import built-in print function (deps/binaryen/src/shell-interface.h)
  const char* internalName = "print_i32";
  const char* externalModuleName = "spectest";
  const char* externalBaseName = "print_i32";
  BinaryenType params = BinaryenTypeInt32();
  BinaryenType results = b.noneType;
  BinaryenAddFunctionImport(
    b.module, internalName, externalModuleName, externalBaseName, params, results);

  BinaryenModulePrint(b.module);
  if (R_UNLIKELY(!BinaryenModuleValidate(b.module))) {
    dlog("[bn] BinaryenModuleValidate failed");
    // Attempt to insert (drop). From the binaryen API docs on BinaryenModuleAutoDrop:
    //   Auto-generate drop() operations where needed. This lets you generate code
    //   without worrying about where they are needed. (It is more efficient to do it
    //   yourself, but simpler to use autodrop).
    BinaryenModuleAutoDrop(b.module);
    if (BinaryenModuleValidate(b.module)) {
      dlog("[bn] recovered OK with BinaryenModuleAutoDrop");
    } else {
      ok = false;
      goto end;
    }
  }

  // optimize the module
  if (build->opt > CoOptNone) {
    bn_opt_mod(&b);
    #if DEBUG
    if (!BinaryenModuleValidate(b.module)) {
      ok = false;
      goto end;
    }
    #endif
    dlog("after BinaryenModuleOptimize:");
    BinaryenModulePrint(b.module);
  }

  // generate wasm file
  auto wr = BinaryenModuleAllocateAndWrite(b.module, /*sourceMapUrl=*/NULL);
  writefile("out.wasm", wr.binary, wr.binaryBytes);
  free(wr.binary);
  assert(wr.sourceMap == NULL);

  // generate wast file
  char* wast = BinaryenModuleAllocateAndWriteText(b.module);
  writefile("out.wast", wast, strlen(wast));
  free(wast);

  // interpret
  // bn_mod_interpret(b.module);
  BinaryenModuleInterpret(b.module);

end:
  BNBuilderDispose(&b);
  return ok;
}


static bool writefile(const char* filename, const void* data, size_t size) {
  FILE* fp = fopen(filename, "w");
  if (!fp) {
    errlog("failed to open \"%s\" for writing", filename);
  } else {
    ssize_t n = fwrite(data, size, 1, fp);
    fclose(fp);
    if (n) {
      fprintf(stderr, "wrote %s (%zu bytes)\n", filename, size);
      return true;
    }
    errlog("failed to write to \"%s\"", filename);
  }
  return false;
}
