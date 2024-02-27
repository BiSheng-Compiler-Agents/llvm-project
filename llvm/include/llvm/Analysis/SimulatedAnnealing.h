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

#include "llvm/Analysis/PhaseOrder.h"
#include <memory>

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
  virtual double cost(State &S) = 0;

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
  std::string CoolingSchedule;
  double MaxTemperature;
  double MinTemperature;
  double CoolingRate;
  unsigned int MaxIterations;
  SimulatedAnnealingProtean(std::string CoolingSchedule,
                            unsigned int MaxIterations);

  using State = PhaseOrderGeneratorBase::Recipes;
  void run() override;

  // Identity function.
  double temperature(int Iteration) override;

  // Use the PhaseOrderGeneratorBase to generate a random recipe.
  State neighbour(State &S) override;

  // For now: Return a constant energy.
  double cost(State &S) override;

  // For now: Return 1 (i.e. always accept the new state).
  double probabilityOfNewState(State &S, State &SNew,
                               double Temperature) override;
};

#endif
