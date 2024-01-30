//===- SimulatedAnnealing.cpp - The LLVM Modular Optimizer ----------------===//
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

#include "llvm/Analysis/SimulatedAnnealing.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <random>
#include <sys/wait.h>
#include <unistd.h>

#undef DEBUG_TYPE
#define DEBUG_TYPE "protean"

SimulatedAnnealingProtean::SimulatedAnnealingProtean(
    std::string CoolingSchedule, unsigned int MaxIterations)
    : Generator{new PhaseOrderGeneratorBase()} {
  this->CoolingSchedule = CoolingSchedule;
  this->MaxIterations = MaxIterations;
  this->MaxTemperature = 100.0;
  this->MinTemperature = 0.1;
}

// Generate a random int from [Min, Max]
static int randInt(int Min, int Max) {
  std::random_device RD;
  std::mt19937 Gen(RD());
  std::uniform_int_distribution<> Distr(Min, Max);
  return Distr(Gen);
}

void SimulatedAnnealingProtean::run() {
  SimulatedAnnealingProtean::State S = Generator->generateRecipe();
  for (int Iteration = 0; Iteration < this->MaxIterations + 1; ++Iteration) {
    double Temp = temperature(Iteration);
    LLVM_DEBUG(llvm::dbgs()
               << "Iteration " << Iteration << " Temperature:" << Temp << "\n");
    if (Temp <= 0.1) {
      break;
    }
    SimulatedAnnealingProtean::State SNew = Generator->generateRecipe(S);
    setCurState(SNew);

    // Fork a new child process and compile with the new recipe.
    // The parent process waits for the child proces to finish and then continue
    // the  Simulated Annealing main loop.
    pid_t Pid = fork();
    if (Pid != 0) {
      int Wstatus;
      waitpid(Pid, &Wstatus, 0);
      if (!WIFEXITED(Wstatus)) {
        LLVM_DEBUG(llvm::dbgs()
                   << "Recipe exited unexpected: "
                   << PhaseOrderGeneratorBase::RecipesToString(SNew) << "\n");
      } else {
        LLVM_DEBUG(llvm::dbgs()
                   << "Child exited with: " << WEXITSTATUS(Wstatus) << "\n\n");
      }
    } else {
      return;
    }

    // Accept or reject the new state.
    if (P(S, SNew, Temp) >= ((double)randInt(0, INT_MAX) / (double)INT_MAX)) {
      LLVM_DEBUG(llvm::dbgs() << "New state accepted\n");
      S = SNew;
    }
  }
  setFinalState(getCurState());
  setFinished(true);
}

SimulatedAnnealingProtean::State
SimulatedAnnealingProtean::neighbour(SimulatedAnnealingProtean::State S) {
  return Generator->generateRecipe(S);
}

double SimulatedAnnealingProtean::temperature(int Iteration) {
  // Generate new temperature based on cooling schedule
  if (this->CoolingSchedule == "geometric") {
    CoolingRate = pow(MinTemperature / MaxTemperature, 1.0 / (MaxIterations));
    return MaxTemperature * pow(CoolingRate, Iteration);
  } else if (this->CoolingSchedule == "linear") {
    CoolingRate = (MinTemperature + MaxTemperature) / MaxIterations;
    return this->MaxTemperature - this->CoolingRate * Iteration;
  }
  return 0.0;
}

double SimulatedAnnealingProtean::E(SimulatedAnnealingProtean::State S) {
  // perform IR analysis to determine cost of current state

  return randInt(0, 100);
}

double SimulatedAnnealingProtean::P(SimulatedAnnealingProtean::State S,
                                    SimulatedAnnealingProtean::State SNew,
                                    double Temperature) {
  double Diff = E(SNew) - E(S);
  // If new state is worse than old, use equation below to calculate probability
  // of accepting new state
  if (Diff >= 0)
    return exp(-1 * Diff / Temperature);
  // If new state is better than old, accept new state
  return 1.0;
}
