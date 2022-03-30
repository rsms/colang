// parser, converting source text into AST (via Scanner)
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

typedef struct Parser Parser;     // parser state (includes Scanner)

// Parser holds state used during parsing
struct Parser {
  Scanner; // parser is based on a scanner

  Scope* pkgscope; // package-level scope
  u32    fnest;    // function nesting level
  error  err;      // !0 if a fatal error occurred (e.g. memory allocation failed)

  // set when parsing named type e.g. "type Foo ..."
  Sym nullable typename;

  // ctxtype is non-null when the parser is confident about the type context
  Type* nullable ctxtype;

  // scopestack is used for tracking identifiers during parsing.
  // This is a simple stack which we do a linear search on when looking up identifiers.
  // It is faster than using chained hash maps in most cases because of cache locality
  // and the fact that...
  // 1. Most identifiers reference an identifier defined nearby. For example:
  //      x = 3
  //      A = x + 5
  //      B = x - 5
  // 2. Most bindings are short-lived and temporary ("locals") which means we can
  //    simply change a single index pointer to "unwind" an entire scope of bindings and
  //    then reuse that memory for the next binding scope.
  //
  // base is the offset in ptr to the current scope's base. Loading ptr[base] yields a uintptr
  // that is the next scope's base index.
  // keys (Sym) and values (Node) are interleaved in ptr together with saved base pointers.
  struct {
    uintptr cap;          // capacity of ptr (count, not bytes)
    uintptr len;          // current length (use) of ptr
    uintptr base;         // current scope's base index into ptr
    void**  ptr;          // entries
    void*   storage[256]; // initial storage in parser's memory
  } scopestack;
};

// Scanner must be the head of the Parser struct to support parser scan state save & restore
static_assert(offsetof(Parser,build) == offsetof(Scanner,build),
  "Scanner is not the head of the Parser struct");


// parse_cunit parses a translation unit, producing AST at *result.
// Expects p to be zero-initialized on first call. Can reuse p after return.
error parse_tu(Parser* p, BuildCtx*, Source*, ParseFlags, Scope* pkgscope, FileNode** result)
  WARN_UNUSED_RESULT;


END_INTERFACE
