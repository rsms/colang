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
  Mem            mem;   // memory for all IR data constructed by this builder
  Build*         build; // current source context (source-file specific)
  IRBuilderFlags flags;
  IRPkg*         pkg;
  PtrMap         typecache; // IRType* interning keyed on AST Type* which is interned

  // state used during building
  IRBlock* b; // current block
  IRFun*   f; // current function

  // vas holds variable assignments in the current block (map from variable symbol to ssa value).
  // This map is moved into defvars when a block ends (internal call to endBlock.)
  SymMap* vars; // Sym => IRValue*

  // defvars maps block id to defined variables at the end of each block.
  // A NULL entry indicates there are no variables in that block.
  Array defvars; void* defvarsStorage[512]; // [block_id] => SymMap* from vars field

  // funstack is used for saving current function generation state when stumbling upon
  // a call op to a not-yet-generated function.
  Array funstack; void* funstackStorage[8];

  // incompletePhis tracks pending, incomplete phis that are completed by sealBlock for
  // blocks that are sealed after they have started. This happens when preds are not known
  // at the time a block starts, but is known and registered before the block ends.
  //incompletePhis :Map<Block,Map<ByteStr,Value>>|null

} IRBuilder;

// IRBuilderInit starts a new IRPkg.
// Initially b must be zeroed memory. You can pass a previously-used b to recycle it.
// Returns false if OS memory allocation failed.
bool IRBuilderInit(IRBuilder* b, Build* build, IRBuilderFlags flags);

// IRBuilderDispose frees all memory allocated by b.
void IRBuilderDispose(IRBuilder* b);

// IRBuilderAddAST adds a top-level AST node to the current IRPkg.
// Returns false if any errors occurred.
// After AST has been added, the AST's memory may be freed as IR does not reference the AST.
// It does however hold on to references to symbols (Sym) but those are usually allocated
// separately from the AST.
bool IRBuilderAddAST(IRBuilder*, Node*);

// IROpFromAST performs a lookup of IROp based on one or two inputs (type1 & type2)
// along with an AST operator (astOp). For single-input, set type2 to TypeCode_nil.
// Returns OpNil if there is no corresponding IR operation.
IROp IROpFromAST(Tok astOp, TypeCode type1, TypeCode type2);
