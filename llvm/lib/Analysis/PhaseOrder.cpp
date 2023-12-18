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
#include <algorithm>
#include <random>

#define REGISTER_RECIPE_TO_PASSES(RECIPE, ...)                                 \
  { PhaseOrderGeneratorBase::Recipe::RECIPE, #__VA_ARGS__ }
// TODO: For some reason when trying to commit this file there will be
// formatting issues.
// TODO: Rightnow the __VA_ARGS__ has to be comma separated WITHOUT any spaces.
// Find a way to get rid of all the spaces.
std::map<PhaseOrderGeneratorBase::Recipe, std::string>
    PhaseOrderGeneratorBase::RecipeToPassOrders{
      REGISTER_RECIPE_TO_PASSES(A, loop-simplify,loop-unroll<O3;partial>,),
      REGISTER_RECIPE_TO_PASSES(B, inline,),
      REGISTER_RECIPE_TO_PASSES(C, argpromotion,),
    };
#undef REGISTER_RECIPE_TO_PASSES

#define REGISTER_RECIPE_TO_STRING(RECIPE)                                      \
  { PhaseOrderGeneratorBase::Recipe::RECIPE, #RECIPE }
std::map<PhaseOrderGeneratorBase::Recipe, std::string>
    PhaseOrderGeneratorBase::RecipeToString{
        REGISTER_RECIPE_TO_STRING(A),
        REGISTER_RECIPE_TO_STRING(B),
        REGISTER_RECIPE_TO_STRING(C),
    };
#undef REGISTER_RECIPE_TO_STRING

std::string PhaseOrderGeneratorBase::RecipesToPasses(
    PhaseOrderGeneratorBase::Recipes R) {
  std::string Res;
  for (auto r : R) {
    Res += PhaseOrderGeneratorBase::RecipeToPassOrders[r];
  }
  // Get rid of last comma
  size_t LastComma = Res.find_last_of(",");
  return Res.substr(0, LastComma);
}

std::string PhaseOrderGeneratorBase::RecipesToString(
    PhaseOrderGeneratorBase::Recipes R) {
  std::string Res;
  for (auto r : R) {
    Res += PhaseOrderGeneratorBase::RecipeToString[r];
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

PhaseOrderGeneratorBase::Recipes
PhaseOrderGeneratorBase::generateRecipe(PhaseOrderGeneratorBase::Recipes R) {
  // For now just generate a random sequence.
  return generateRecipe();
}
