#include <rbase/rbase.h>
#include "ir.h"
#include "irbuilder.h"

#if DEBUG
__attribute__((used))
static Str debug_fmtval1(Str s, const IRValue* v, int indent) {
  s = str_appendfmt(s, "v%u(op=%s type=%s args=[", v->id, IROpName(v->op), TypeCodeName(v->type));
  if (v->argslen > 0) {
    for (int i = 0; i < v->argslen; i++) {
      s = str_appendfmt(s, "\n  %*s", (indent * 2), "");
      s = debug_fmtval1(s, v->args[i], indent + 1);
    }
    s = str_appendfmt(s, "\n%*s", (indent * 2), "");
  }
  s = str_appendcstr(s, "])");
  return s;
}

__attribute__((used))
static const char* debug_fmtval(u32 bufid, const IRValue* v) {
  static char bufs[6][512];
  assert(bufid < countof(bufs));
  char* buf = bufs[bufid];
  auto s = debug_fmtval1(str_new(32), v, 0);
  memcpy(buf, s, str_len(s) + 1);
  str_free(s);
  return buf;
}
#endif


void IRBuilderInit(IRBuilder* u, Build* build, IRBuilderFlags flags) {
  memset(u, 0, sizeof(IRBuilder));
  u->build = build;
  u->mem = MemNew(0);
  u->pkg = IRPkgNew(u->mem, build->pkg->id);
  PtrMapInit(&u->funs, 32, u->mem);
  u->vars = SymMapNew(8, u->mem);
  u->flags = flags;
  ArrayInitWithStorage(&u->defvars, u->defvarsStorage, sizeof(u->defvarsStorage)/sizeof(void*));
}

void IRBuilderDispose(IRBuilder* u) {
  MemFree(u->mem);
}


// sealBlock sets b.sealed=true, indicating that no further predecessors will be added
// (no changes to b.preds)
static void sealBlock(IRBuilder* u, IRBlock* b) {
  assert(!b->sealed); // block not sealed already
  dlog("sealBlock %p", b);
  // TODO: port from co:
  // if (b->incompletePhis != NULL) {
  //   let entries = s.incompletePhis.get(b)
  //   if (entries) {
  //     for (let [name, phi] of entries) {
  //       dlogPhi(`complete pending phi ${phi} (${name})`)
  //       s.addPhiOperands(name, phi)
  //     }
  //     s.incompletePhis.delete(b)
  //   }
  // }
  b->sealed = true;
}

// startBlock sets the current block we're generating code in
static void startBlock(IRBuilder* u, IRBlock* b) {
  assert(u->b == NULL); // err: forgot to call endBlock
  u->b = b;
  dlog("startBlock %p", b);
}

// startSealedBlock is a convenience for sealBlock followed by startBlock
static void startSealedBlock(IRBuilder* u, IRBlock* b) {
  sealBlock(u, b);
  startBlock(u, b);
}

// endBlock marks the end of generating code for the current block.
// Returns the (former) current block. Returns null if there is no current
// block, i.e. if no code flows to the current execution point.
// The block sealed if not already sealed.
static IRBlock* endBlock(IRBuilder* u) {
  auto b = u->b;
  assert(b != NULL); // has current block

  // Move block-local vars to long-term definition data.
  // First we fill any holes in defvars.
  while (u->defvars.len <= b->id) {
    ArrayPush(&u->defvars, NULL, u->mem);
  }
  if (u->vars->len > 0) {
    u->defvars.v[b->id] = u->vars;
    u->vars = SymMapNew(8, u->mem);  // new block-local vars
  }

  u->b = NULL;  // crash if we try to use b before a new block is started
  return b;
}


static void startFun(IRBuilder* u, IRFun* f) {
  assert(u->f == NULL); // starting function with existing function
  u->f = f;
  dlog("startFun %p", u->f);
}

static void endFun(IRBuilder* u) {
  assert(u->f != NULL); // no current function
  dlog("endFun %p", u->f);
  #if DEBUG
  u->f = NULL;
  #endif
}


static IRValue* TODO_Value(IRBuilder* u) {
  return IRValueNew(u->f, u->b, OpNil, TypeCode_nil, NoPos);
}


// ———————————————————————————————————————————————————————————————————————————————————————————————
// Phi & variables

