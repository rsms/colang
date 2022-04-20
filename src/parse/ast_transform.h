// AST transformer
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

Node* atr_visit_template(BuildCtx* build, TemplateNode* tpl, NodeArray* tplvals);

END_INTERFACE
