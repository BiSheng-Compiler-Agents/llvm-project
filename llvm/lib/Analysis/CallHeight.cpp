//===- CallHeight.cpp - CallHeight implementation  ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements getting the call height of functions in a module.
//
//===----------------------------------------------------------------------===//

#if defined(ENABLE_ACPO)
#include "llvm/Analysis/CallHeight.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "call-height"

// Adapted from MLInlineAdvisor.cpp
CallBase *getInlinableCallSite(Instruction &I) {
  if (auto *CS = dyn_cast<CallBase>(&I)) {
    if (Function *Callee = CS->getCalledFunction())
      if (!Callee->isDeclaration()) {
        return CS;
      }
  }
  return nullptr;
}

unsigned CallHeight::getLevel(Function &F) { return (*Levels)[&F]; }

CallHeight::CallHeight(Module &M)
    : Levels(std::make_unique<std::map<const Function *, unsigned>>()) {
  // Adapted from MLInlineAdvisor.cpp
  CallGraph CG = CallGraph(M);

  for (auto I = scc_begin(&CG); !I.isAtEnd(); ++I) {
    const std::vector<CallGraphNode *> &CGNodes = *I;
    unsigned Level = 0;
    for (auto *CGNode : CGNodes) {
      Function *F = CGNode->getFunction();
      if (!F || F->isDeclaration())
        continue;
      for (auto &I : instructions(F)) {
        if (auto *CS = getInlinableCallSite(I)) {
          auto *Called = CS->getCalledFunction();
          auto Pos = Levels->find(Called);
          // In bottom up traversal, an inlinable callee is either in the
          // same SCC, or to a function in a visited SCC. So not finding its
          // level means we haven't visited it yet, meaning it's in this SCC.
          if (Pos == Levels->end())
            continue;
          Level = std::max(Level, Pos->second + 1);
        }
      }
    }
    for (auto *CGNode : CGNodes) {
      Function *F = CGNode->getFunction();
      if (F && !F->isDeclaration())
        (*Levels)[F] = Level;
    }
  }
}

AnalysisKey CallHeightAnalysis::Key;

CallHeight CallHeightAnalysis::run(Module &M, ModuleAnalysisManager &MAM) {
  return CallHeight(M);
}

bool CallHeightAnalysisWrapper::runOnModule(Module &M) {
  Result.reset(new CallHeight(M));
  return false;
}

void CallHeightAnalysisWrapper::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
}

char CallHeightAnalysisWrapper::ID = 0;
INITIALIZE_PASS(CallHeightAnalysisWrapper, DEBUG_TYPE, "Call Height Analysis",
                false, true)

Pass *llvm::createCallHeightAnalysisWrapper() {
  return new CallHeightAnalysisWrapper();
}
#endif
