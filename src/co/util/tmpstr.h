#pragma once
ASSUME_NONNULL_BEGIN

// TMPSTR_MAX_CONCURRENCY is the limit of concurrent valid buffers returned by tmpstr_get.
// For example, if TMPSTR_MAX_CONCURRENCY==2 then:
//   Str* a = tmpstr_get();
//   Str* b = tmpstr_get();
//   Str* c = tmpstr_get(); // same as a
//
#define TMPSTR_MAX_CONCURRENCY 8

// tmpstr_get allocates the next temporary string buffer.
// It is thread safe.
//
// Strs returned by this function are managed in a circular-buffer fashion; calling tmpstr_get
// many times will eventually return the same Str. See TMPSTR_MAX_CONCURRENCY.
//
// If you return a temporary string to a caller, make sure to annotate its type as ConstStr
// to communicate that the user must not modify it.
// 
// Example:
//   ConstStr fmtnode(const Node* n) {
//     Str* sp = tmpstr_get(); // allocate
//     *sp = NodeStr(*sp, n);  // use and update pointer
//     return *sp;             // return to user
//   }
//
Str* tmpstr_get();


ASSUME_NONNULL_END
