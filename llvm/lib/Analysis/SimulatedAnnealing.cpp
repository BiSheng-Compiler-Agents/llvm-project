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
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <climits>
#include <random>
#include <sys/wait.h>
#include <unistd.h>

SimulatedAnnealingProtean::SimulatedAnnealingProtean()
    : Generator{new PhaseOrderGeneratorBase()} {}

// Generate a random int from [Min, Max]
static int randInt(int Min, int Max) {
  std::random_device RD;
  std::mt19937 Gen(RD());
  std::uniform_int_distribution<> Distr(Min, Max);
  return Distr(Gen);
}

void SimulatedAnnealingProtean::run(int Max) {
  SimulatedAnnealingProtean::State S = Generator->generateRecipe();

  for (int it = 0; it < Max; ++it) {
    double Temp = temperature(1 - (it + 1) / Max);
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
        llvm::outs() << "Recipe exited unexpected: "
                     << PhaseOrderGeneratorBase::RecipesToString(SNew) << "\n";
      } else {
        llvm::outs() << "Child exited with: " << WEXITSTATUS(Wstatus) << "\n";
      }
    } else {
      return;
    }

    // Accept or reject the new state.
    if (P(S, SNew, Temp) >= ((double)randInt(0, INT_MAX) / (double)INT_MAX)) {
      S = SNew;
    }
  }
  setFinalState(S);
  setFinished(true);
}

SimulatedAnnealingProtean::State
SimulatedAnnealingProtean::neighbour(SimulatedAnnealingProtean::State S) {
  return Generator->generateRecipe(S);
}

double SimulatedAnnealingProtean::temperature(double Budget) { return Budget; }

double SimulatedAnnealingProtean::E(SimulatedAnnealingProtean::State S) {
  return 0;
}

double SimulatedAnnealingProtean::P(SimulatedAnnealingProtean::State S,
                                    SimulatedAnnealingProtean::State SNew,
                                    double Temperature) {
  return 1;
}
