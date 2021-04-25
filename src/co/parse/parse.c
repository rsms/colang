//
// Pratt-style recursive descent parser.
//
// Syntax productions are parsed by functions dubbed "parselets".
// They are defined with a magical comment described by the following eBNF:
//
//   infix  = <LF> "//!Parselet" ( "(" Tok Precedence ")" )+
//   prefix = <LF> "//!PrefixParselet" Tok+
//
// Examples:
//
//   //!Parselet (TPlus ADD) (TMinus ADD) (TStar MULTIPLY) (TSlash MULTIPLY)
//   static Node* PInfixOp(Parser* p, const Parselet* e, PFlag fl, Node* left)
//
//   //!PrefixParselet TPlus TMinus TExcalm
//   static Node* PPrefixOp(Parser* p, PFlag fl)
//
// The parselet dispatch map for all tokens is generated by a script (gen_parselet_map.py)
// which builds the "parselets" table (see end of file.)
//
// enable debug messages for pushScope() and popScope()
// #define DEBUG_SCOPE_PUSH_POP
//
// enable debug messages for defsym()
// #define DEBUG_DEFSYM
//
// enable debug messages for unresolved identifiers
// #define DEBUG_UNRESOLVED
//
#include <rbase/rbase.h>
#include "parse.h"


// Operator precedence
// Precedence    Operator
//     5             *  /  %  <<  >>  &  &^
//     4             +  -  |  ^
//     3             ==  !=  <  <=  >  >=
//     2             &&
//     1             ||
//
typedef enum Precedence {
  PREC_LOWEST,
  PREC_ASSIGN,
  PREC_COMMA,
  PREC_NULL_JOIN,
  PREC_LOGICAL_OR,
  PREC_LOGICAL_AND,
  PREC_BITWISE_OR,
  PREC_BITWISE_XOR,
  PREC_BITWISE_AND,
  PREC_EQUAL,
  PREC_COMPARE,
  PREC_SHIFT,
  PREC_ADD,
  PREC_MULTIPLY,
  PREC_UNARY_PREFIX,
  PREC_UNARY_POSTFIX,
  PREC_MEMBER,
} Precedence;


typedef enum PFlag {
  PFlagNone   = 0,
  PFlagRValue = 1 << 0, // parsing an rvalue
  PFlagType   = 1 << 1, // parsing a type
} PFlag;


typedef struct Parselet Parselet;

typedef Node* (ParseletPrefixFun)(Parser* p, PFlag fl);
typedef Node* (ParseletFun)      (Parser* p, const Parselet* e, PFlag fl, Node* left);

typedef struct Parselet {
  ParseletPrefixFun* fprefix;
  ParseletFun*       f;
  Precedence         prec;
} Parselet;

typedef struct UnresolvedNode {
  Source* src;
  Node*   n;
} UnresolvedNode;

typedef struct Unresolved {
  Sym            sym;
  u32            nodeslen;
  u32            nodescap;
  UnresolvedNode nodes[];
} Unresolved;


#define nexttok(p) ScannerNext(&(p)->s)


// syntaxerrp reports a source-token related syntax error.
// It will point to the source location of the last-scanned token.
// If n is not NULL, use source location of n instead of current location.
//
static void syntaxerrp(Parser* p, SrcPos pos, const char* format, ...) {
  if (pos.src == NULL)
    pos = ScannerSrcPos(&p->s);

  va_list ap;
  va_start(ap, format);
  auto msg = str_new(64);
  if (strlen(format) > 0)
    msg = str_appendfmtv(msg, format, ap);
  va_end(ap);

  const char* tokname;
  if (p->s.tok == TNone) {
    tokname = "end of input";
  } else if (p->s.tok == TSemi && p->s.inp > p->s.src->body && *(p->s.inp - 1) == '\n') {
    tokname = "newline";
  } else {
    tokname = TokName(p->s.tok);
  }

  Str stmp = NULL;
  if (str_len(msg) == 0) {
    stmp = msg;
    msg = str_fmt("unexpected %s", tokname);
  } else if (str_hasprefixcstr(msg, "expecting ")) {
    stmp = msg;
    msg = str_fmt("unexpected %s, %s", tokname, msg);
  } else if (
    str_hasprefixcstr(msg, "after ") ||
    str_hasprefixcstr(msg, "in ") ||
    str_hasprefixcstr(msg, "at ")
  ) {
    stmp = msg;
    msg = str_fmt("unexpected %s %s", tokname, msg);
  }
  if (stmp)
    str_free(stmp);

  if (p->build->errh)
    p->build->errh(pos, msg, p->build->userdata);

  str_free(msg);
}


