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

#include "PhaseOrder.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#define REGISTER_RECIPE_TO_PASSES(RECIPE, ...)                                 \
  {PhaseOrderGeneratorBase::Recipe::RECIPE, #__VA_ARGS__}
// TODO: For some reason when trying to commit this file there will be
// formatting issues.
// TODO: Right now the __VA_ARGS__ has to be comma separated WITHOUT any spaces.
// Find a way to get rid of all the spaces.
// clang-format off
std::unordered_map<PhaseOrderGeneratorBase::Recipe, std::string>
    PhaseOrderGeneratorBase::RecipeToPassOrders {
      REGISTER_RECIPE_TO_PASSES(A, globalopt,cgscc(devirt<4>(inline<only-mandatory>,inline,move-auto-init,function-attrs<skip-non-recursive-function-attrs>,argpromotion,function<eager-inv;no-rerun>(sroa<modify-cfg>,speculative-execution,tailcallelim,loop-mssa(licm<allowspeculation>,simple-loop-unswitch<nontrivial;trivial>),loop(loop-idiom,indvars,loop-deletion),loop-unroll<O3>,early-cse<>,callsite-splitting,sroa<modify-cfg>,early-cse<memssa>,speculative-execution,jump-threading,correlated-propagation,lower-expect,simplifycfg<bonus-inst-threshold=1;no-forward-switch-cond;no-switch-range-to-icmp;no-switch-to-lookup;keep-loops;no-hoist-common-insts;no-sink-common-insts;speculate-blocks;simplify-cond-branch>,instcombine<max-iterations=1;no-use-loop-info;no-verify-fixpoint>,aggressive-instcombine,tailcallelim,simplifycfg<bonus-inst-threshold=1;no-forward-switch-cond;no-switch-range-to-icmp;no-switch-to-lookup;keep-loops;no-hoist-common-insts;no-sink-common-insts;speculate-blocks;simplify-cond-branch>,reassociate))),),
      REGISTER_RECIPE_TO_PASSES(B, function<eager-inv>(loop-simplify,lcssa,crypto,chr,loop(loop-rotate<no-header-duplication;no-prepare-for-lto>,loop-deletion),annotation-remarks,constraint-elimination,mem2reg,instcombine<max-iterations=1;no-use-loop-info;no-verify-fixpoint>,loop-simplify,lcssa,indvars,loop-deletion,loop-simplify,lcssa,loop-instsimplify,loop-simplifycfg,function(loop-mssa(licm<allowspeculation>)),simple-loop-unswitch,simplifycfg<bonus-inst-threshold=1;no-forward-switch-cond;no-switch-range-to-icmp;no-switch-to-lookup;keep-loops;no-hoist-common-insts;no-sink-common-insts;speculate-blocks;simplify-cond-branch>,instcombine<max-iterations=1;no-use-loop-info;no-verify-fixpoint>),require<globals-aa>,function(invalidate<aa>),require<profile-summary>,function<eager-inv>(loop-simplify,lcssa,loop(loop-idiom,loop-deletion,loop-unroll-full),loop-data-prefetch,hash-data-prefetch,separate-const-offset-from-gep),),
      REGISTER_RECIPE_TO_PASSES(C, function<eager-inv>(sroa<modify-cfg>,gvn-hoist,mldst-motion,gvn,sccp,bdce,instcombine<max-iterations=1;no-use-loop-info;no-verify-fixpoint>,jump-threading,correlated-propagation,adce,memcpyopt),),
      REGISTER_RECIPE_TO_PASSES(D, cgscc(dse,function<eager-inv>(loop-simplify,lcssa,coro-elide,simplifycfg<bonus-inst-threshold=1;no-forward-switch-cond;no-switch-range-to-icmp;no-switch-to-lookup;keep-loops;no-hoist-common-insts;no-sink-common-insts;speculate-blocks;simplify-cond-branch>,instcombine<max-iterations=1;no-use-loop-info;no-verify-fixpoint>,reassociate),function-attrs,function(require<should-not-run-function-passes>),coro-split,function(invalidate<all>)),deadargelim,coro-cleanup,globalopt,globaldce,elim-avail-extern,rpo-function-attrs,recompute-globalsaa,ipsccp,function<eager-inv>(float2int,lower-constant-intrinsics),constmerge,cg-profile,rel-lookup-table-converter,ir-library-injection,),
      REGISTER_RECIPE_TO_PASSES(E, function<eager-inv>(loop-simplify,lcssa,loop(loop-rotate<no-header-duplication;no-prepare-for-lto>,loop-deletion),loop-distribute,loop-simplify,lcssa,loop-unroll-and-jam,inject-tli-mappings,loop-vectorize<no-interleave-forced-only;vectorize-forced-only;>,infer-alignment,loop-load-elim,instcombine<max-iterations=1;no-use-loop-info;no-verify-fixpoint>,simplifycfg<bonus-inst-threshold=1;no-forward-switch-cond;no-switch-range-to-icmp;no-switch-to-lookup;keep-loops;no-hoist-common-insts;no-sink-common-insts;speculate-blocks;simplify-cond-branch>,vector-combine,instcombine<max-iterations=1;no-use-loop-info;no-verify-fixpoint>,loop-unroll<O3>,transform-warning,sroa<preserve-cfg>,instcombine<max-iterations=1;no-use-loop-info;no-verify-fixpoint>,loop-simplify,lcssa,loop-mssa(licm<allowspeculation>),alignment-from-assumptions,loop-sink,instsimplify,div-rem-pairs,tailcallelim,simplifycfg<bonus-inst-threshold=1;no-forward-switch-cond;no-switch-range-to-icmp;no-switch-to-lookup;keep-loops;no-hoist-common-insts;no-sink-common-insts;speculate-blocks;simplify-cond-branch>,annotation-remarks),),
};
#undef REGISTER_RECIPE_TO_PASSES
// clang-format on

#define REGISTER_RECIPE_TO_STRING(RECIPE)                                      \
  {PhaseOrderGeneratorBase::Recipe::RECIPE, #RECIPE}
std::unordered_map<PhaseOrderGeneratorBase::Recipe, std::string>
    PhaseOrderGeneratorBase::RecipeToString{
        REGISTER_RECIPE_TO_STRING(A), REGISTER_RECIPE_TO_STRING(B),
        REGISTER_RECIPE_TO_STRING(C), REGISTER_RECIPE_TO_STRING(D),
        REGISTER_RECIPE_TO_STRING(E),

    };
#undef REGISTER_RECIPE_TO_STRING

PhaseOrderGeneratorBase::PhaseOrderGeneratorBase(
    int InitialSampleSize, unsigned int PopulationSize, double MutationRate,
    double CrossoverRate, CrossoverFunction CrossoverType,
    MutationFunction MutationType)
    : InitialSampleSize(InitialSampleSize), PopulationSize(PopulationSize),
      MutationRate(MutationRate), CrossoverRate(CrossoverRate),
      CrossoverType(CrossoverType), MutationType(MutationType) {}

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
const int RngVal = 123;
std::mt19937 Gen(RngVal);
static int randInt(int Min, int Max) {
  std::uniform_int_distribution<> Distr(Min, Max);
  return Distr(Gen);
}

// Generate a sequence of random int from [Min, Max] with specified Length.
static std::vector<int> randomIntSeq(int Length, int Min, int Max) {
  std::vector<int> Ret;

  for (int I = 0; I < Length; ++I) {
    Ret.push_back(randInt(Min, Max));
  }

  return Ret;
}

static PhaseOrderGeneratorBase::Recipes convert(const std::vector<int> &In) {
  PhaseOrderGeneratorBase::Recipes Out;
  Out.reserve(In.size());

  std::transform(In.begin(), In.end(), std::back_inserter(Out), [](int N) {
    return static_cast<PhaseOrderGeneratorBase::Recipe>(N);
  });

  return Out;
}

PhaseOrderGeneratorBase::Recipes PhaseOrderGeneratorBase::generateRecipe() {
  // Generate random sequence
  int Length = randInt(1, 5);
  std::vector<int> Res = randomIntSeq(
      Length, 0,
      static_cast<int>(PhaseOrderGeneratorBase::Recipe::NumOfRecipe) - 1);

  return convert(Res);
}

PhaseOrderGeneratorBase::Recipes PhaseOrderGeneratorBase::generateRecipe(
    PhaseOrderGeneratorBase::Recipes const &R) {
  // For now just generate a random sequence.
  return generateRecipe();
}

bool chanceOutcome(int Probability) { return randInt(1, 100) <= Probability; }

void PhaseOrderGeneratorBase::updateBestRecipes(std::string Recipe,
                                                double Cost) {
  BestRecipes.insert(std::make_pair(Cost, Recipe));
  if (BestRecipes.size() > PopulationSize) {
    BestRecipes.erase(*BestRecipes.begin());
  }
}

std::string PhaseOrderGeneratorBase::getRandomBestRecipe() {
  int Len = BestRecipes.size();
  int Choice = randInt(1, Len);
  for (auto &Recipe : BestRecipes) {
    --Choice;
    if (Choice == 0) {
      return Recipe.second;
    }
  }
  return "";
}

std::string PhaseOrderGeneratorBase::crossoverUniform(std::string Recipe1,
                                                      std::string Recipe2) {
  if (chanceOutcome(50)) {
    swap(Recipe1, Recipe2);
  }
  int Len = Recipe1.size();
  std::string Offspring;
  for (int I = 0; I < Len; ++I) {
    if (I >= Recipe2.size()) {
      Offspring.push_back(Recipe1[I]);
    } else if (chanceOutcome(50)) {
      Offspring.push_back(Recipe1[I]);
    } else {
      Offspring.push_back(Recipe2[I]);
    }
  }
  return Offspring;
}

std::string PhaseOrderGeneratorBase::crossoverOnePoint(std::string Recipe1,
                                                       std::string Recipe2) {
  if (chanceOutcome(50)) {
    swap(Recipe1, Recipe2);
  }
  int Len = Recipe1.size();
  std::string Offspring;
  int Cutoff = randInt(0, Len - 1);
  for (int I = 0; I < Len; I++) {
    if (I >= Recipe2.size()) {
      Offspring.push_back(Recipe1[I]);
    } else if (I < Cutoff) {
      Offspring.push_back(Recipe1[I]);
    } else {
      Offspring.push_back(Recipe2[I]);
    }
  }
  return Offspring;
}

std::string PhaseOrderGeneratorBase::crossoverTwoPoint(std::string Recipe1,
                                                       std::string Recipe2) {
  if (chanceOutcome(50)) {
    swap(Recipe1, Recipe2);
  }
  int Len = Recipe1.size();
  std::string Offspring;
  int Left = randInt(0, Len - 1);
  int Right = randInt(0, Len - 1);
  if (Left > Right) {
    std::swap(Left, Right);
  }
  for (int I = 0; I < Len; I++) {
    if (I >= Recipe2.size()) {
      Offspring.push_back(Recipe1[I]);
    } else if (Left <= I && I <= Right) {
      Offspring.push_back(Recipe2[I]);
    } else {
      Offspring.push_back(Recipe1[I]);
    }
  }
  return Offspring;
}

std::string PhaseOrderGeneratorBase::mutateLength(std::string Recipe) {
  std::vector<int> NewChance = {0, 100, 95, 90, 85, 0};
  std::vector<int> LessChance = {0, 0, 5, 10, 15, 20};
  if (chanceOutcome(NewChance[Recipe.size()])) {
    Recipe.push_back('A' + randInt(0, 4));
  }
  if (chanceOutcome(LessChance[Recipe.size()])) {
    Recipe.pop_back();
  }
  return Recipe;
}

std::string PhaseOrderGeneratorBase::mutateSwap(std::string Recipe,
                                                int MutateChance) {
  if (!chanceOutcome(MutateChance)) {
    return Recipe;
  }
  Recipe = mutateLength(Recipe);
  int Left = randInt(0, Recipe.size() - 1);
  int Right = 0;
  do {
    Right = randInt(0, Recipe.size() - 1);
  } while (Left != Right);
  std::swap(Recipe[Left], Recipe[Right]);
  return Recipe;
}

std::string PhaseOrderGeneratorBase::mutateFlip(std::string Recipe,
                                                int MutateChance) {
  if (!chanceOutcome(MutateChance)) {
    return Recipe;
  }

  Recipe = mutateLength(Recipe);

  int Idx = randInt(0, Recipe.size() - 1);
  char Last = Recipe[Idx];
  do {
    Recipe[Idx] = 'A' + randInt(0, 4);
  } while (Recipe[Idx] == Last);

  return Recipe;
}

PhaseOrderGeneratorBase::Recipes PhaseOrderGeneratorBase::generateRecipeGenetic(
    std::vector<std::string> &AllRecipes, std::string Recipe, int Iteration,
    double Temperature) {
  if (Iteration <= InitialSampleSize) {
    return generateRecipe(AllRecipes, Iteration);
  }
  std::string GoodRecipe = getRandomBestRecipe();
  std::string CrossedOver = Recipe;

  int MutateChance = MutationRate * 100;
  if (chanceOutcome(CrossoverRate * 100)) {
    if (CrossoverType == CrossoverFunction::Uniform) {
      CrossedOver = crossoverUniform(Recipe, GoodRecipe);
    } else if (CrossoverType == CrossoverFunction::SinglePoint) {
      CrossedOver = crossoverOnePoint(Recipe, GoodRecipe);
    } else {
      CrossedOver = crossoverTwoPoint(Recipe, GoodRecipe);
    }
  }
  if (CrossedOver == Recipe) {
    MutateChance = 100;
  }
  std::string FinalRecipe = CrossedOver;
  if (MutationType == MutationFunction::FlipOne) {
    FinalRecipe = mutateFlip(CrossedOver, MutateChance);
  } else {
    FinalRecipe = mutateSwap(CrossedOver, MutateChance);
  }
  std::vector<int> Rs;
  for (auto I : FinalRecipe) {
    Rs.push_back(I - 'A');
  }
  return convert(Rs);
}

PhaseOrderGeneratorBase::Recipes
PhaseOrderGeneratorBase::generateRecipe(std::vector<std::string> &AllRecipes,
                                        int Iteration) {
  int Sz = AllRecipes.size();
  Iteration = std::min(Sz - 1, Iteration);
  std::string Recipe = AllRecipes[Iteration];
  std::vector<int> Rs;
  for (auto I : Recipe) {
    Rs.push_back(std::stoi(std::string(1, I)));
  }
  return convert(Rs);
}

PhaseOrderGeneratorBase::Recipes
PhaseOrderGeneratorBase::generateRecipe(std::string Recipe) {
  if (Recipe.length() != 5) {
    llvm::errs() << "Recipe length incorrect\n";
    return {};
  }
  std::vector<int> Rs;
  for (auto I : Recipe) {
    if (I != '0' && I != '1' && I != '2' && I != '3' && I != '4') {
      llvm::errs() << "Invalid recipe character: " << I << "\n";
      return {};
    }
    // Convert character to integer
    Rs.push_back(std::stoi(std::string(1, I)));
  }
  return convert(Rs);
}
