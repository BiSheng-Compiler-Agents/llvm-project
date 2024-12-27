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

enum MutationFunction { FlipOne, SwapTwo };
enum CrossoverFunction { SinglePoint, DoublePoint, Uniform };

class PhaseOrderGeneratorBase {
public:
  PhaseOrderGeneratorBase(int InitialSampleSize, unsigned int PopulationSize,
                          double MutationRate, double CrossoverRate,
                          CrossoverFunction CrossoverType,
                          MutationFunction MutationType);
  enum class Recipe { A, B, C, D, E, NumOfRecipe };
  // Define a recipe type
  using Recipes = std::vector<Recipe>;
  using PMap = std::unordered_map<std::string, std::string>;
  // Generate a random recipe.
  virtual Recipes generateRecipe();

  // Given a recipe R generate another recipe.
  virtual Recipes generateRecipe(Recipes const &R);

  std::set<std::pair<double, std::string>> BestRecipes;
  void updateBestRecipes(std::string Recipe, double Cost);
  std::string getRandomBestRecipe();

  static std::string recipesToPasses(Recipes const &R, PMap &PassMap);
  static std::string recipesToString(Recipes const &R);
  std::string crossoverUniform(std::string Recipe1, std::string Recipe2);
  std::string crossoverOnePoint(std::string Recipe1, std::string Recipe2);
  std::string crossoverTwoPoint(std::string Recipe1, std::string Recipe2);
  std::string mutateLength(std::string Recipe);
  std::string mutateFlip(std::string Recipe, int MutateChance);
  std::string mutateSwap(std::string Recipe, int MutateChance);
  Recipes generateRecipeGenetic(std::vector<std::string> &AllRecipes,
                                std::string Recipe, int Iteration,
                                double Temperature);
  Recipes generateRecipe(std::vector<std::string> &AllRecipes, int Iteration);
  Recipes generateRecipe(std::string Recipe);

private:
  static std::unordered_map<Recipe, std::string> RecipeToPassOrders;
  static std::unordered_map<Recipe, std::string> RecipeToString;
  int InitialSampleSize;
  unsigned int PopulationSize;
  double MutationRate;
  double CrossoverRate;
  CrossoverFunction CrossoverType;
  MutationFunction MutationType;
};

#endif
