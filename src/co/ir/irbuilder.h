#pragma once
#include "../util/array.h"
#include "../util/ptrmap.h"
#include "../util/symmap.h"
#include "../build.h"
#include "../parse/parse.h"


typedef enum IRBuilderFlags {
  IRBuilderDefault  = 0,
  IRBuilderComments = 1 << 1,  // include comments in some values, for formatting
  IRBuilderOpt      = 1 << 2,  // apply construction-pass [optimization]s
} IRBuilderFlags;


typedef struct IRBuilder {
  Build*         build; // current source context (source-file specific)
  Mem            mem;   // memory for all IR data constructed by this builder
  IRBuilderFlags flags;
  IRPkg*         pkg;

  // state used during building
  IRBlock* b;     // current block
  IRFun*   f;     // current function

  SymMap* vars; // Sym => IRValue*
    // variable assignments in the current block (map from variable symbol to ssa value)
    // this PtrMap is moved into defvars when a block ends (internal call to endBlock.)

  Array defvars; void* defvarsStorage[512]; // list of SymMap*  (from vars)
    // all defined variables at the end of each block. Indexed by block id.
    // null indicates there are no variables in that block.

  Array funstack; void* funstackStorage[8];
    // used for saving current function generation state when stumbling upon a call op
    // to a not-yet-generated function.

  // incompletePhis :Map<Block,Map<ByteStr,Value>>|null
    // tracks pending, incomplete phis that are completed by sealBlock for
    // blocks that are sealed after they have started. This happens when preds
    // are not known at the time a block starts, but is known and registered
    // before the block ends.

} IRBuilder;

// start a new IRPkg.
// b must be zeroed memory or a reused builder.
void IRBuilderInit(IRBuilder*, Build*, IRBuilderFlags flags);
void IRBuilderDispose(IRBuilder*); // frees b->mem

// -------------------------
// Co AST-specific functions

// IRBuilderAddAST adds a top-level AST node to the current IRPkg.
// Returns false if any errors occured.
bool IRBuilderAddAST(IRBuilder*, Node*);

// IROpFromAST performs a lookup of IROp based on one or two inputs (type1 & type2)
// along with an AST operator (astOp). For single-input, set type2 to TypeCode_nil.
// Returns OpNil if there is no corresponding IR operation.
IROp IROpFromAST(Tok astOp, TypeCode type1, TypeCode type2);
