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

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// This class should handle generating a phase ordering recipe.
// This class can be used as part of SimluatedAnnealingBase::neighbour().
class PhaseOrderGeneratorBase {
public:
  enum class Recipe { A, B, C, D, E, NumOfRecipe };
  // Define a recipe type
  using Recipes = std::vector<Recipe>;
  using PMap = std::unordered_map<std::string, std::string>;
  // Generate a random recipe.
  virtual Recipes generateRecipe();

  // Given a recipe R generate another recipe.
  virtual Recipes generateRecipe(Recipes const &R);

  static std::string recipesToPasses(Recipes const &R, PMap &PassMap);
  static std::string recipesToString(Recipes const &R);
  Recipes generateRecipe(std::vector<std::string> &AllRecipes, int Iteration);

private:
  static std::unordered_map<Recipe, std::string> RecipeToPassOrders;
  static std::unordered_map<Recipe, std::string> RecipeToString;
};

#endif
