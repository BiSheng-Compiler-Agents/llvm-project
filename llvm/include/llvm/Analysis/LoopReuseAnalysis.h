//===- llvm/Analysis/LoopReuseAnalysis.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the interface for the loop reuse analysis.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LOOPREUSEANALYSIS_H
#define LLVM_ANALYSIS_LOOPREUSEANALYSIS_H

#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
class ReuseDelinearizationInfo {
public:
  ReuseDelinearizationInfo();
  ReuseDelinearizationInfo(LoadInst *Ld, Loop *L, ScalarEvolution *SE);
  bool isSuccessful();

  const SCEV *BasePointer;
  const SCEV *AccessFn;
  SmallVector<const SCEV *, 3> Subscripts;
  SmallVector<const SCEV *, 3> Sizes;
};

class LoopReuseInfo {
public:
  LoopReuseInfo(Loop *L, ScalarEvolution *SE, LoopInfo *LI);
  int getOuterLoopReuse();
  int getLiveCount();

private:
  ReuseDelinearizationInfo getDelinearizationInfo(LoadInst *Ld);
  ReuseDelinearizationInfo findMatchingDelinearizableLoad(
      LoadInst *Ld, ReuseDelinearizationInfo *RDI,
      std::vector<LoadInst *> &Loads,
      std::function<bool(ReuseDelinearizationInfo, ReuseDelinearizationInfo,
                         ScalarEvolution *)>
          Predicate);

  int LiveCount = 0;
  int OuterLoopReuseCount = 0;

  DenseMap<LoadInst *, ReuseDelinearizationInfo> DelinearizationMap;

  Loop *L = nullptr;
  ScalarEvolution *SE = nullptr;
  LoopInfo *LI = nullptr;
}; // End class LoopReuseInfo

class LoopReuseResult {
public:
  LoopReuseResult(ScalarEvolution *SE, LoopInfo *LI);
  const LoopReuseInfo &getInfo(Loop *L);
  void releaseMemory() { LoopReuseInfoMap.clear(); }

private:
  DenseMap<Loop *, std::unique_ptr<LoopReuseInfo>> LoopReuseInfoMap;

  ScalarEvolution *SE;
  LoopInfo *LI;
}; // End class LoopReuseResult.

class LoopReuseAnalysis : public FunctionPass {
public:
  LoopReuseAnalysis();
  bool runOnFunction(Function &F) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  LoopReuseResult &getResult() const { return *LRR; }
  void releaseMemory() override { LRR->releaseMemory(); }
  void print(raw_ostream &OS, const Module *M = nullptr) const override;

  static char ID;

private:
  std::unique_ptr<LoopReuseResult> LRR;
}; // End class LoopReuseAnalysisPass

class LoopReuseAnalysisWrapper
    : public AnalysisInfoMixin<LoopReuseAnalysisWrapper> {
  friend AnalysisInfoMixin<LoopReuseAnalysisWrapper>;

  static AnalysisKey Key;

public:
  using Result = LoopReuseResult;

  LoopReuseResult run(Function &F, FunctionAnalysisManager &AM);
}; // End class LoopReuseAnalysisWrapper
} // namespace llvm

#endif // LLVM_ANALYSIS_LOOPREUSEANALYSIS_H
