// quick sort
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

// xqsort is qsort_r aka qsort_s
typedef int(*xqsort_cmp)(const void* x, const void* y, void* nullable ctx);
void xqsort(void* base, usize nmemb, usize size, xqsort_cmp cmp, void* nullable ctx);

END_INTERFACE
