// typeid identifies types
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

// typeid_make appends a type ID string for n to buf.
// It writes at most bufsize-1 of the characters to the output buf (the bufsize'th
// character then gets the terminating '\0'). If the return value is greater than or
// equal to the bufsize argument, buf was too short and some of the characters were
// discarded. The output is always null-terminated, unless size is 0.
#define typeid_make(buf, bufsize, t) _typeid_make((buf),(bufsize),as_Type(t))
usize _typeid_make(char* buf, usize bufsize, const Type* t);


END_INTERFACE
