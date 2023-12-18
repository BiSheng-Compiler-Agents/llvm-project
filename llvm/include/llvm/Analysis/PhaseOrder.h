//===- PhaseOrder.h - The LLVM Modular Optimizer --------------------------===//
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

#ifndef PHASE_ORDER_H
#define PHASE_ORDER_H

#include <map>
#include <string>
#include <vector>

// This class should handle generating a phase ordering recipe.
// This class can be used as part of SimluatedAnnealingBase::neighbour().
class PhaseOrderGeneratorBase {
public:
  enum class Recipe { A, B, C, NumOfRecipe };

  // Define a recipe type
  using Recipes = std::vector<Recipe>;

  // Generate a random recipe.
  virtual Recipes generateRecipe();

  // Given a recipe R generate another recipe.
  virtual Recipes generateRecipe(Recipes R);

  static std::string RecipesToPasses(Recipes);
  static std::string RecipesToString(Recipes);

private:
  static std::map<Recipe, std::string> RecipeToPassOrders;
  static std::map<Recipe, std::string> RecipeToString;
};

#endif
