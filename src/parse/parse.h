// Co parser package
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
#include "../colib.h"

#include "token.h"    // Tok { T* }
#include "type.h"     // TypeCode { TC_* }, TypeFlags { TF_*}
#include "source.h"   // Source, Pkg
#include "pos.h"      // Pos, PosMap, PosSpan
#include "ast.h"      // Scope, Node types, NodeKind { N* }, NodeFlags { NF_* }
#include "typeid.h"
#include "buildctx.h" // BuildCtx, Diagnostic, DiagLevel
#include "universe.h"
#include "ctypecast.h"
#include "eval.h"
#include "resolve.h"
#include "scanner.h"
#include "parser.h"
