// error codes
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

typedef i32 error;
#define CO_FOREACH_ERROR(_) \
  _(ok            , "(no error)") \
  _(invalid       , "invalid data or argument") \
  _(sys_op        , "invalid syscall op or syscall op data") \
  _(badfd         , "invalid file descriptor") \
  _(bad_name      , "invalid or misformed name") \
  _(not_found     , "not found") \
  _(name_too_long , "name too long") \
  _(canceled      , "operation canceled") \
  _(not_supported , "not supported") \
  _(exists        , "already exists") \
  _(access        , "permission denied") \
  _(nomem         , "cannot allocate memory") \
  _(nospace       , "no space left") \
  _(mfault        , "bad memory address") \
  _(overflow      , "value too large") \
// end CO_FOREACH_ERROR

enum _co_error_tmp_ { // generate positive values
  #define _(NAME, ...) _err_##NAME,
  CO_FOREACH_ERROR(_)
  #undef _
};
enum _co_error { // canonical negative values
  #define _(NAME, ...) err_##NAME = - _err_##NAME,
  CO_FOREACH_ERROR(_)
  #undef _
};
// note: this file is included by llvm/*.cc, so enum names must differ from typedef

EXTERN_C error error_from_errno(int errno);
EXTERN_C const char* error_str(error);

END_INTERFACE
