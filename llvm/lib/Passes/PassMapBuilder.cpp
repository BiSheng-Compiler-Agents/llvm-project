//===- PassMapBuilder.cpp - Creates Map from Pass to Pass Type ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2024, Huawei Technologies Co., Ltd. All rights reserved.
//
//===----------------------------------------------------------------------===//
//
// Maps pass to its corresponding Pass Type (ie. Module, CGSCC, Function, and
// Loop) to be used for Pass Order Nesting in PhaseOrder.cpp
//
//===----------------------------------------------------------------------===//

#include "llvm/Passes/PassMapBuilder.h"
#include <unordered_map>

static void addToPassMap(std::string PassName, std::string PassType,
                         std::unordered_map<std::string, std::string> &Out) {
  if (Out[PassName] == "")
    Out[PassName] = PassType;
}

std::unordered_map<std::string, std::string> initializePassMap() {
  std::unordered_map<std::string, std::string> Out;
#define MODULE_PASS(NAME, CREATE_PASS) addToPassMap(NAME, "module", Out);
#include "PassRegistry.def"

#define MODULE_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)      \
  addToPassMap(NAME, "module", Out);
#include "PassRegistry.def"

#define MODULE_ANALYSIS(NAME, CREATE_PASS) addToPassMap(NAME, "module", Out);
#include "PassRegistry.def"

#define MODULE_ALIAS_ANALYSIS(NAME, CREATE_PASS)                               \
  addToPassMap(NAME, "module", Out);
#include "PassRegistry.def"

#define CGSCC_PASS(NAME, CREATE_PASS) addToPassMap(NAME, "cgscc", Out);
#include "PassRegistry.def"

#define CGSCC_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)       \
  addToPassMap(NAME, "cgscc", Out);
#include "PassRegistry.def"

#define CGSCC_ANALYSIS(NAME, CREATE_PASS) addToPassMap(NAME, "cgscc", Out);
#include "PassRegistry.def"

#define FUNCTION_PASS(NAME, CREATE_PASS) addToPassMap(NAME, "function", Out);
#include "PassRegistry.def"

#define FUNCTION_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)    \
  addToPassMap(NAME, "function", Out);
#include "PassRegistry.def"

#define FUNCTION_ANALYSIS(NAME, CREATE_PASS)                                   \
  addToPassMap(NAME, "function", Out);
#include "PassRegistry.def"

#define FUNCTION_ALIAS_ANALYSIS(NAME, CREATE_PASS)                             \
  addToPassMap(NAME, "function", Out);
#include "PassRegistry.def"

#define LOOPNEST_PASS(NAME, CREATE_PASS) addToPassMap(NAME, "loop", Out);
#include "PassRegistry.def"

#define LOOP_PASS(NAME, CREATE_PASS) addToPassMap(NAME, "loop", Out);
#include "PassRegistry.def"

#define LOOP_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)        \
  addToPassMap(NAME, "loop", Out);
#include "PassRegistry.def"

#define LOOP_ANALYSIS(NAME, CREATE_PASS) addToPassMap(NAME, "loop", Out);
#include "PassRegistry.def"

  return Out;
}
