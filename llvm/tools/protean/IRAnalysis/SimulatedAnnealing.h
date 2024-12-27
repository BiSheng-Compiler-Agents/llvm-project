//===- SimulatedAnnealing.h - The LLVM Modular Optimizer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2024, Huawei Technologies Co., Ltd. All rights reserved.
//
//===----------------------------------------------------------------------===//
//
// Implements the simulated annealing algorithm for the Metamorphic Code
// Optimizer
//
//===----------------------------------------------------------------------===//

#ifndef SIMULATED_ANNEALING_H
#define SIMULATED_ANNEALING_H

#include "PhaseOrder.h"
#include <memory>
#include <random>
#include <unordered_map>
#include <mutex>

enum IRCostFunction { FileSize, InstCount, IRAnalysis, MCA };

enum CoolingType { Geometric, Linear };

// A base class for simulated annealing
template <class State> class SimulatedAnnealingBase {
private:
  bool Finished = false;
  State FinalState;
  State CurState;

public:
  // The run command should implement the main loop of Simulated Annealing:
  // Initialize a state S
  // For k in [0, ..., k_max)
  //   T <- temperature(1 - (k+1)/k_max)
  //   Pick a new state, s_new <- neighbour(s)
  //   If P(E(s), E(s_new), T) >= random(0,1)
  //      s <- s_new
  //  Output final state s
  //
  // Goal: Find argmin E(s)
  virtual void run() = 0;

  bool getFinished() const { return Finished; }

  void setFinished(bool F) { Finished = F; }

  State getFinalState() const { return FinalState; }
  void setFinalState(State &S) { FinalState = S; }
  State getCurState() const { return CurState; }
  void setCurState(State &S) { CurState = S; }

  // Given a time budget, return a temperature.
  virtual double temperature(int Iteration) = 0;

  // Given a state S, return a neighbour.
  virtual State neighbour(State &S) = 0;

  // Return the energy of S
  virtual double cost(const State &S) = 0;

  // Return the acceptance probability given the current state, new state,
  // and a temperature value.
  // Mathematically: Pr( SNew | S, Temperature)
  virtual double probabilityOfNewState(State &S, State &SNew,
                                       double Temperature) = 0;
};

class SimulatedAnnealingProtean
    : public SimulatedAnnealingBase<PhaseOrderGeneratorBase::Recipes> {
private:
  std::unique_ptr<PhaseOrderGeneratorBase> Generator;

public:
  int RngVal;
  CoolingType CoolingSchedule;
  IRCostFunction CostType;
  std::string OutputFilename;
  double MaxTemperature;
  double MinTemperature;
  double CoolingRate;
  unsigned int MaxIterations;
  bool ProteanOutputTable;
  bool UseProteanCollect;
  bool ModLevelIPC;
  bool UseAOTModel;
  unsigned int InitialSampleSize;
  double MutationRate;
  double CrossoverRate;
  unsigned int PopulationSize;
  CrossoverFunction CrossoverType;
  MutationFunction MutationType;
  std::unordered_map<std::string, double> CostMap;
  std::vector<double> CachedCosts;
  std::set<std::string> AllRecipesSet;
  std::vector<std::string> AllRecipes;
  std::default_random_engine RandomEngine;
  SimulatedAnnealingProtean(
      int RngVal, CoolingType CoolingSchedule, double MaxTemperature,
      double MinTemperature, unsigned int MaxIterations,
      IRCostFunction CostType, std::string OutputFileName,
      bool ProteanOutputTableGen, bool UseProteanCollectFeatures,
      bool ModLevelIPC, bool UseAOT, unsigned int SampleSize,
      double MutationRate, double CrossoverRate, unsigned int PopulationSize,
      CrossoverFunction CrossoverType, MutationFunction MutationType);

  using State = PhaseOrderGeneratorBase::Recipes;
  void run() override;

  // Identity function.
  double temperature(int Iteration) override;

  // Use the PhaseOrderGeneratorBase to generate a random recipe.
  State neighbour(State &S) override;

  // For now: Return a constant energy.
  double cost(const State &S) override;

  // For now: Return 1 (i.e. always accept the new state).
  double probabilityOfNewState(State &S, State &SNew,
                               double Temperature) override;

  // Calculates cost based on instruction count
  double instructionCountCost(const State &S, std::string OutputFilename);

  std::vector<std::pair<std::string, std::string>>
  ir2VecCollectFeatures(std::string OutputFilename);
  // Calculates cost using an IR Analyzer
  double irAnalysisCost(const State &S, std::string OutputFilename);

  // Calculates cost based on file size
  double fileSizeCost(const State &S, std::string OutputFilename);

  // Calculates cost based on stats from llvm-mca
  double mcaCost(const State &S, std::string OutputFilename);

  // Generates all permutations of a given string of recipes
  void generatePermutationsWithRepetitions(std::string &Recipes,
                                           std::string &Current,
                                           int MaxSeqLength);
};

#endif