#define dlogvar(format, ...) \
  fprintf(stderr, "VAR " format "\t(%s:%d)\n", ##__VA_ARGS__, __FILE__, __LINE__)


static void writeVariable(IRBuilder* u, Sym name, IRValue* value, IRBlock* b) {
  if (b == u->b) {
    dlogvar("write %.*s in current block", (int)symlen(name), name);
    auto oldv = SymMapSet(u->vars, name, value);
    if (oldv != NULL) {
      dlogvar("new value replaced old value: %p", oldv);
    }
  } else {
    dlogvar("TODO write %.*s in defvars", (int)symlen(name), name);
  }
}


static IRValue* readVariable(IRBuilder* u, Sym name, Node* typeNode, IRBlock* b/*null*/) {
  if (b == u->b) {
    // current block
    dlogvar("read %.*s in current block", (int)symlen(name), name);
    auto v = SymMapGet(u->vars, name);
    if (v != NULL) {
      return v;
    }
  } else {
    dlogvar("TODO read %.*s in defvars", (int)symlen(name), name);
  //   let m = u.defvars[b.id]
  //   if (m) {
  //     let v = m.get(name)
  //     if (v) {
  //       return v
  //     }
  //   }
  }

  dlogvar("read %.*s not found -- falling back to readRecursive", (int)symlen(name), name);

  // // global value numbering
  // return u.readVariableRecursive(name, t, b)

  dlog("TODO");
  return TODO_Value(u);
}


// ———————————————————————————————————————————————————————————————————————————————————————————————
// add functions. Most atomic at top, least at bottom. I.e. let -> block -> fun -> file.

static IRValue* ast_add_expr(IRBuilder* u, Node* n);
static bool ast_add_toplevel(IRBuilder* u, Node* n);


static IRValue* ast_add_intconst(IRBuilder* u, Node* n) {
  assert(n->kind == NIntLit);
  assert(n->type->kind == NBasicType);
  auto v = IRFunGetConstInt(u->f, n->type->t.basic.typeCode, n->val.i);
  return v;
}


static IRValue* ast_add_boolconst(IRBuilder* u, Node* n) {
  assert(n->kind == NBoolLit);
  assert(n->type->kind == NBasicType);
  assert(n->type->t.basic.typeCode == TypeCode_bool);
  return IRFunGetConstBool(u->f, (bool)n->val.i);
}


static IRValue* addAssign(IRBuilder* u, Sym name /*nullable*/, IRValue* value) {
  assert(value != NULL);
  if (name == NULL) {
    // dummy assignment to "_"; i.e. "_ = x" => "x"
    return value;
  }

  // instead of issuing an intermediate "copy", simply associate variable
  // name with the value on the right-hand side.
  writeVariable(u, name, value, u->b);

  if (u->flags & IRBuilderComments) {
    IRValueAddComment(value, u->mem, name);
  }

  return value;
}


static IRValue* ast_add_id(IRBuilder* u, Node* n) {
  assert(n->kind == NId);
  assert(n->ref.target != NULL); // never unresolved
  // dlog("ast_add_id \"%s\" target = %s", n->ref.name, fmtnode(n->ref.target));
  if (n->ref.target->kind == NLet) {
    // variable
    return readVariable(u, n->ref.name, n->type, u->b);
  }
  // else: type or builtin etc
  return ast_add_expr(u, (Node*)n->ref.target);
}


static IRValue* ast_add_typecast(IRBuilder* u, Node* n) {
  assert(n->kind == NTypeCast);
  assert(n->call.receiver != NULL);
  assert(n->call.args != NULL);

  // generate rvalue
  auto inval = ast_add_expr(u, n->call.args);
  auto dstType = n->call.receiver;

  if (R_UNLIKELY(dstType->kind != NBasicType)) {
    build_errf(u->build, n->pos, NoPos, "invalid type %s in type cast", fmtnode(dstType));
    return TODO_Value(u);
  }

  auto totype = dstType->t.basic.typeCode;
  // aliases. e.g. int, uint
  switch (totype) {
    case TypeCode_int:  totype = TypeCode_int32; break;
    case TypeCode_uint: totype = TypeCode_uint32; break;
    default: break;
  }
  if (totype == inval->type) {
    return inval;
  }
  IROp convop = IROpConvertType(inval->type, totype);
  if (R_UNLIKELY(convop == OpNil)) {
    build_errf(u->build, n->pos, NoPos, "invalid type conversion %s to %s",
      TypeCodeName(inval->type), TypeCodeName(dstType->t.basic.typeCode));
    return TODO_Value(u);
  }
  auto v = IRValueNew(u->f, u->b, convop, totype, n->pos);
  IRValueAddArg(v, inval);
  return v;
}


static IRValue* ast_add_arg(IRBuilder* u, Node* n) {
  assert(n->kind == NArg);
  if (R_UNLIKELY(n->type->kind != NBasicType)) {
    // TODO add support for NTupleType et al
    build_errf(u->build, n->pos, NoPos, "invalid argument type %s", fmtnode(n->type));
    return TODO_Value(u);
  }
  auto v = IRValueNew(u->f, u->b, OpArg, n->type->t.basic.typeCode, n->pos);
  v->auxInt = n->field.index;
  return v;
}


static IRValue* ast_add_binop(IRBuilder* u, Node* n) {
  assert(n->kind == NBinOp);

  dlog("ast_add_binop %s %s = %s",
    TokName(n->op.op),
    fmtnode(n->op.left),
    n->op.right != NULL ? fmtnode(n->op.right) : "nil"
  );

  // gen left operand
  auto left  = ast_add_expr(u, n->op.left);
  auto right = ast_add_expr(u, n->op.right);

  dlog("[BinOp] left:  %s", debug_fmtval(0, left));
  dlog("[BinOp] right: %s", debug_fmtval(0, right));

  // lookup IROp
  IROp op = IROpFromAST(n->op.op, left->type, right->type);
  assert(op != OpNil);

  // read result type
  #if DEBUG
  // ensure that the type we think n will have is actually the type of the resulting value.
  TypeCode restype = IROpInfo(op)->outputType;
  if (restype > TypeCode_NUM_END) {
    // result type is parametric; is the same as an input type.
    assert(restype == TypeCode_param1 || restype == TypeCode_param2);
    if (restype == TypeCode_param1) {
      restype = left->type;
    } else {
      restype = right->type;
    }
  }
  assert( restype == n->type->t.basic.typeCode);
  #else
  TypeCode restype = n->type->t.basic.typeCode;
  #endif

  auto v = IRValueNew(u->f, u->b, op, restype, n->pos);
  IRValueAddArg(v, left);
  IRValueAddArg(v, right);
  return v;
}


static IRValue* ast_add_let(IRBuilder* u, Node* n) {
  assert(n->kind == NLet);
  assertnotnull(n->field.init);
  assertnotnull(n->type);
  assert(n->type != Type_ideal);
  dlog("ast_add_let %s %s = %s",
    n->field.name ? n->field.name : "_",
    fmtnode(n->type),
    n->field.init ? fmtnode(n->field.init) : "nil"
  );
  auto v = ast_add_expr(u, n->field.init); // right-hand side
  return addAssign(u, n->field.name, v);
}


// ast_add_if reads an "if" expression, e.g.
//  (If (Op > (Ident x) (Int 1)) ; condition
//      (Let x (Int 1))          ; then block
//      (Let x (Int 2)) )        ; else block
// Returns a new empty block that's the block after the if.
static IRValue* ast_add_if(IRBuilder* u, Node* n) {
  assert(n->kind == NIf);
  //
  // if..end has the following semantics:
  //
  //   if cond b1 b2
  //   b1:
  //     <then-block>
  //   goto b2
  //   b2:
  //     <continuation-block>
  //
  // if..else..end has the following semantics:
  //
  //   if cond b1 b2
  //   b1:
  //     <then-block>
  //   goto b3
  //   b2:
  //     <else-block>
  //   goto b3
  //   b3:
  //     <continuation-block>
  //
  // TODO

  // generate control condition
  auto control = ast_add_expr(u, n->cond.cond);
  if (R_UNLIKELY(control->type != TypeCode_bool)) {
    // AST should not contain conds that are non-bool
    build_errf(u->build, n->cond.cond->pos, NoPos,
      "invalid non-bool type in condition %s", fmtnode(n->cond.cond));
  }

  assert(n->cond.thenb != NULL);

  // [optimization] Early optimization of constant boolean condition
  if ((u->flags & IRBuilderOpt) != 0 && (IROpInfo(control->op)->aux & IRAuxBool)) {
    dlog("[ir/builder if] short-circuit constant cond");
    if (control->auxInt != 0) {
      // then branch always taken
      return ast_add_expr(u, n->cond.thenb);
    }
    // else branch always taken
    if (n->cond.elseb == NULL) {
      dlog("TODO ir/builder produce nil value");
      return IRValueNew(u->f, u->b, OpNil, TypeCode_nil, n->pos);
    }
    return ast_add_expr(u, n->cond.elseb);
  }

  // end predecessor block (leading up to and including "if")
  auto ifb = endBlock(u);
  ifb->kind = IRBlockIf;
  IRBlockSetControl(ifb, control);

  // create blocks for then and else branches
  auto thenb = IRBlockNew(u->f, IRBlockCont, n->cond.thenb->pos);
  auto elsebIndex = u->f->blocks.len; // may be used later for moving blocks
  auto elseb = IRBlockNew(u->f, IRBlockCont, n->cond.elseb == NULL ? n->pos : n->cond.elseb->pos);
  ifb->succs[0] = thenb;
  ifb->succs[1] = elseb; // if -> then, else

  // begin "then" block
  dlog("[if] begin \"then\" block");
  thenb->preds[0] = ifb; // then <- if
  startSealedBlock(u, thenb);
  auto thenv = ast_add_expr(u, n->cond.thenb);  // generate "then" body
  thenb = endBlock(u);

  IRValue* elsev = NULL;

  if (n->cond.elseb != NULL) {
    // "else"

    // allocate "cont" block; the block following both thenb and elseb
    auto contbIndex = u->f->blocks.len;
    auto contb = IRBlockNew(u->f, IRBlockCont, n->pos);

    // begin "else" block
    dlog("[if] begin \"else\" block");
    elseb->preds[0] = ifb; // else <- if
    startSealedBlock(u, elseb);
    elsev = ast_add_expr(u, n->cond.elseb);  // generate "else" body
    elseb = endBlock(u);
    elseb->succs[0] = contb; // else -> cont
    thenb->succs[0] = contb; // then -> cont
    contb->preds[0] = thenb;
    contb->preds[1] = elseb; // cont <- then, else
    startSealedBlock(u, contb);

    // move cont block to end (in case blocks were created by "else" body)
    IRFunMoveBlockToEnd(u->f, contbIndex);

    assertf(thenv->type == elsev->type,
      "branch type mismatch %s, %s", TypeCodeName(thenv->type), TypeCodeName(elsev->type));

    if (elseb->values.len == 0) {
      // "else" body may be empty in case it refers to an existing value. For example:
      //   x = 9 ; y = if true x + 1 else x
      // This compiles to:
      //   b0:
      //     v1 = const 9
      //     v2 = const 1
      //   if true -> b1, b2
      //   b1:
      //     v3 = add v1 v2
      //   cont -> b3
      //   b2:                    #<-  Note: Empty
      //   cont -> b3
      //   b3:
      //     v4 = phi v3 v1
      //
      // The above can be reduced to:
      //   b0:
      //     v1 = const 9
      //     v2 = const 1
      //   if true -> b1, b3     #<- change elseb to contb
      //   b1:
      //     v3 = add v1 v2
      //   cont -> b3
      //                         #<- remove elseb
      //   b3:
      //     v4 = phi v3 v1      #<- phi remains valid; no change needed
      //
      ifb->succs[1] = contb;  // if -> cont
      contb->preds[1] = ifb;  // cont <- if
      IRBlockDiscard(elseb);
      elseb = NULL;
    }

    if (u->flags & IRBuilderComments) {
      thenb->comment = memsprintf(u->mem, "b%u.then", ifb->id);
      if (elseb != NULL) { elseb->comment = memsprintf(u->mem, "b%u.else", ifb->id); }
      contb->comment = memsprintf(u->mem, "b%u.end", ifb->id);
    }

  } else {
    // no "else" block
    thenb->succs[0] = elseb; // then -> else
    elseb->preds[0] = ifb;
    elseb->preds[1] = thenb; // else <- if, then
    startSealedBlock(u, elseb);

    // move cont block to end (in case blocks were created by "then" body)
    IRFunMoveBlockToEnd(u->f, elsebIndex);

    if (u->flags & IRBuilderComments) {
      thenb->comment = memsprintf(u->mem, "b%u.then", ifb->id);
      elseb->comment = memsprintf(u->mem, "b%u.end", ifb->id);
    }

    // Consider and decide what semantics we want for if..then expressions without else.
    // There are at least three possibilities:
    //
    //   A. zero initialized value of the then-branch type:
    //
    //      "x = if y 3"                 typeof(x) => int       If false: 0
    //      "x = if y Account{ id: 1 }"  typeof(x) => Account   If false: Account{id:0}
    //
    //   B. zero initialized basic types, higher-level types become optional:
    //
    //      "x = if y 3"                 typeof(x) => int       If false: 0
    //      "x = if y Account{ id: 1 }"  typeof(x) => Account?  If false: nil
    //
    //   C. any type becomes optional:
    //
    //      "x = if y 3"                 typeof(x) => int?      If false: nil
    //      "x = if y Account{ id: 1 }"  typeof(x) => Account?  If false: nil
    //
    // Discussion:
    //
    //   C implies that the language has a concept of pointers beyond reference types.
    //   i.e. is an int? passed to a function copy-by-value or not? Probably not because then
    //   "fun foo(x int)" vs "fun foo(x int?)" would be equivalent, which doesn't make sense.
    //   Okay, so C is out.
    //
    //   B is likely the best choice here, assuming the language has a concept of optional.
    //   To implement B, we need to:
    //
    //   - Add support to resolve_type so that the effective type of the the if expression is
    //     optional for higher-level types (but not for basic types.)
    //
    //   - Decide on a representation of optional. Likely actually null; the constant 0.
    //     In that case, we have two options for IR branch block generation:
    //
    //       1. Store 0 to the result before evaluating the condition expression, or
    //
    //       2. generate an implicit "else" block that stores 0 to the result.
    //
    //     Approach 1 introduces possibly unnecessary stores while the second approach introduces
    //     Phi joints always. Also, the second approach introduces additional branching in the
    //     final generated code.
    //
    //     Because of this, approach 1 is the better one. It has optimization opportunities as
    //     well, like for instance: if we know that the storage for the result of the
    //     if-expression is already zero (e.g. from calloc), we can skip storing zero.
    //
    // Conclusion:
    //
    // - B. zero-initialized basic types, higher-level types become optional.
    // - Store zero before branch, rather than generating implicit "else" branches.
    // - Introduce "optional" as a concept in the language.
    // - Update resolve_type to convert higher-order types to optional in lieu of an "else" branch.
    //

    // zero constant in place of "else" block, sized to match the result type
    elsev = IRFunGetConstInt(u->f, thenv->type, 0);
  }

  // make Phi, joining the two branches together
  auto phi = IRValueNew(u->f, u->b, OpPhi, thenv->type, n->pos);
  assertf(u->b->preds[0] != NULL, "phi in block without predecessors");
  phi->args[0] = thenv;
  phi->args[1] = elsev;
  thenv->uses++;
  elsev->uses++;
  phi->argslen = 2;
  return phi;
}


static IRValue* ast_add_block(IRBuilder* u, Node* n) {  // language block, not IR block.
  assert(n->kind == NBlock);
  IRValue* v = NULL;
  for (u32 i = 0; i < n->array.a.len; i++)
    v = ast_add_expr(u, (Node*)n->array.a.v[i]);
  return v;
}


static IRValue* ast_add_expr(IRBuilder* u, Node* n) {
  assertnotnull(n->type); // AST should be fully typed
  if (R_UNLIKELY(n->type == Type_ideal)) {
    // This means the expression is unused. It does not necessarily mean its value is unused,
    // so it would not be accurate to issue diagnostic warnings at this point.
    // For example:
    //
    //   fun foo {
    //     x = 1    # <- the NLet node is unused but its value (NIntLit 3) ...
    //     bar(x)   # ... is used by this NCall node.
    //   }
    //
    // There's one exception which is unresolved let expressions with an unresolved value;
    // that definitely means it's unused and we produce a diagnostic warning for that.
    //
    dlog("skip unused %s", fmtnode(n));
    if (n->kind == NLet && n->field.init->type == Type_ideal)
      build_warnf(u->build, n->pos, NodeEndPos(n), "unused expression: %s", fmtnode(n));
    return NULL;
  }
  switch (n->kind) {
    case NLet:      return ast_add_let(u, n);
    case NBlock:    return ast_add_block(u, n);
    case NIntLit:   return ast_add_intconst(u, n);
    case NBoolLit:  return ast_add_boolconst(u, n);
    case NBinOp:    return ast_add_binop(u, n);
    case NId:       return ast_add_id(u, n);
    case NIf:       return ast_add_if(u, n);
    case NTypeCast: return ast_add_typecast(u, n);
    case NArg:      return ast_add_arg(u, n);

    case NFloatLit:
    case NNil:
    case NAssign:
    case NBasicType:
    case NCall:
    case NComment:
    case NField:
    case NFun:
    case NFunType:
    case NPrefixOp:
    case NPostfixOp:
    case NReturn:
    case NTuple:
    case NTupleType:
    case NZeroInit:
      dlog("TODO ast_add_expr kind %s", NodeKindName(n->kind));
      break;

    case NFile:
    case NPkg:
    case NNone:
    case NBad:
    case _NodeKindMax:
      build_errf(u->build, n->pos, NoPos, "invalid AST node %s", NodeKindName(n->kind));
      break;
  }
  return TODO_Value(u);
}


static IRFun* ast_add_fun(IRBuilder* u, Node* n) {
  assert(n->kind == NFun);
  assert(n->fun.body != NULL); // not a concrete function

  auto f = (IRFun*)PtrMapGet(&u->funs, (void*)n);
  if (f != NULL) {
    // fun already built or in progress of being built
    return f;
  }

  dlog("ast_add_fun %s", fmtnode(n));

  // allocate a new function and its entry block
  assert(n->type != NULL);
  assert(n->type->kind == NFunType);
  auto params = n->type->fun.params;
  u32 nparams = 0;
  if (params)
    nparams = params->kind == NTuple ? params->array.a.len : 1;
  f = IRFunNew(u->mem, n->type->t.id, n->fun.name, n->pos, nparams);
  auto entryb = IRBlockNew(f, IRBlockCont, n->pos);

  // Since functions can be anonymous and self-referential, short-circuit using a PtrMap
  // Add the function before we generate its body.
  PtrMapSet(&u->funs, n, f);
  IRPkgAddFun(u->pkg, f);

  // start function
  startFun(u, f);
  startSealedBlock(u, entryb); // entry block has no predecessors, so seal right away.

  // build body
  auto bodyval = ast_add_expr(u, n->fun.body);

  // end last block, if not already ended
  if (u->b != NULL) {
    u->b->kind = IRBlockRet;
    IRBlockSetControl(u->b, bodyval);
    endBlock(u);
  }

  // end function and return
  endFun(u);
  return f;
}


static bool ast_add_file(IRBuilder* u, Node* n) { // n->kind==NFile
  #if DEBUG
  auto src = build_get_source(u->build, n->pos);
  dlog("ast_add_file %s", src ? src->filename : "(unknown)");
  #endif
  for (u32 i = 0; i < n->array.a.len; i++) {
    if (!ast_add_toplevel(u, (Node*)n->array.a.v[i]))
      return false;
  }
  return true;
}


static bool ast_add_pkg(IRBuilder* u, Node* n) { // n->kind==NPkg
  for (u32 i = 0; i < n->array.a.len; i++) {
    if (!ast_add_file(u, (Node*)n->array.a.v[i]))
      return false;
  }
  return true;
}


static bool ast_add_toplevel(IRBuilder* u, Node* n) {
  switch (n->kind) {
    case NPkg:  return ast_add_pkg(u, n);
    case NFile: return ast_add_file(u, n);
    case NFun:  return ast_add_fun(u, n) != NULL;

    case NLet:
      // top-level let bindings which are not exported can be ignored.
      // All let bindings are resolved already, so they only concern IR if their data is exported.
      // Since exporting is not implemented, just ignore top-level let for now.
      return true;

    case NNone:
    case NBad:
    case NBoolLit:
    case NIntLit:
    case NFloatLit:
    case NNil:
    case NAssign:
    case NBasicType:
    case NBlock:
    case NCall:
    case NComment:
    case NField:
    case NArg:
    case NFunType:
    case NId:
    case NIf:
    case NBinOp:
    case NPrefixOp:
    case NPostfixOp:
    case NReturn:
    case NTuple:
    case NTupleType:
    case NTypeCast:
    case NZeroInit:
    case _NodeKindMax:
      build_errf(u->build, n->pos, NoPos, "invalid top-level AST node %s", NodeKindName(n->kind));
      break;
  }
  return false;
}


bool IRBuilderAddAST(IRBuilder* u, Node* n) {
  return ast_add_toplevel(u, n);
}

