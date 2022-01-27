// universe -- built-in symbols and AST nodes
#pragma once
#include "ast.h"
ASSUME_NONNULL_BEGIN

// DEF_CONST_NODES_PUB: predefined named constant AST Nodes, exported in universe_scope,
// included in universe_syms
//   const Sym sym_##name
//   Node*     kNode_##name
#define DEF_CONST_NODES_PUB(_) /* (name, AST_TYPE, typecode_suffix, structinit) */ \
  _( nil,   Nil,     nil,  "" ) \
  _( true,  BoolLit, bool, ".ival=1" ) \
  _( false, BoolLit, bool, ".ival=0" ) \
// end DEF_CONST_NODES_PUB

// DEF_SYMS_PUB: predefined additional symbols, included in universe_syms
//   const Sym sym_##name
#define DEF_SYMS_PUB(X) /* (name) */ \
  X( _ ) \
// end DEF_SYMS_PUB

// precompiled constants defined in universe_data.h
extern Node*          kNode_bad;
extern BasicTypeNode* kType_bool;
extern BasicTypeNode* kType_i8;
extern BasicTypeNode* kType_u8;
extern BasicTypeNode* kType_i16;
extern BasicTypeNode* kType_u16;
extern BasicTypeNode* kType_i32;
extern BasicTypeNode* kType_u32;
extern BasicTypeNode* kType_i64;
extern BasicTypeNode* kType_u64;
extern BasicTypeNode* kType_f32;
extern BasicTypeNode* kType_f64;
extern BasicTypeNode* kType_int;
extern BasicTypeNode* kType_uint;
extern BasicTypeNode* kType_nil;
extern BasicTypeNode* kType_ideal;
extern BasicTypeNode* kType_str;
extern BasicTypeNode* kType_auto;
extern NilNode*       kExpr_nil;
extern BoolLitNode*   kExpr_true;
extern BoolLitNode*   kExpr_false;

void universe_init();
const Scope* universe_scope();
const SymPool* universe_syms();

ASSUME_NONNULL_END
