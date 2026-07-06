//===------------------- LoopReuseAnalysis.cpp ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// ===--------------------------------------------------------------------=== //

#include "llvm/Analysis/LoopReuseAnalysis.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/Delinearization.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "loop-reuse"

// Static helper functions.
// Iterate BBs inside the SubLoop to look for all load instructions, returning
// the std::vector with pointers to the loads that were found.
void generateListOfLoads(Loop *SubLoop, std::vector<LoadInst *> &ListOfLoads) {
  for (auto &BB : SubLoop->getBlocks()) {
    for (Instruction &I : *BB) {
      if (auto Ld = dyn_cast<LoadInst>(&I)) {
        ListOfLoads.push_back(Ld);
      }
    }
  }
}

static unsigned computeReuseLiveCount(Loop *SubLoop) {
  unsigned LiveCount = 0;
  unsigned MaxLiveCount = 0;
  unsigned NumOfPhiInst = 0;
  DenseMap<Instruction *, unsigned> UseMap;
  for (auto &BB : SubLoop->getBlocks()) {
    for (Instruction &I : *BB) {
      // Count number of PHI nodes for later.
      if (isa<PHINode>(&I)) {
        ++NumOfPhiInst;
      }
      // Add current instruction to the map if it has future uses.
      // Each use adds one to the value in the map.
      bool HasNonPhiUsers = false;
      for (auto U : I.users()) {
        if (auto UInst = dyn_cast<Instruction>(U)) {
          if (isa<PHINode>(UInst)) {
            continue;
          }
          auto UseIt = UseMap.find(&I);
          if (UseIt == UseMap.end()) {
            UseMap[&I] = 1;
          } else {
            UseMap[&I] += 1;
          }
          HasNonPhiUsers = true;
        }
      }
      if (HasNonPhiUsers) {
        LiveCount += 1;
        if (LiveCount > MaxLiveCount) {
          MaxLiveCount = LiveCount;
        }
      }
      // Check if current instruction is used by an instruction
      // that is already in the map. If so, decrement the number of uses.
      // PHI nodes are ignored and counted separately.
      if (I.operand_values().empty()) {
        continue;
      }
      for (auto U : I.operand_values()) {
        if (auto UInst = dyn_cast<Instruction>(U)) {
          auto UseIt = UseMap.find(UInst);
          if (UseIt != UseMap.end()) {
            UseMap[UInst] -= 1;
            if (UseMap[UInst] <= 0) {
              LiveCount -= 1;
            }
          }
        }
      }
    }
  }

  return MaxLiveCount + NumOfPhiInst;
}

// Return a pointer to the subloop in case the parent loop has exactly one
// subloop and both the parent loop and the subloop are in loop simplify form.
static Loop *checkLoopForAnalysis(const Loop *L) {
  if (!L->isLoopSimplifyForm() || L->getSubLoops().size() != 1) {
    LLVM_DEBUG(dbgs() << "LRA: Skipping loop not in simplify form or without a "
                         "single subloop.");
    return nullptr;
  }
  Loop *SubLoop = L->getSubLoops()[0];
  if (!SubLoop->isLoopSimplifyForm()) {
    LLVM_DEBUG(
        dbgs() << "LRA: Skipping loop with subloop not in loop simplify form.");
    return nullptr;
  }
  return SubLoop;
}

// ReuseDelinearizationInfo
ReuseDelinearizationInfo::ReuseDelinearizationInfo()
    : BasePointer(nullptr), AccessFn(nullptr) {}

ReuseDelinearizationInfo::ReuseDelinearizationInfo(LoadInst *Ld, Loop *L,
                                                   ScalarEvolution *SE) {
  if (Ld) {
    AccessFn = SE->getSCEVAtScope(Ld->getPointerOperand(), L);
    BasePointer = SE->getPointerBase(AccessFn);
    AccessFn = SE->getMinusSCEV(AccessFn, BasePointer);
    delinearize(*SE, AccessFn, Subscripts, Sizes, SE->getElementSize(Ld));
  } else {
    AccessFn = nullptr;
    BasePointer = nullptr;
  }
}

bool ReuseDelinearizationInfo::isSuccessful() {
  if (Subscripts.empty() || Sizes.empty() ||
      Subscripts.size() != Sizes.size()) {
    return false;
  }
  return true;
}

// LoopReuseResult
LoopReuseResult::LoopReuseResult(ScalarEvolution *SE, LoopInfo *LI)
    : SE(SE), LI(LI) {}

const LoopReuseInfo &LoopReuseResult::getInfo(Loop *L) {
  auto &LRI = LoopReuseInfoMap[L];
  if (!LRI) {
    LRI = std::make_unique<LoopReuseInfo>(L, SE, LI);
  }
  return *LRI.get();
}

// LoopReuseInfo
ReuseDelinearizationInfo LoopReuseInfo::getDelinearizationInfo(LoadInst *Ld) {
  return ReuseDelinearizationInfo(Ld, L, SE);
}

