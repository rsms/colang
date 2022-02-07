#include "../coimpl.h"
#include "parse.h"


Node* NodeInit(Node* n, NodeKind kind) {
  n->kind = kind;
  switch (kind) {
    case NPkg:
    case NFile: {
      auto N = as_CUnitNode(n);
      NodeArrayInitStorage(&N->a, N->a_storage, countof(N->a_storage));
      break;
    }
    case NBlock: {
      auto N = (BlockNode*)n;
      NodeArrayInitStorage(&N->a, N->a_storage, countof(N->a_storage));
      break;
    }
    case NArray:
    case NTuple: {
      auto N = as_ListExpr(n);
      ExprArrayInitStorage(&N->a, N->a_storage, countof(N->a_storage));
      break;
    }
    case NSelector: {
      auto N = (SelectorNode*)n;
      U32ArrayInitStorage(&N->indices, N->indices_storage, countof(N->indices_storage));
      break;
    }
    case NTupleType: {
      auto N = (TupleTypeNode*)n;
      TypeArrayInitStorage(&N->a, N->a_storage, countof(N->a_storage));
      break;
    }
    case NStructType: {
      auto N = (StructTypeNode*)n;
      FieldArrayInitStorage(&N->fields, N->fields_storage, countof(N->fields_storage));
      break;
    }
    default:
      break;
  }
  return n;
}


PosSpan _NodePosSpan(const Node* n) {
  assertnotnull(n);
  PosSpan span = { n->pos, n->endpos };
  // dlog("-- NodePosSpan %s %u:%u",
  //   NodeKindName(n->kind), pos_line(n->endpos), pos_col(n->endpos));
  if (!pos_isknown(span.end))
    span.end = span.start;

  switch (n->kind) {
    case NBinOp: {
      auto op = (BinOpNode*)n;
      span.start = op->left->pos;
      span.end = op->right->pos;
      break;
    }
    case NCall: {
      auto call = (CallNode*)n;
      span.start = NodePosSpan(call->receiver).start;
      if (call->args)
        span.end = NodePosSpan(call->args).end;
      break;
    }
    case NTuple: {
      span.start = pos_with_adjusted_start(span.start, -1);
      break;
    }
    case NNamedArg: {
      auto namedarg = (NamedArgNode*)n;
      span.end = NodePosSpan(namedarg->value).end;
      break;
    }
    default:
      break;
  }

  return span;
}


Scope* ScopeNew(Mem mem, const Scope* parent) {
  Scope* s = memalloczt(mem, Scope);
  if (!s)
    return NULL;
  //assertf(IS_ALIGN2((uintptr)s, sizeof(void*)), "%p not a pointer aligned address", s);
  s->parent = parent;
  map_init_small(&s->bindings);
  return s;
}

void ScopeFree(Scope* s, Mem mem) {
  symmap_free(&s->bindings, mem);
  memfree(mem, s);
}

error ScopeAssign(Scope* s, Sym key, Node* n, Mem mem) {
  void** valp = symmap_assign(&s->bindings, key, mem);
  if (UNLIKELY(valp == NULL))
    return err_nomem;
  *valp = n;
  return 0;
}

const Node* ScopeLookup(const Scope* scope, Sym s) {
  const Node* n = NULL;
  while (scope && n == NULL) {
    dlog("[lookup] %s in scope %p(len=%zu)", s, scope, map_len(&scope->bindings));
    n = symmap_access(&scope->bindings, s);
    scope = scope->parent;
  }
  #ifdef DEBUG_LOOKUP
  if (n == NULL) {
    dlog("ScopeLookup(%p) %s => (null)", scope, s);
  } else {
    dlog("ScopeLookup(%p) %s => node of kind %s", scope, s, NodeKindName(n->kind));
  }
  #endif
  return n;
}


//BEGIN GENERATED CODE by ast_gen.py

const char* NodeKindName(NodeKind k) {
  // kNodeNameTable[NodeKind] => const char* name
  static const char* const kNodeNameTable[NodeKind_MAX+2] = {
"Bad", "Field", "Pkg", "File", "Comment", "Nil", "BoolLit", "IntLit",
    "FloatLit", "StrLit", "Id", "BinOp", "PrefixOp", "PostfixOp", "Return",
    "Assign", "Tuple", "Array", "Block", "Fun", "Macro", "Call", "TypeCast",
    "Var", "Ref", "NamedArg", "Selector", "Index", "Slice", "If", "TypeType",
    "NamedType", "AliasType", "RefType", "BasicType", "ArrayType", "TupleType",
    "StructType", "FunType", "?"
  };
  return kNodeNameTable[MIN(NodeKind_MAX+2,k)];
}

static error ASTVisitorNoop(ASTVisitor* v, const Node* n) { return 0; }

