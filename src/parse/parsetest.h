// parser tests
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

#if defined(CO_TESTING_ENABLED) && !defined(CO_NO_LIBC)
  int parse_test_main(int argc, const char** argv);
#else
  inline static int parse_test_main(int argc, const char** argv) { return 0; }
#endif

END_INTERFACE
