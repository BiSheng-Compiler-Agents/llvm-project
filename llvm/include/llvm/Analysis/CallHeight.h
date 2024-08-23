//===- CallHeight.h - Call height for function ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header file defines passes to get the call height of functions.
//
//===----------------------------------------------------------------------===//

#if defined(ENABLE_ACPO)
#ifndef LLVM_ANALYSIS_CALLHEIGHT
#define LLVM_ANALYSIS_CALLHEIGHT

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

#include <unordered_map>
#include <map>

namespace llvm {

class CallHeight {
private:
  /// Map from function to its level (callheight)
  std::unique_ptr<std::map<const Function *, unsigned>> Levels;

public:
  CallHeight(Module &M);

  // Change this to getHeight
  unsigned getLevel(Function &F);

  bool invalidate(Module &, const PreservedAnalyses &PA,
                  ModuleAnalysisManager::Invalidator &) {
    return false;
  }
};

/// This analysis computes the mapping from function to level (callheight)
/// for MLInliner
class CallHeightAnalysis : public AnalysisInfoMixin<CallHeightAnalysis> {
public:
  static AnalysisKey Key;
  using Result = CallHeight;

  Result run(Module &M, ModuleAnalysisManager &MAM);
};

/// Legacy wrapper pass to provide the CallHeightAnalysis object.
class CallHeightAnalysisWrapper : public ModulePass {
  std::unique_ptr<llvm::CallHeight> Result;

public:
  static char ID;

  CallHeightAnalysisWrapper() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;

  llvm::CallHeight &getResult() { return *Result; }
  const llvm::CallHeight &getResult() const { return *Result; }
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

Pass *createCallHeightAnalysisWrapper();

} // namespace llvm

#endif
#endif // ENABLE_ACPO
