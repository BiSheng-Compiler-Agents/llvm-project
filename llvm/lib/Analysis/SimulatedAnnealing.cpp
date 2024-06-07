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
#include "llvm/Analysis/IR2Score.h"
#include "llvm/Analysis/ModelDataCollector.h"
#include "llvm/Analysis/PhaseOrder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <llvm/IR/LLVMContext.h>
#include <memory>
#include <random>
#include <sys/wait.h>
#include <unistd.h>

#undef DEBUG_TYPE
#define DEBUG_TYPE "protean"
namespace llvm {
class ModelDataScoreCollector : public ModelDataCollector {
public:
  ModelDataScoreCollector(formatted_raw_ostream &OS, std::string OutputFileName)
      : ModelDataCollector(OS, OutputFileName) {}

  void collectFeatures(std::unique_ptr<Module> &M) {
    std::vector<std::string> keys;
    keys.push_back("proteanFeatureHeaders");
    keys.push_back("proteanFeatureValues");
    std::vector<std::string> headers;
    std::vector<std::string> values;
    for (auto key : keys) {
      // Get the module flag
      llvm::Metadata *flag = M->getModuleFlag(key);
      // Check if the module flag exists and is an array
      if (llvm::MDNode *arrayNode =
              llvm::dyn_cast_or_null<llvm::MDNode>(flag)) {
        // Iterate over array elements
        for (unsigned i = 0, e = arrayNode->getNumOperands(); i < e; ++i) {
          llvm::Metadata *element = arrayNode->getOperand(i);
          if (llvm::MDString *mdString =
                  llvm::dyn_cast<llvm::MDString>(element)) {
            llvm::StringRef stringValue = mdString->getString();
            if (key == "proteanFeatureHeaders") {
              headers.push_back(stringValue.str());
            } else {
              values.push_back(stringValue.str());
            }
          }
        }
      } else {
        llvm::outs() << "Module flag with key '" << key
                     << "' not found or not an array.\n";
      }
    }
    for (int i = 0; i < headers.size(); i++) {
      Features.insert(Features.end(), {std::make_pair(headers[i], values[i])});
    }
  }
};
} // namespace llvm
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
  std::string RecipeStr = PhaseOrderGeneratorBase::recipesToString(S);
  llvm::LLVMContext Context;
  llvm::SMDiagnostic Err;
  std::unique_ptr<llvm::Module> M =
      llvm::parseIRFile(OutputFilename, Err, Context);
  if (!M) {
    Err.print("IR parsing error", llvm::errs());
    return 1;
  }

  std::error_code EC;
  std::string SAModelFile = "Simulated-annealing-model";
  llvm::raw_fd_ostream RawOS(SAModelFile, EC, llvm::sys::fs::CD_OpenAlways,
                             llvm::sys::fs::FA_Write, llvm::sys::fs::OF_Append);
  llvm::formatted_raw_ostream OS(RawOS);
  llvm::ModelDataScoreCollector MDC(OS, SAModelFile);
  MDC.collectFeatures(M);
  std::vector<std::pair<std::string, std::string>> Features = MDC.getFeatures();
  std::unique_ptr<llvm::IR2ScoreModel> IRModel =
      std::make_unique<llvm::IR2ScoreModel>(&Context);
  // std::vector<std::string> LUFeatures{"PartialOptSizeThreshold",
  //                                     "AllowRemainder",
  //                                     "UnrollRemainder",
  //                                     "AllowExpensiveTripCount",
  //                                     "Force",
  //                                     "TripCount",
  //                                     "MaxTripCount",
  //                                     "Size",
  //                                     "InitialIVValueInt",
  //                                     "FinalIVValueInt",
  //                                     "StepValueInt",
  //                                     "NumPartitions",
  //                                     "IndVarSetSize",
  //                                     "AvgStoreSetSize",
  //                                     "AvgNumInsts",
  //                                     "NumLoadInstPerLoopNest",
  //                                     "NumStoreInstPerLoopNest",
  //                                     "TotLoopNestInstCount",
  //                                     "AvgNumLoadInstPerLoopNest",
  //                                     "AvgNumStoreInstPerLoopNest",
  //                                     "NumLoadInstPerLoop",
  //                                     "NumStoreInstPerLoop",
  //                                     "TotLoopInstCount",
  //                                     "AvgNumLoadInstPerLoop",
  //                                     "AvgNumStoreInstPerLoop",
  //                                     "IsInnerMostLoop",
  //                                     "IsOuterMostLoop",
  //                                     "MaxLoopHeight",
  //                                     "TotBlocksPerLoop",
  //                                     "IsFixedTripCount"};
  // std::vector<std::pair<std::string, std::string>> LUFeaturePairs;

  // for (auto feature : LUFeatures) {
  //   LUFeaturePairs.push_back(std::make_pair(feature, "1.0"));
  // }
  IRModel->setMLCustomFeatures(Features);
  std::unique_ptr<llvm::ACPOAdvice> Score = IRModel->getAdvice();
  llvm::Constant *Val = Score->getField("IRSCORE");
  assert(Val != nullptr);
  assert(llvm::isa<llvm::ConstantInt>(Val));
  llvm::ConstantInt *ACPOInline = llvm::dyn_cast<llvm::ConstantInt>(Val);
  int Cost = ACPOInline->getSExtValue();
  CostMap[RecipeStr] = Cost;
  return Cost;
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
