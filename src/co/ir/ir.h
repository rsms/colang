#pragma once
#include "../util/array.h"
#include "../build.h"
#include "../types.h"
#include "op.h"

ASSUME_NONNULL_BEGIN


typedef enum IRBlockKind {
  IRBlockInvalid = 0,
  IRBlockCont,     // plain block with a single successor
  IRBlockFirst,    // 2 successors, always takes the first one (second is dead)
  IRBlockIf,       // 2 successors, if control goto succs[0] else goto succs[1]
  IRBlockRet,      // no successors, control value is memory result
} IRBlockKind;

typedef enum IRBranchPrediction {
  IRBranchUnlikely = -1,
  IRBranchUnknown  = 0,
  IRBranchLikely   = 1,
} IRBranchPrediction;


typedef struct IRPkg   IRPkg;
typedef struct IRFun   IRFun;
typedef struct IRBlock IRBlock;
typedef struct IRValue IRValue;


// Edge represents a CFG edge
typedef struct IREdge { int TODO; } IREdge;


// IRConstCache is used internally by IRFun (fun.c) and holds constants
typedef struct IRConstCache {
  u32   bmap;       // maps TypeCode => branch array index
  void* branches[]; // dense branch array
} IRConstCache;


typedef struct IRValue {
  u32      id;   // unique identifier
  IROp     op;   // operation that computes this value
  TypeCode type;
  SrcPos   pos;  // source position
  IRValue* args[3]; u8 argslen; // arguments
  union {
    i64 auxInt; // floats are stored as reinterpreted bits
  };
  u32 uses; // use count. Each appearance in args or IRBlock.control counts once.
  char* nullable comment; // short comment for IR formatting. Likely NULL. (memalloc)
} IRValue;


// Block represents a basic block
typedef struct IRBlock {
  IRFun*      f;        // owning function
  u32         id;       // block ID
  IRBlockKind kind;     // kind of block
  bool        sealed;   // true if no further predecessors will be added
  SrcPos      pos;      // source position
  const char* comment;  // short comment for IR formatting. May be NULL.
  IRBlock*    succs[2]; // Successor/subsequent blocks (CFG)
  IRBlock*    preds[2]; // Predecessors (CFG)

  // three-address code values
  Array values; void* valuesStorage[8]; // IRValue*[]

  // control is a value that determines how the block is exited.
  // Its value depends on the kind of the block. For instance, a IRBlockIf has a boolean
  // control value and IRBlockExit has a memory control value.
  IRValue* control;

} IRBlock;


// Fun represents a function
typedef struct IRFun {
  Mem nullable mem; // owning allocator
  Array        blocks; void* blocksStorage[4]; // IRBlock*[]
  Sym          typeid; // TypeCode encoding
  Sym nullable name;
  SrcPos       pos;     // source position
  u32          nparams; // number of parameters

  // internal; valid only during building
  u32    bid;    // block ID allocator
  u32    vid;    // value ID allocator
  IRConstCache* consts; // constants cache maps type+value => IRValue
} IRFun;


// Pkg represents a package with functions and data
typedef struct IRPkg {
  Mem nullable mem; // owning allocator
  const char*  id;  // c-string. "_" if NULL is passed for name to IRPkgNew. (TODO use Sym?)
  // TODO: Move the PtrMap funs from builder here. Need to make PtrMap use Mem.
  Array funs; void* funsStorage[4]; // IRFun*[]
} IRPkg;


IRPkg* IRPkgNew(Mem nullable, const char* name/*null*/);
void   IRPkgAddFun(IRPkg* pkg, IRFun* f);

IRFun*   IRFunNew(Mem nullable mem, Sym typeid, Sym nullable name, SrcPos pos, u32 nparams);
IRValue* IRFunGetConstBool(IRFun* f, bool value);
IRValue* IRFunGetConstInt(IRFun* f, TypeCode t, u64 n);
IRValue* IRFunGetConstFloat(IRFun* f, TypeCode t, double n);
void     IRFunInvalidateCFG(IRFun*);
void     IRFunMoveBlockToEnd(IRFun*, u32 blockIndex); // moves block at index to end of f->blocks

IRBlock* IRBlockNew(IRFun* f, IRBlockKind, const SrcPos* nullable pos);
void IRBlockDiscard(IRBlock* b); // removes it from b->f and frees memory of b.
void IRBlockAddValue(IRBlock* b, IRValue* v);
void IRBlockSetControl(IRBlock* b, IRValue* v/*pass null to clear*/);
void IRBlockAddEdgeTo(IRBlock* b1, IRBlock* b2); // add an edge from b1 to successor block b2
void IRBlockSetPred(IRBlock* b, u32 index, IRBlock* pred);
void IRBlockDelPred(IRBlock* b, u32 index);
void IRBlockSetSucc(IRBlock* b, u32 index, IRBlock* succ);
void IRBlockDelSucc(IRBlock* b, u32 index);

IRValue* IRValueNew(IRFun*, IRBlock* nullable b, IROp, TypeCode, const SrcPos* nullable pos);
void IRValueAddComment(IRValue* v, Mem nullable, ConstStr comment);
void IRValueAddArg(IRValue* v, IRValue* arg);


// IRReprPkgStr appends to append_to_str a human-readable representation of a package's IR.
Str IRReprPkgStr(const IRPkg* f, Str append_to_str);


// Note: Must use the same Mem for all calls to the same IRConstCache.
// Note: addHint is only valid until the next call to a mutating function like Add.
IRValue* nullable IRConstCacheGet(
  const IRConstCache*, Mem nullable mem, TypeCode, u64 value, int* out_addHint);
IRConstCache* IRConstCacheAdd(
  IRConstCache* c, Mem nullable, TypeCode t, u64 value, IRValue* v, int addHint);

ASSUME_NONNULL_END