// syntaxerr = syntaxerrp(p, <srcpos of current token>, ...)
#define syntaxerr(p, format, ...) \
  syntaxerrp((p), NoSrcPos, format, ##__VA_ARGS__)


// toklistHas returns true if t is in list (list is expected to be 0-terminated)
static bool toklistHas(const Tok* list, Tok t) {
  Tok t2;
  while ((t2 = *list++)) {
    if (t2 == t) {
      return true;
    }
  }
  return false;
}

// advance consumes tokens until it finds a token of the followlist.
// It is used to recover from parse errors and is not speed critical.
//
static void advance(Parser* p, const Tok* followlist) {
  nexttok(p); // guarantee progress
  if (followlist == NULL || *followlist == 0) {
    return;
  }
  if (p->fnest > 0) {
    // Stop at keywords that start a statement.
    // They are good synchronization points in case of syntax
    // errors and (usually) shouldn't be skipped over.
    while (!toklistHas(followlist, p->s.tok)) {
      switch (p->s.tok) {
        case TNone:
        case TBreak:
        case TContinue:
        case TDefer:
        case TFor:
        case TIf:
        case TMutable:
        case TReturn:
        case TSelect:
        case TSwitch:
        case TType:
          return;
        default:
          break;
      }
      // dlog("skip %s", TokName(p->s.tok));
      nexttok(p);
    }
  } else {
    while (p->s.tok != TNone && !toklistHas(followlist, p->s.tok)) {
      // dlog("skip %s", TokName(p->s.tok));
      nexttok(p);
    }
  }
  if (p->s.tok == TSemi) {
    nexttok(p);
  }
}


// // PFreeNode is shallow; does not free node members
// inline static void PFreeNode(Parser* p, Node* n) {
//   memfree(p->build->mem, n);
// }


// allocate a new ast node
inline static Node* mknode(Parser* p, NodeKind kind) {
  auto n = NewNode(p->build->mem, kind);
  n->pos.src = p->s.src;
  n->pos.offs = p->s.tokstart - p->s.src->body;
  n->pos.span = p->s.tokend - p->s.tokstart;  assert(p->s.tokend >= p->s.tokstart);
  return n;
}

// precedence should match the calling parselet's own precedence
static Node* expr(Parser* p, int precedence, PFlag fl);

// exprOrTuple = Expr | Tuple
static Node* exprOrTuple(Parser* p, int precedence, PFlag fl);

// pushScope adds a new scope to the stack. Returns the new scope.
static Scope* pushScope(Parser* p) {
  auto s = ScopeNew(p->scope, p->build->mem);
  p->scope = s;
  #ifdef DEBUG_SCOPE_PUSH_POP
  dlog("push scope #%p", s);
  #endif
  return s;
}

// popScope removes & returns the topmost scope
static Scope* popScope(Parser* p) {
  auto s = p->scope;
  p->scope = (Scope*)s->parent; // note: discard const qual
  assert(p->scope != NULL);
  assert(p->scope != GetGlobalScope());

  #ifdef DEBUG_SCOPE_PUSH_POP
  dlog("pop scope #%p", s);
  if (s->bindings.len == 0 && s->childcount == 0) {
    dlog("  unused scope (free)");
  } else {
    dlog("  used scope (keep)");
  }
  #endif

  if (s->bindings.len == 0 && s->childcount == 0) {
    // the scope is unused and has no dependants; free it and return NULL
    ScopeFree(s, p->build->mem);
    return NULL;
  }
  if (p->scope) {
    p->scope->childcount++;
  }
  return s;
}


static const Node* defsym(Parser* p, Sym s, Node* n) {
  const Node* existing = ScopeAssoc(p->scope, s, n);

  #ifdef DEBUG_DEFSYM
  if (existing) {
    dlog("defsym %s => %s (replacing %s)", s, NodeKindName(n->kind), NodeKindName(existing->kind));
  } else {
    dlog("defsym %s => %s", s, NodeKindName(n->kind));
  }
  #endif

  return existing;
}


// If the current token is t, advances scanner and returns true.
inline static bool got(Parser* p, Tok t) {
  if (p->s.tok == t) {
    nexttok(p);
    return true;
  }
  return false;
}

// want reports a syntax error if p->s.tok != t.
// In any case, this function will advance the scanner by one token.
inline static void want(Parser* p, Tok t) {
  if (!got(p, t)) {
    syntaxerr(p, "expecting %s", TokName(t));
    nexttok(p);
  }
}

static Node* bad(Parser* p) {
  return mknode(p, NBad);
}


// tupleTrailingComma = Expr ("," Expr)* ","?
static Node* tupleTrailingComma(Parser* p, int precedence, PFlag fl, Tok stoptok) {
  auto tuple = mknode(p, NTuple);
  do {
    NodeListAppend(p->build->mem, &tuple->array.a, expr(p, precedence, fl));
  } while (got(p, TComma) && p->s.tok != stoptok);
  return tuple;
}

// ============================================================================================
// ============================================================================================
// Parselets

// PId -- identifier
// When parsing an rvalue identifier, PFlagRValue is set in fl
//
//!PrefixParselet TId
static Node* PId(Parser* p, PFlag fl) {
  assert(p->s.tok == TId);
  // Attempt to lookup rvalue identifier that references a constant or type.
  // Example:
  //   "x = true" parses as (Let (Id x) (Id true))
  //   Notice how "true" is an identifier here and not BoolLit.
  //   Unless "true" has been rebound, we instead yield the following:
  //   "x = true" parses as (Let (Id x) (BoolLit true))
  //
  //   Similarly, types are short-circuited, too. Instead of...
  //   "MyBool = bool" parses as (Let (Id MyBool) (Id bool))
  //   ...we get:
  //   "MyBool = bool" parses as (Let (Id MyBool) (Type bool))
  //

  Node* target = NULL;
  if (fl & PFlagRValue) {
    target = (Node*)ScopeLookup(p->scope, p->s.name);
    if (target == NULL) {
      p->unresolved++;
      #ifdef DEBUG_UNRESOLVED
      dlog("unresolved (rvalue) %s", p->s.name);
      #endif
    } else if (!NodeKindIsExpr(target->kind)) {
      nexttok(p);
      return target;
    }
  }

  auto n = mknode(p, NId);
  n->ref.name = p->s.name;
  n->ref.target = target;
  nexttok(p);

  if ((fl & PFlagRValue) == 0 && p->s.tok != TAssign) {
    // identifier is lvalue and not followed by '=' -- attempt to resolve
    n->ref.target = (Node*)ScopeLookup(p->scope, n->ref.name);
    if (n->ref.target == NULL) {
      #ifdef DEBUG_UNRESOLVED
      dlog("unresolved (lvalue) %s", n->ref.name);
      #endif
      p->unresolved++;
    }
  }

  return n;
}


// assignment to fields, e.g. "x.y = 3" -> (assign (Field (Id x) (Id y)) (Int 3))
static Node* pAssign(Parser* p, const Parselet* e, PFlag fl, Node* left) {
  assert(fl & PFlagRValue);
  auto n = mknode(p, NAssign);
  n->op.op = p->s.tok;
  nexttok(p); // consume '='
  auto right = exprOrTuple(p, e->prec, fl);
  n->op.left = left;
  n->op.right = right;

  // defsym
  if (left->kind == NTuple) {
    if (right->kind != NTuple) {
      syntaxerrp(p, left->pos, "assignment mismatch: %u targets but 1 value", left->array.a.len);
    } else {
      auto lnodes = &left->array.a;
      auto rnodes = &right->array.a;
      if (lnodes->len != rnodes->len) {
        syntaxerrp(p, left->pos, "assignment mismatch: %u targets but %u values",
          lnodes->len, rnodes->len);
      } else {
        auto l = lnodes->head;
        auto r = rnodes->head;
        while (l) {
          if (l->node->kind == NId) {
            defsym(p, l->node->ref.name, r->node);
          } else {
            // e.g. foo.bar = 3
            dlog("TODO pAssign l->node->kind != NId");
          }
          l = l->next;
          r = r->next;
        }
      }
    }
  } else if (right->kind == NTuple) {
    syntaxerrp(p, left->pos, "assignment mismatch: 1 target but %u values", right->array.a.len);
  } else if (left->kind == NId) {
    defsym(p, left->ref.name, right);
  }

  return n;
}


//!Parselet (TAssign ASSIGN)
static Node* PLetOrAssign(Parser* p, const Parselet* e, PFlag fl, Node* left) {
  fl |= PFlagRValue;

  if (left->kind != NId) {
    return pAssign(p, e, fl, left);
  }
  // let or var assignment
  // common case: let binding. e.g. "x = 3" -> (let (Id x) (Int 3))
  // dlog("PLetOrAssign/let %s", left->ref.name);
  nexttok(p); // consume '='

  auto name = left;
  auto value = expr(p, PREC_LOWEST, fl);

  // new let binding
  auto n = mknode(p, NLet);
  n->pos = name->pos;
  n->type = value->type;
  n->field.init = value;
  n->field.name = name->ref.name;
  defsym(p, name->ref.name, n);
  return n;
}


// Group = "(" Expr ("," Expr)* ")"
// Groups are used to control precedence.
//!PrefixParselet TLParen
static Node* PGroup(Parser* p, PFlag fl) {
  nexttok(p); // consume "("
  auto n = exprOrTuple(p, PREC_LOWEST, fl);
  want(p, TRParen);
  return n;
}


// Type (always rvalue)
static Node* pType(Parser* p, PFlag fl) {
  assert(fl & PFlagRValue);
  return exprOrTuple(p, PREC_LOWEST, fl | PFlagType);
  // syntaxerr(p, "expecting type");
  // return bad(p);
}


// As = expr "as" Type
// "as" has the lowest precedence and thus... Examples:
//
//   "9 * 2 as int8"         => (TypeCast int8 (Op * (Int 9) (Int 2)))
//   "9 * (2 as int8)"       => (Op * (Int 9) (TypeCast int8 (Int 2)))
//   "9, 2 as (int8,int8)"   => (Int 9) (TypeCast (Tuple int8 int8) (Int 2))
//   "(9, 2) as (int8,int8)" => (TypeCast (Tuple int8 int8) (Tuple (Int 9) (Int 2)))
//
//!Parselet (TAs LOWEST)
static Node* PAs(Parser* p, const Parselet* e, PFlag fl, Node* expr) {
  // assert(fl & PFlagRValue);
  fl |= PFlagRValue;
  auto n = mknode(p, NTypeCast);
  nexttok(p); // consume "as"
  n->call.receiver = pType(p, fl);
  n->call.args = expr;
  return n;
}

//!Parselet (TLParen MEMBER)
static Node* PCall(Parser* p, const Parselet* e, PFlag fl, Node* receiver) {
  auto n = mknode(p, NCall);
  nexttok(p); // consume "("
  n->call.receiver = receiver;
  auto args = tupleTrailingComma(p, PREC_LOWEST, fl, TRParen);
  want(p, TRParen);
  assert(args->kind == NTuple);
  switch (args->array.a.len) {
    case 0:  break; // leave as-is, i.e. n->call.args=NULL
    case 1:  n->call.args = args->array.a.head->node; break;
    default: n->call.args = args; break;
  }
  if (NodeKindIsType(receiver->kind)) {
    n->kind = NTypeCast;
    return n;
  }
  return n;
}

// Block = "{" Expr* "}"
//!PrefixParselet TLBrace
static Node* PBlock(Parser* p, PFlag fl) {
  auto n = mknode(p, NBlock);
  nexttok(p); // consume "{"

  pushScope(p);

  // clear rvalue flag; productions of block are lvalue
  fl &= ~PFlagRValue;

  while (p->s.tok != TNone && p->s.tok != TRBrace) {
    NodeListAppend(p->build->mem, &n->array.a, exprOrTuple(p, PREC_LOWEST, fl));
    if (!got(p, TSemi)) {
      break;
    }
  }
  if (!got(p, TRBrace)) {
    syntaxerr(p, "expecting ; or }");
    nexttok(p);
  }

  n->array.scope = popScope(p);

  return n;
}

// PrefixOp = ( "+" | "-" | "!" ) Expr
//!PrefixParselet TPlus TMinus TExcalm
static Node* PPrefixOp(Parser* p, PFlag fl) {
  auto n = mknode(p, NPrefixOp);
  n->op.op = p->s.tok;
  nexttok(p);
  n->op.left = expr(p, PREC_LOWEST, fl);
  return n;
}

// InfixOp = Expr ( "+" | "-" | "*" | "/" ) Expr
//!Parselet (TPlus ADD) (TMinus ADD)
//          (TStar MULTIPLY) (TSlash MULTIPLY)
//          (TLt COMPARE) (TGt COMPARE)
//          (TEq EQUAL) (TNEq EQUAL) (TLEq EQUAL) (TGEq EQUAL)
static Node* PInfixOp(Parser* p, const Parselet* e, PFlag fl, Node* left) {
  auto n = mknode(p, NBinOp);
  n->op.op = p->s.tok;
  n->op.left = left;
  nexttok(p);
  n->op.right = expr(p, e->prec, fl);
  return n;
}

// PostfixOp = Expr ( "++" | "--" )
//!Parselet (TPlusPlus UNARY_POSTFIX) (TMinusMinus UNARY_POSTFIX)
static Node* PPostfixOp(Parser* p, const Parselet* e, PFlag fl, Node* operand) {
  auto n = mknode(p, NPostfixOp);
  n->op.op = p->s.tok;
  n->op.left = operand;
  nexttok(p); // consume "+"
  return n;
}

// IntLit = [0-9]+
//!PrefixParselet TIntLit
static Node* PIntLit(Parser* p, PFlag fl) {
  auto n = mknode(p, NIntLit);
  size_t len = p->s.tokend - p->s.tokstart;
  if (!parseu64((const char*)p->s.tokstart, len, /*base*/10, &n->val.i)) {
    n->val.i = 0;
    syntaxerrp(p, n->pos, "invalid integer literal");
  }
  nexttok(p);
  n->val.ct = CType_int;
  n->type = Type_ideal;
  return n;
}

// If = "if" Expr Expr
//!PrefixParselet TIf
static Node* PIf(Parser* p, PFlag fl) {
  auto n = mknode(p, NIf);
  nexttok(p);
  n->cond.cond = expr(p, PREC_LOWEST, fl);
  n->cond.thenb = expr(p, PREC_LOWEST, fl);
  if (p->s.tok == TElse) {
    nexttok(p);
    n->cond.elseb = expr(p, PREC_LOWEST, fl);
  }
  // optimize simple constant conditions e.g. (if true A else B) => A
  if (p->s.flags & ParseOpt)
    n = ast_opt_ifcond(n);
  return n;
}

// Return = "return" Expr?
//!PrefixParselet TReturn
static Node* PReturn(Parser* p, PFlag fl) {
  auto n = mknode(p, NReturn);
  nexttok(p);
  if (p->s.tok != TSemi && p->s.tok != TRBrace)
    n->op.left = exprOrTuple(p, PREC_LOWEST, fl | PFlagRValue);
  return n;
}


// params = "(" param ("," param)* ","? ")"
// param  = Id Type? | Type
//
static Node* params(Parser* p) { // => NTuple
  // examples:
  //
  // (T)
  // (x T)
  // (x, y, z T)
  // (... T)
  // (x  ... T)
  // (x, y, z  ... T)
  // (T1, T2, T3)
  // (T1, T2, ... T3)
  //
  want(p, TLParen);
  auto n = mknode(p, NTuple);
  bool hasTypedParam = false; // true when at least one param has type; e.g. "x T"
  NodeList typeq = {0};
  PFlag fl = PFlagRValue;

  while (p->s.tok != TRParen && p->s.tok != TNone) {
    auto field = mknode(p, NArg);
    if (p->s.tok == TId) {
      field->field.name = p->s.name;
      nexttok(p);
      // TODO: check if "<" follows. If so, this is a type.
      if (p->s.tok != TRParen && p->s.tok != TComma && p->s.tok != TSemi) {
        field->type = expr(p, PREC_LOWEST, fl);
        hasTypedParam = true;
        // spread type to predecessors
        if (typeq.len > 0) {
          NodeListForEach(&typeq, field2, {
            field2->type = field->type;
          });
          NodeListClear(&typeq);
        }
      } else {
        NodeListAppend(p->build->mem, &typeq, field);
      }
    } else {
      // definitely just type, e.g. "fun(int)int"
      field->type = expr(p, PREC_LOWEST, fl);
    }
    NodeListAppend(p->build->mem, &n->array.a, field);
    if (!got(p, TComma)) {
      if (p->s.tok != TRParen) {
        syntaxerr(p, "expecting comma or )");
        nexttok(p);
      }
      break;
    }
  }

  if (hasTypedParam) {
    // name-and-type form; e.g. "(x, y T, z Y)"
    if (typeq.len > 0) {
      // at least one param has type, but the last one does not.
      // e.g. "(x, y int, z)"
      syntaxerr(p, "expecting type");
    }
    u32 index = 0;
    NodeListForEach(&n->array.a, field, {
      field->field.index = index++;
      defsym(p, field->field.name, field);
    });
  } else {
    // type-only form; e.g. "(T, T, Y)"
    // make ident of each field->field.name where field->type == NULL
    u32 index = 0;
    NodeListForEach(&n->array.a, field, {
      if (!field->type) {
        auto t = mknode(p, NId);
        t->ref.name = field->field.name;
        field->type = t;
        field->field.name = sym__;
        field->field.index = index++;
      }
    });
  }

  want(p, TRParen);
  return n;
}


// Fun     = FunDef | FunExpr
// FunDef  = "fun" Id? params? Type? Block?
// FunExpr = "fun" Id? params? Type? "->" Expr
//
// e.g.
//   fun foo (x, y int) int
//   fun foo (x, y int) int { x * y }
//   fun foo { 5 }
//   fun foo -> 5
//   fun (x, y int) int { x * y }
//   fun { 5 }
//   fun -> 5
//
//!PrefixParselet TFun
static Node* PFun(Parser* p, PFlag fl) {
  auto n = mknode(p, NFun);
  nexttok(p);
  // name
  if (p->s.tok == TId) {
    auto name = p->s.name;
    n->fun.name = name;
    defsym(p, name, n);
    nexttok(p);
  } else if ((fl & PFlagRValue) == 0) {
    syntaxerr(p, "expecting name");
    nexttok(p);
  }
  // parameters
  pushScope(p);
  if (p->s.tok == TLParen) {
    auto pa = params(p);
    assert(pa->kind == NTuple);
    switch (pa->array.a.len) {
      case 0:  break; // leave as-is, i.e. n->fun.params=NULL
      case 1:  n->fun.params = pa->array.a.head->node; break;
      default: n->fun.params = pa; break;
    }
  }
  // result type(s)
  if (p->s.tok != TLBrace && p->s.tok != TSemi && p->s.tok != TRArr) {
    n->type = pType(p, fl | PFlagRValue);
  }
  // body
  p->fnest++;
  if (p->s.tok == TLBrace) {
    n->fun.body = PBlock(p, fl);
  } else if (got(p, TRArr)) {
    n->fun.body = exprOrTuple(p, PREC_LOWEST, fl & ~PFlagRValue /* lvalue semantics */);
  }
  p->fnest--;
  n->fun.scope = popScope(p);
  return n;
}

// end of parselets
// ============================================================================================
// ============================================================================================


//PARSELET_MAP_BEGIN
// automatically generated by misc/gen_parselet_map.py; do not edit
static const Parselet parselets[TMax] = {
  [TId] = {PId, NULL, PREC_MEMBER},
  [TLParen] = {PGroup, PCall, PREC_MEMBER},
  [TLBrace] = {PBlock, NULL, PREC_MEMBER},
  [TPlus] = {PPrefixOp, PInfixOp, PREC_ADD},
  [TMinus] = {PPrefixOp, PInfixOp, PREC_ADD},
  [TExcalm] = {PPrefixOp, NULL, PREC_MEMBER},
  [TIntLit] = {PIntLit, NULL, PREC_MEMBER},
  [TIf] = {PIf, NULL, PREC_MEMBER},
  [TReturn] = {PReturn, NULL, PREC_MEMBER},
  [TFun] = {PFun, NULL, PREC_MEMBER},
  [TAssign] = {NULL, PLetOrAssign, PREC_ASSIGN},
  [TAs] = {NULL, PAs, PREC_LOWEST},
  [TStar] = {NULL, PInfixOp, PREC_MULTIPLY},
  [TSlash] = {NULL, PInfixOp, PREC_MULTIPLY},
  [TLt] = {NULL, PInfixOp, PREC_COMPARE},
  [TGt] = {NULL, PInfixOp, PREC_COMPARE},
  [TEq] = {NULL, PInfixOp, PREC_EQUAL},
  [TNEq] = {NULL, PInfixOp, PREC_EQUAL},
  [TLEq] = {NULL, PInfixOp, PREC_EQUAL},
  [TGEq] = {NULL, PInfixOp, PREC_EQUAL},
  [TPlusPlus] = {NULL, PPostfixOp, PREC_UNARY_POSTFIX},
  [TMinusMinus] = {NULL, PPostfixOp, PREC_UNARY_POSTFIX},
};
//PARSELET_MAP_END


inline static Node* prefixExpr(Parser* p, PFlag fl) {
  // find prefix parselet
  assert((u32)p->s.tok < (u32)TMax);
  auto parselet = &parselets[p->s.tok];
  if (!parselet->fprefix) {
    syntaxerr(p, "expecting expression");
    auto n = bad(p);
    Tok followlist[] = { TRParen, TRBrace, TRBrack, TSemi, 0 };
    advance(p, followlist);
    return n;
  }
  return parselet->fprefix(p, fl);
}


inline static Node* infixExpr(Parser* p, int precedence, PFlag fl, Node* left) {
  // wrap parselets
  // TODO: Should we set fl|PFlagRValue here?
  while (p->s.tok != TNone) {
    auto parselet = &parselets[p->s.tok];
    // if (parselet->f) {
    //   dlog("found infix parselet for %s; parselet->prec=%d < precedence=%d = %s",
    //     TokName(p->s.tok), parselet->prec, precedence, parselet->prec < precedence ? "Y" : "N");
    // }
    if ((int)parselet->prec < precedence || !parselet->f) {
      break;
    }
    assert(parselet);
    left = parselet->f(p, parselet, fl, left);
  }
  return left;
}


static Node* expr(Parser* p, int precedence, PFlag fl) {
  // Note: precedence should match the calling parselet's own precedence
  auto left = prefixExpr(p, fl);
  return infixExpr(p, precedence, fl, left);
}


// exprOrTuple = Expr | Tuple
//
// This function has different behavior depending on PFlagRValue:
//
//   PFlagRValue=OFF consumes a prefixExpr, then a possible tuple and finally calls
//   infixExpr to include the tuple in an infix expression like t + y.
//
//   - PFlagRValue=OFF is "conservative" used for lvalues, e.g. (a b c) in a,b,c=1,2,3
//   - PFlagRValue=ON is "greedy" and used for rvalues, e.g. (x (y + z)) in _,_=x,y+z
//
//   Consider the following source code:
//     a, b + c, d
//   Parsing this with the different functions yields:
//   - PFlagRValue=OFF => (+ (a b) c)
//   - PFlagRValue=ON => (a (+ b c) d)
//
//   Explanation of PFlagRValue=OFF:
//   • PFlagRValue=OFF calls prefixExpr => a
//   • PFlagRValue=OFF sees a comma and begins tuple-parsing mode:
//   • PFlagRValue=OFF calls prefixExpr => b
//   • PFlagRValue=OFF end tuple => (a b)
//   • infixExpr with tuple as the LHS:
//     • infixExpr calls '+' parselet
//       • '+' parselet reads RHS by calling expr:
//         • expr calls prefixExpr (which in turn calls 'ident' parselet) => c
//         • expr returns the c identifier (NId)
//       • '+' parselet produces LHS + RHS => (+ (a b) c)
//     • return
//   • return
//
//   Explanation of PFlagRValue=ON:
//   • PFlagRValue=ON calls expr => a
//   • PFlagRValue=ON sees a comma and begins tuple-parsing mode:
//   • PFlagRValue=ON calls expr
//     • expr calls prefixExpr => b
//     • expr calls infixExpr with b as LHS:
//       • infixExpr calls '+' parselet
//         • '+' parselet reads RHS by calling expr:
//           • expr calls prefixExpr => c
//           • expr returns the c identifier (NId)
//         • '+' parselet produces LHS + RHS => (+ b c)
//       • return
//     • return
//   • PFlagRValue=ON calls expr after another comma
//     • calls prefixExpr => b
//   • PFlagRValue=ON see no more comma; ends the tuple => (a (+ b c) d)
//
static Node* exprOrTuple(Parser* p, int precedence, PFlag fl) {
  auto left = (
    fl & PFlagRValue ? expr(p, precedence, fl) :
                       prefixExpr(p, fl) ); // read a prefix expression, like an identifier
  if (got(p, TComma)) {
    auto g = mknode(p, fl & PFlagType ? NTupleType : NTuple);
    NodeListAppend(p->build->mem, &g->array.a, left);
    if (fl & PFlagRValue) {
      do {
        NodeListAppend(p->build->mem, &g->array.a, expr(p, precedence, fl));
      } while (got(p, TComma));
    } else {
      do {
        NodeListAppend(p->build->mem, &g->array.a, prefixExpr(p, fl));
      } while (got(p, TComma));
    }
    left = g;
  }
  if (fl & PFlagRValue) {
    return left;
  }
  // wrap in possible infix expression, e.g. "left + right"
  return infixExpr(p, precedence, fl, left);
}


Node* Parse(Parser* p, BuildCtx* ctx, Source* src, ParseFlags fl, Scope* pkgscope) {
  // initialize scanner
  ScannerInit(&p->s, ctx, src, fl);
  p->build = ctx;
  p->scope = pkgscope;
  p->fnest = 0;
  p->unresolved = 0;

  // read first token
  nexttok(p);

  // TODO: ParseFlags, where one option is PARSE_IMPORTS to parse only imports and then stop.

  auto file = mknode(p, NFile);
  pushScope(p);

  while (p->s.tok != TNone) {
    Node* n = exprOrTuple(p, PREC_LOWEST, PFlagNone);
    NodeListAppend(p->build->mem, &file->array.a, n);

    // // print associated comments
    // auto c = p->s.comments;
    // while (c) { printf("#%.*s\n", (int)c->len, c->ptr); c = c->next; }
    // p->s.comments = p->s.comments_tail = NULL;
    // // TODO: Add "comments" to Node struct and if caller requests inclusion of comments,
    // // assign these comments to the node. This should be done in mknode and not here.

    // check that we either got a semicolon or EOF
    if (p->s.tok != TNone && !got(p, TSemi)) {
      syntaxerr(p, "after top level declaration");
      Tok followlist[] = { TType, TFun, TSemi, 0 };
      advance(p, followlist);
    }
  }

  file->array.scope = popScope(p);
  return file;
}