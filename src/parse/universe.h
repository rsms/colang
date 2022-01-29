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

// precompiled constants, defined in universe_data.h
extern const Sym kSym__;
extern Node* kNode_bad;
extern Type* kType_bool;
extern Type* kType_i8;
extern Type* kType_u8;
extern Type* kType_i16;
extern Type* kType_u16;
extern Type* kType_i32;
extern Type* kType_u32;
extern Type* kType_i64;
extern Type* kType_u64;
extern Type* kType_f32;
extern Type* kType_f64;
extern Type* kType_int;
extern Type* kType_uint;
extern Type* kType_nil;
extern Type* kType_ideal;
extern Type* kType_str;
extern Type* kType_auto;
extern Expr* kExpr_nil;
extern Expr* kExpr_true;
extern Expr* kExpr_false;

void universe_init();
const Scope* universe_scope();
const SymPool* universe_syms();

ASSUME_NONNULL_END