ReuseDelinearizationInfo LoopReuseInfo::findMatchingDelinearizableLoad(
    LoadInst *Ld, ReuseDelinearizationInfo *RDI, std::vector<LoadInst *> &Loads,
    std::function<bool(ReuseDelinearizationInfo, ReuseDelinearizationInfo,
                       ScalarEvolution *)>
        Predicate) {
  for (LoadInst *Other : Loads) {
    if (Other->isIdenticalTo(Ld)) {
      continue;
    }
    auto OtherRDI = this->getDelinearizationInfo(Other);
    if (!OtherRDI.isSuccessful()) {
      continue;
    }
    const SCEV *Diff = SE->getMinusSCEV(OtherRDI.BasePointer, RDI->BasePointer);
    if (auto ConstDiff = dyn_cast<SCEVConstant>(Diff)) {
      if (ConstDiff->getAPInt() != 0) {
        continue;
      }
      if (Predicate(OtherRDI, *RDI, SE)) {
        return OtherRDI;
      }
    }
  }
  return ReuseDelinearizationInfo(nullptr, nullptr, nullptr);
}

LoopReuseInfo::LoopReuseInfo(Loop *L, ScalarEvolution *SE, LoopInfo *LI)
    : L(L), SE(SE), LI(LI) {
  Loop *SubLoop = checkLoopForAnalysis(L);
  // If loops are simplify and single subloop on the parent, the analysis is
  // safe.
  if (SubLoop != nullptr) {
    // Create list of candidate loads:
    LLVM_DEBUG(dbgs() << "LRA: Starting Loop Reuse Analysis for loop: "
                      << L->getName() << "\n");
    // Compute live count for subloop.
    LiveCount = computeReuseLiveCount(SubLoop);
    // Generate list of candidate loads.
    std::vector<LoadInst *> ListOfLoads;
    generateListOfLoads(SubLoop, ListOfLoads);
    for (LoadInst *Ld : ListOfLoads) {
      // Only continue the analysis for the delinerealizable loads.
      auto RDI = this->getDelinearizationInfo(Ld);
      if (!RDI.isSuccessful()) {
        continue;
      }
      LLVM_DEBUG(dbgs() << "Found delinearizable load of base pointer ";
                 RDI.BasePointer->dump());
      // Find another load instruction that matches the target predicate.
      // WIP: Maybe return list of instructions in case there is more, since
      // that may be more valuable to reuse analysis decisions.
      auto Predicate = [=](ReuseDelinearizationInfo MatchedRDI,
                           ReuseDelinearizationInfo CurrentRDI,
                           ScalarEvolution *SE) {
        // Find out subscript of outer loop.
        int SubscriptIdx = -1;
        for (int Idx = MatchedRDI.Subscripts.size() - 1; Idx >= 0; --Idx) {
          if (auto AddRec =
                  dyn_cast<SCEVAddRecExpr>(MatchedRDI.Subscripts[Idx])) {
            if (AddRec->getLoop() == L) {
              SubscriptIdx = Idx;
              break;
            }
          }
        }
        // Outer loop is not taken into account for address calculation.
        if (SubscriptIdx < 0) {
          return false;
        }
        // Find out if matched instruction has difference of constant one in the
        // second subscript. This is especially useful for unroll and jam
        // transformation.
        const SCEV *Diff = SE->getMinusSCEV(MatchedRDI.Subscripts[1],
                                            CurrentRDI.Subscripts[1]);
        auto ConstDiff = dyn_cast<SCEVConstant>(Diff);
        if (ConstDiff && ConstDiff->getAPInt() == 1) {
          LLVM_DEBUG(
              dbgs()
              << "Found matching delinearizable load of the same"
              << " base pointer that matched predicate on second subscript and "
              << "constant value 1.\n");
          return true;
        }
        return false;
      };
      // If there is a predicated match, we can increment the reuse conter for
      // that predicate.
      ReuseDelinearizationInfo MatchRDI = this->findMatchingDelinearizableLoad(
          Ld, &RDI, ListOfLoads, Predicate);
      if (MatchRDI.isSuccessful()) {
        ++OuterLoopReuseCount;
      }
    }
  }
}

int LoopReuseInfo::getOuterLoopReuse() { return OuterLoopReuseCount; }

int LoopReuseInfo::getLiveCount() { return LiveCount; }


void LoopReuseAnalysis::print(raw_ostream &OS, const Module *M) const {
  OS.indent(2) << "Loop reuse analysis pass instance.\n";
}


void LoopReuseAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<ScalarEvolutionWrapperPass>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.setPreservesAll();
}

// LoopReuseAnalysisWrapper
AnalysisKey LoopReuseAnalysisWrapper::Key;

LoopReuseResult LoopReuseAnalysisWrapper::run(Function &F,
                                              FunctionAnalysisManager &AM) {
  ScalarEvolution &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
  LoopInfo &LI = AM.getResult<LoopAnalysis>(F);
  return LoopReuseResult(&SE, &LI);
}