void ASTVisitorInit(ASTVisitor* v, const ASTVisitorFuns* f) {
  ASTVisitorFun dft = (f->Node ? f->Node : &ASTVisitorNoop), dft1 = dft;
  // populate v->ftable
  v->ftable[NBad] = f->Bad ? ((ASTVisitorFun)f->Bad) : dft;
  v->ftable[NField] = f->Field ? ((ASTVisitorFun)f->Field) : dft;
  // begin Stmt
  if (f->Stmt) { dft1 = dft; dft = ((ASTVisitorFun)f->Stmt); }
  // begin CUnit
  if (f->CUnit) { dft1 = dft; dft = ((ASTVisitorFun)f->CUnit); }
  v->ftable[NPkg] = f->Pkg ? ((ASTVisitorFun)f->Pkg) : dft;
  v->ftable[NFile] = f->File ? ((ASTVisitorFun)f->File) : dft;
  dft = dft1; // end CUnit
  v->ftable[NComment] = f->Comment ? ((ASTVisitorFun)f->Comment) : dft;
  dft = dft1; // end Stmt
  // begin Expr
  if (f->Expr) { dft1 = dft; dft = ((ASTVisitorFun)f->Expr); }
  // begin LitExpr
  if (f->LitExpr) { dft1 = dft; dft = ((ASTVisitorFun)f->LitExpr); }
  v->ftable[NNil] = f->Nil ? ((ASTVisitorFun)f->Nil) : dft;
  v->ftable[NBoolLit] = f->BoolLit ? ((ASTVisitorFun)f->BoolLit) : dft;
  v->ftable[NIntLit] = f->IntLit ? ((ASTVisitorFun)f->IntLit) : dft;
  v->ftable[NFloatLit] = f->FloatLit ? ((ASTVisitorFun)f->FloatLit) : dft;
  v->ftable[NStrLit] = f->StrLit ? ((ASTVisitorFun)f->StrLit) : dft;
  dft = dft1; // end LitExpr
  v->ftable[NId] = f->Id ? ((ASTVisitorFun)f->Id) : dft;
  v->ftable[NBinOp] = f->BinOp ? ((ASTVisitorFun)f->BinOp) : dft;
  // begin UnaryOp
  if (f->UnaryOp) { dft1 = dft; dft = ((ASTVisitorFun)f->UnaryOp); }
  v->ftable[NPrefixOp] = f->PrefixOp ? ((ASTVisitorFun)f->PrefixOp) : dft;
  v->ftable[NPostfixOp] = f->PostfixOp ? ((ASTVisitorFun)f->PostfixOp) : dft;
  dft = dft1; // end UnaryOp
  v->ftable[NReturn] = f->Return ? ((ASTVisitorFun)f->Return) : dft;
  v->ftable[NAssign] = f->Assign ? ((ASTVisitorFun)f->Assign) : dft;
  // begin ListExpr
  if (f->ListExpr) { dft1 = dft; dft = ((ASTVisitorFun)f->ListExpr); }
  v->ftable[NTuple] = f->Tuple ? ((ASTVisitorFun)f->Tuple) : dft;
  v->ftable[NArray] = f->Array ? ((ASTVisitorFun)f->Array) : dft;
  dft = dft1; // end ListExpr
  v->ftable[NBlock] = f->Block ? ((ASTVisitorFun)f->Block) : dft;
  v->ftable[NFun] = f->Fun ? ((ASTVisitorFun)f->Fun) : dft;
  v->ftable[NMacro] = f->Macro ? ((ASTVisitorFun)f->Macro) : dft;
  v->ftable[NCall] = f->Call ? ((ASTVisitorFun)f->Call) : dft;
  v->ftable[NTypeCast] = f->TypeCast ? ((ASTVisitorFun)f->TypeCast) : dft;
  v->ftable[NVar] = f->Var ? ((ASTVisitorFun)f->Var) : dft;
  v->ftable[NRef] = f->Ref ? ((ASTVisitorFun)f->Ref) : dft;
  v->ftable[NNamedArg] = f->NamedArg ? ((ASTVisitorFun)f->NamedArg) : dft;
  v->ftable[NSelector] = f->Selector ? ((ASTVisitorFun)f->Selector) : dft;
  v->ftable[NIndex] = f->Index ? ((ASTVisitorFun)f->Index) : dft;
  v->ftable[NSlice] = f->Slice ? ((ASTVisitorFun)f->Slice) : dft;
  v->ftable[NIf] = f->If ? ((ASTVisitorFun)f->If) : dft;
  dft = dft1; // end Expr
  // begin Type
  if (f->Type) { dft1 = dft; dft = ((ASTVisitorFun)f->Type); }
  v->ftable[NTypeType] = f->TypeType ? ((ASTVisitorFun)f->TypeType) : dft;
  v->ftable[NNamedType] = f->NamedType ? ((ASTVisitorFun)f->NamedType) : dft;
  v->ftable[NAliasType] = f->AliasType ? ((ASTVisitorFun)f->AliasType) : dft;
  v->ftable[NRefType] = f->RefType ? ((ASTVisitorFun)f->RefType) : dft;
  v->ftable[NBasicType] = f->BasicType ? ((ASTVisitorFun)f->BasicType) : dft;
  v->ftable[NArrayType] = f->ArrayType ? ((ASTVisitorFun)f->ArrayType) : dft;
  v->ftable[NTupleType] = f->TupleType ? ((ASTVisitorFun)f->TupleType) : dft;
  v->ftable[NStructType] = f->StructType ? ((ASTVisitorFun)f->StructType) : dft;
  v->ftable[NFunType] = f->FunType ? ((ASTVisitorFun)f->FunType) : dft;
  // end Type
}

//END GENERATED CODE
