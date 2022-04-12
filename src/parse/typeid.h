// type identifier
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

// typeid_append appends a type ID string for n to dst.
// Returns false on memory allocation failure.
#define typeid_append(dst, t) _typeid_append((dst),as_Type(t))
bool _typeid_append(Str* dst, const Type* t);


END_INTERFACE
