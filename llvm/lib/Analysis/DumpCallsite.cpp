//===- DumpCallsite.cpp - DumpCallsite implementation  --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ability to dump all callsites in a given function.
//
//===----------------------------------------------------------------------===//
#if defined(ENABLE_ACPO)
#include "llvm/Analysis/DumpCallsite.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool>
    IncludeDeclaration("include-declaration", cl::Hidden,
                       cl::desc("Also dump declaration in dump-callsite pass"));

namespace {

// Implementation of actual DumpCallsite
class DumpCallsite {
public:
  void run(Function &F);
};

// Wrapper for legacy PM
class DumpCallsiteLegacy : public FunctionPass {
public:
  static char ID;
  DumpCallsiteLegacy() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override;
};

void DumpCallsite::run(Function &F) {
  outs() << F.getName();
  // Get all callees from 'call' inst
  for (auto &I : instructions(F)) {
    // Is a call inst
    if (auto *CS = dyn_cast<CallBase>(&I)) {
      // callee is present
      if (Function *Callee = CS->getCalledFunction()) {
        // Not intrinsic
        if (!Callee->isIntrinsic()) {
          // decide whether to dump declaration
          if (!Callee->isDeclaration() || IncludeDeclaration) {
            outs() << " " << Callee->getName();
          }
        }
      }
    }
  }
  outs() << "\n";
}

bool DumpCallsiteLegacy::runOnFunction(Function &F) {
  DumpCallsite Impl;
  Impl.run(F);
  return false;
}

} // namespace

char DumpCallsiteLegacy::ID = 0;
INITIALIZE_PASS(DumpCallsiteLegacy, "dump-callsite", "Dump Callsite", false,
                false)

PreservedAnalyses DumpCallsitePass::run(Function &F,
                                        FunctionAnalysisManager &FAM) {
  DumpCallsite Impl;
  Impl.run(F);
  return PreservedAnalyses::all();
}
#endif
