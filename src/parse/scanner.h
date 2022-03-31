// lexical scanner; converts source text into tokens that the parser can read
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

typedef struct Scanner Scanner; // lexical scanner state
typedef struct Comment Comment; // source comment
typedef struct Indent  Indent;  // source indentation


// ParseFlags controls scanner and parser behavior
typedef u8 ParseFlags;
enum ParseFlags {
  ParseComments = 1 << 1, // parse comments, populating S.comments_{head,tail}
  ParseOpt      = 1 << 2, // apply optimizations. might produce a non-1:1 AST/token stream
} END_ENUM(ParseFlags)


// Comment is a scanned comment
struct Comment {
  struct Comment* next; // next comment in linked list
  Source*         src;  // source
  const u8*       ptr;  // ptr into source
  u32             len;  // byte length
};

// Indent tracks source indetation
struct Indent {
  bool isblock; // true if this indent is a block
  u32  n;       // number of whitespace chars
};

// Scanner reads source code and produces tokens
struct Scanner {
  BuildCtx*  build;        // build context (memory allocator, sympool, pkg, etc.)
  Source*    src;          // input source
  u32        srcposorigin;
  ParseFlags flags;
  bool       insertSemi;   // insert a semicolon before next newline
  const u8*  inp;          // input buffer current pointer
  const u8*  inend;        // input buffer end

  // indentation
  Indent indent;           // current level
  Indent indentDst;        // unwind to level
  struct { // previous indentation levels (Indent elements)
    Indent* v;
    u32     len;
    u32     cap;
    Indent  storage[16];
  } indentStack;

  // token
  Tok        tok;           // current token
  const u8*  tokstart;      // start of current token
  const u8*  tokend;        // end of current token
  const u8*  prevtokend;    // end of previous token
  Sym        name;          // Current name (valid for TId and keywords)

  u32        lineno;        // source position line
  const u8*  linestart;     // source position line start pointer (for column)

  Comment*   comments_head; // linked list head of comments scanned so far
  Comment*   comments_tail; // linked list tail of comments scanned so far
};


// ScannerInit initializes a scanner. Returns false if SourceOpenBody fails.
error ScannerInit(Scanner*, BuildCtx*, Source*, ParseFlags) WARN_UNUSED_RESULT;

// ScannerDispose frees internal memory of s.
// Caller is responsible for calling SourceCloseBody as ScannerInit calls SourceOpenBody.
void ScannerDispose(Scanner*);

// ScannerNext scans the next token
Tok ScannerNext(Scanner*);

// ScannerPos returns the source position of s->tok (current token)
Pos ScannerPos(const Scanner* s);

// ScannerTokStr returns a token's string value and length, which is a pointer
// into the source's body.
inline static const u8* ScannerTokStr(const Scanner* s, usize* len_out) {
  *len_out = (usize)(s->tokend - s->tokstart);
  return s->tokstart;
}

// ScannerCommentPop removes and returns the least recently scanned comment.
// The caller takes ownership of the comment and should free it using memfree(s->mem,comment).
Comment* nullable ScannerCommentPop(Scanner* s);


END_INTERFACE
