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
#include "llvm/Analysis/PhaseOrder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sys/wait.h>
#include <unistd.h>

#undef DEBUG_TYPE
#define DEBUG_TYPE "protean"

SimulatedAnnealingProtean::SimulatedAnnealingProtean(
    CoolingType CoolingSchedule, unsigned int MaxIterations,
    IRCostFunction CostType, std::string OutputFilename)
    : Generator{new PhaseOrderGeneratorBase()},
      CoolingSchedule(CoolingSchedule), CostType(CostType),
      OutputFilename(OutputFilename), MaxTemperature(100.0),
      MinTemperature(0.1), MaxIterations(MaxIterations) {}

// Generate a random int from [Min, Max]
static int randInt(int Min, int Max) {
  std::random_device RD;
  std::mt19937 Gen(RD());
  std::uniform_int_distribution<> Distr(Min, Max);
  return Distr(Gen);
}

void SimulatedAnnealingProtean::run() {
  SimulatedAnnealingProtean::State S = Generator->generateRecipe();
  SimulatedAnnealingProtean::State SNew = S;
  setCurState(SNew);
  for (int Iteration = 0; Iteration < this->MaxIterations + 1; ++Iteration) {
    double Temp = temperature(Iteration);
    LLVM_DEBUG(llvm::dbgs()
               << "Iteration " << Iteration << " Temperature:" << Temp << "\n");
    if (Temp <= 0.1) {
      break;
    }

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
                   << PhaseOrderGeneratorBase::recipesToString(SNew) << "\n");
      } else {
        LLVM_DEBUG(llvm::dbgs()
                   << "Child exited with: " << WEXITSTATUS(Wstatus) << "\n\n");
      }
    } else {
      return;
    }

    // Accept or reject the new state.
    double P = probabilityOfNewState(S, SNew, Temp);
    if (P == -1) {
      continue;
    }
    if (P >= ((double)randInt(0, INT_MAX) / (double)INT_MAX)) {
      LLVM_DEBUG(llvm::dbgs() << "New state accepted\n");
      S = SNew;
    }
    SNew = Generator->generateRecipe(S);
    setCurState(SNew);
  }

  setFinalState(S);
  setFinished(true);
}

SimulatedAnnealingProtean::State
SimulatedAnnealingProtean::neighbour(SimulatedAnnealingProtean::State &S) {
  return Generator->generateRecipe(S);
}

double SimulatedAnnealingProtean::temperature(int Iteration) {
  // Generate new temperature based on cooling schedule
  switch (CoolingSchedule) {
  case CoolingType::Geometric:
    CoolingRate = pow(MinTemperature / MaxTemperature, 1.0 / (MaxIterations));
    return MaxTemperature * pow(CoolingRate, Iteration);
  case CoolingType::Linear:
    CoolingRate = (MinTemperature + MaxTemperature) / MaxIterations;
    return this->MaxTemperature - this->CoolingRate * Iteration;
  default:
    llvm_unreachable("Not a valid cooling schedule");
  }
}

double
SimulatedAnnealingProtean::irAnalysisCost(SimulatedAnnealingProtean::State &S,
                                          std::string OutputFilename) {
  return 0.0;
}

double
SimulatedAnnealingProtean::fileSizeCost(SimulatedAnnealingProtean::State &S,
                                        std::string OutputFilename) {
  std::string RecipeStr = PhaseOrderGeneratorBase::recipesToString(S);
  uint64_t Size;
  std::error_code EC = llvm::sys::fs::file_size(OutputFilename, Size);
  if (EC) {
    llvm::errs() << EC.message() << '\n';
    return -1;
  }
  CostMap[RecipeStr] = Size;
  return Size;
}

double SimulatedAnnealingProtean::instructionCountCost(
    SimulatedAnnealingProtean::State &S, std::string OutputFilename) {
  std::string RecipeStr = PhaseOrderGeneratorBase::recipesToString(S);
  llvm::LLVMContext Context;
  llvm::SMDiagnostic Err;
  std::unique_ptr<llvm::Module> M =
      parseIRFile(OutputFilename, Err, Context, {});
  if (!M) {
    llvm::errs() << "Could not calculate cost, invalid IR\n";
    return -1;
  }
  int Instructions = 0;
  for (auto &F : *M) {
    for (auto &BB : F) {
      Instructions += BB.size();
    }
  }
  CostMap[RecipeStr] = Instructions;
  return Instructions;
}

double SimulatedAnnealingProtean::cost(SimulatedAnnealingProtean::State &S) {
  // perform IR analysis to determine cost of current state
  std::string RecipeStr = PhaseOrderGeneratorBase::recipesToString(S);
  // Check if cost has previously been calculated, if so return that value
  if (CostMap.find(RecipeStr) != CostMap.end()) {
    return CostMap[RecipeStr];
  }
  std::string NewOutputFilename = OutputFilename;
  NewOutputFilename.insert(NewOutputFilename.find_last_of("."),
                           "-" + PhaseOrderGeneratorBase::recipesToString(S));
  switch (CostType) {
  case IRCostFunction::IRAnalysis:
    return irAnalysisCost(S, NewOutputFilename);
  case IRCostFunction::InstCount:
    return instructionCountCost(S, NewOutputFilename);
  case IRCostFunction::FileSize:
    return fileSizeCost(S, NewOutputFilename);
  default:
    llvm_unreachable("Not a valid cost function");
  }
}

double SimulatedAnnealingProtean::probabilityOfNewState(
    SimulatedAnnealingProtean::State &S, SimulatedAnnealingProtean::State &SNew,
    double Temperature) {
  double CurrentCost = cost(S);
  double NewCost = cost(SNew);
  if ((CurrentCost == -1) || (NewCost == -1)) {
    return -1;
  }
  double Diff = NewCost - CurrentCost;
  LLVM_DEBUG(llvm::dbgs() << "Cost of new state: " << (int)cost(SNew) << "\n");
  LLVM_DEBUG(llvm::dbgs() << "Cost of current state: " << (int)cost(S) << "\n");
  // If new state is worse than old, use equation below to calculate probability
  // of accepting new state
  if (Diff >= 0)
    return exp(-1 * Diff / Temperature);
  // If new state is better than old, accept new state
  return 1.0;
}
