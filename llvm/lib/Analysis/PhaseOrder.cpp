//===- PhaseOrder.cpp - The LLVM Modular Optimizer ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2024, Huawei Technologies Co., Ltd. All rights reserved.
//
//===----------------------------------------------------------------------===//
//
// Generates set of recipes to be used for optimization
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/PhaseOrder.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <random>
#include <sstream>
#include <unordered_map>

#define REGISTER_RECIPE_TO_PASSES(RECIPE, ...)                                 \
  { PhaseOrderGeneratorBase::Recipe::RECIPE, #__VA_ARGS__ }
// TODO: For some reason when trying to commit this file there will be
// formatting issues.
// TODO: Rightnow the __VA_ARGS__ has to be comma separated WITHOUT any spaces.
// Find a way to get rid of all the spaces.
std::unordered_map<PhaseOrderGeneratorBase::Recipe, std::string>
    PhaseOrderGeneratorBase::RecipeToPassOrders{
      REGISTER_RECIPE_TO_PASSES(A, loop-simplify,loop-unroll<O3;partial>,),
      REGISTER_RECIPE_TO_PASSES(B, inline,),
      REGISTER_RECIPE_TO_PASSES(C, argpromotion,),
};
#undef REGISTER_RECIPE_TO_PASSES

#define REGISTER_RECIPE_TO_STRING(RECIPE)                                      \
  { PhaseOrderGeneratorBase::Recipe::RECIPE, #RECIPE }
std::unordered_map<PhaseOrderGeneratorBase::Recipe, std::string>
    PhaseOrderGeneratorBase::RecipeToString{
        REGISTER_RECIPE_TO_STRING(A),
        REGISTER_RECIPE_TO_STRING(B),
        REGISTER_RECIPE_TO_STRING(C),
    };
#undef REGISTER_RECIPE_TO_STRING

int passTypeToInt(std::string PassType, PhaseOrderGeneratorBase::PMap &PassMap,
                  int PreviousPassNum) {
  if ((PassMap[PassType] == "module" ||
       (PassType.find("function(") != std::string::npos &&
        PreviousPassNum != 1) ||
       PassType.find("cgscc(") != std::string::npos))
    return 0;
  if (PassMap[PassType] == "cgscc" ||
      PassType.find("function(") != std::string::npos)
    return 1;
  if (PassMap[PassType] == "function" ||
      PassType.find("loop(") != std::string::npos ||
      PassType.find("loop-mssa(") != std::string::npos)
    return 2;
  if (PassMap[PassType] == "loop")
    return 3;
  return -1;
}

std::string intToPassType(int Type) {
  if (Type == 0)
    return "module";
  if (Type == 1)
    return "cgscc";
  if (Type == 2)
    return "function";
  if (Type == 3)
    return "loop";
  llvm_unreachable("Not a valid pass type");
  return "";
}

std::string PhaseOrderGeneratorBase::recipesToPasses(
    PhaseOrderGeneratorBase::Recipes const &R, PMap &PassMap) {
  std::string Res;
  std::vector<std::string> Passes;
  int HighestScopeNum = 4;
  // Generate vector Passes of all passes provided in sequence
  for (auto Recipe : R) {
    std::string PassWithCommas =
        PhaseOrderGeneratorBase::RecipeToPassOrders[Recipe];
    std::stringstream SS(PassWithCommas.substr(0, PassWithCommas.length() - 1));
    // Create vector of passes
    while (SS.good()) {
      std::string SubStr;
      getline(SS, SubStr, ',');
      Passes.push_back(SubStr);
    }
  }
  // Find the highest scope to promote to
  for (std::string Pass : Passes) {
    std::string Str = Pass.substr(0, Pass.find_last_of("<"));
    int PassNum = passTypeToInt(Str, PassMap, 10);
    if (PassNum != -1)
      HighestScopeNum = std::min(HighestScopeNum, PassNum);
    else
      HighestScopeNum = 0;
  }
  // Loop through all scopes, starting from lowest, nesting outwards
  for (int Scope = 3; Scope > HighestScopeNum; Scope--) {
    std::vector<std::string> NewPasses;
    int PreviousPassNum = 10;
    for (std::string Pass : Passes) {
      std::string Str = Pass.substr(0, Pass.find_last_of("<"));
      int PassNum = passTypeToInt(Str, PassMap, PreviousPassNum);
      // If the current pass is of the same scope as current scope, promote it
      if (PassNum == Scope) {
        // If current pass is same scope as previous pass, combine them
        if (PassNum == PreviousPassNum) {
          std::string PreviousPass = NewPasses.back();
          NewPasses.pop_back();
          std::string Combined =
              PreviousPass.substr(0, PreviousPass.length() - 1);
          Combined = Combined + "," + Pass + ")";
          NewPasses.push_back(Combined);
        } else {
          std::string Scope = intToPassType(PassNum);
          NewPasses.push_back(Scope + "(" + Pass + ")");
        }
      } else
        NewPasses.push_back(Pass);
      PreviousPassNum = PassNum;
    }
    Passes = NewPasses;
  }
  // Combine vector into string
  for (auto Pass : Passes)
    Res += Pass + ",";
  return Res.substr(0, Res.find_last_of(","));
}

std::string PhaseOrderGeneratorBase::recipesToString(
    PhaseOrderGeneratorBase::Recipes const &R) {
  std::string Res;
  for (auto Recipe : R) {
    Res += PhaseOrderGeneratorBase::RecipeToString[Recipe];
  }
  return Res;
}

// Generate a random int from [Min, Max]
static int randInt(int Min, int Max) {
  std::random_device RD;
  std::mt19937 Gen(RD());
  std::uniform_int_distribution<> Distr(Min, Max);
  return Distr(Gen);
}

// Generate a sequence of random int from [Min, Max] with specified Length.
static std::vector<int> randomIntSeq(int Length, int Min, int Max) {
  std::vector<int> Ret;

  for (int i = 0; i < Length; ++i) {
    Ret.push_back(randInt(Min, Max));
  }

  return Ret;
}

static PhaseOrderGeneratorBase::Recipes convert(const std::vector<int> &in) {
  PhaseOrderGeneratorBase::Recipes Out;
  Out.reserve(in.size());

  std::transform(in.begin(), in.end(), std::back_inserter(Out), [](int n) {
    return static_cast<PhaseOrderGeneratorBase::Recipe>(n);
  });

  return Out;
}

PhaseOrderGeneratorBase::Recipes PhaseOrderGeneratorBase::generateRecipe() {
  // For now just generate a random recipe of length 2.
  std::vector<int> Res = randomIntSeq(
      2, 0, static_cast<int>(PhaseOrderGeneratorBase::Recipe::NumOfRecipe) - 1);

  return convert(Res);
}

PhaseOrderGeneratorBase::Recipes PhaseOrderGeneratorBase::generateRecipe(
    PhaseOrderGeneratorBase::Recipes const &R) {
  // For now just generate a random sequence.
  return generateRecipe();
}
