#if defined(ENABLE_AUTOTUNER)
//===---------------- AutoTuningCompile.h - Auto-Tuning -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// Copyright (C) 2017-2022, Huawei Technologies Co., Ltd. All rights reserved.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file declares the interface for AutoTuning Incremental Compilation.
/// Incremental compilation requires two passes 1) Module Pass and 2) Function
/// Pass for legacy pass manager. It requires an additional Loop Pass for new
/// pass manager.
/// AutoTuningOptPassGate class is also defined here which is used to enable/
/// disable the execution of optimization passes for the compilation pipeline.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_AUTOTUNER_AUTOTUNING_COMPILE_H_
#define LLVM_AUTOTUNER_AUTOTUNING_COMPILE_H_

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/IR/OptBisect.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"

namespace llvm {

class Pass;

//  Skips or runs optimization passes.
class AutoTuningOptPassGate : public OptPassGate {
public:
  explicit AutoTuningOptPassGate(bool Skip = false) : Skip(Skip) {}

  bool shouldRunPass(const StringRef PassName,
                     StringRef IRDescription) override;
  bool isEnabled() const override { return true; }
  bool checkPass(const StringRef PassName, const StringRef TargetDesc);
  void setSkip(bool Skip) { this->Skip = Skip; }
  bool getSkip() const { return Skip; }

private:
  bool Skip;
};

// Returns a static AutoTuningOptPassGate object which will be used to register
// CallBack for OptBisect instrumentation.
// It will also be used by AutoTuningCompile passes to enable/disable
// optimization passes.
AutoTuningOptPassGate &getAutoTuningOptPassGate();

class AutoTuningCompileModule {
public:
  explicit AutoTuningCompileModule(std::string Pass = "unknown");
  bool run(Module &M);
  // Write IR files for each module to be re-used in subsequent compilations
  // for autotuning cycles. It only works with -fautotune-generate.
  void writeIRFiles(Module &M) const;
  // Enable/Disable execution of optimization passes in subsequent compilations
  // based on autotuning methodology and available opportunities. It Only works
  // with -fautotune
  bool modifyCompilationPipeline(Module &M) const;

  static void setSkipCompilation(bool Option) { SkipCompilation = Option; }
  static bool getSkipCompilation() { return SkipCompilation; }

private:
  static bool SkipCompilation;
  std::string Pass = "";
};

class AutoTuningCompileModuleLegacy : public ModulePass {
public:
  static char ID;
  explicit AutoTuningCompileModuleLegacy(std::string Pass = "unknown");
  bool runOnModule(Module &M) override;
  StringRef getPassName() const override;
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

private:
  std::string Pass = "";
};

class AutoTuningCompileModulePass
    : public PassInfoMixin<AutoTuningCompileModulePass> {
public:
  explicit AutoTuningCompileModulePass(std::string Pass = "unknown")
      : Pass(Pass){};
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);

private:
  std::string Pass = "";
};

class AutoTuningCompileFunction {
public:
  explicit AutoTuningCompileFunction(std::string Pass = "unknown");
  bool run(Function &F);
  // Write IR files for each module to be re-used in subsequent compilations
  // for autotuning cycles. It only works with -fautotune-generate.
  void writeIRFiles(Module &M);
  // Enable/Disable execution of optimization passes in subsequent compilations
  // based on autotuning methodology and available opportunities. It Only works
  // with -fautotune
  bool modifyCompilationPipeline(Function &F);

private:
  // A module may have multiple functions; decision to enable/disable
  // execution of an optimization pass will be made for the first function and
  // will be used for all of the functions in the module.
  // 'SkipDecision' will be set once the decision is made for a specific 'Pass'.
  bool SkipDecision = false;

  // A module may have multiple functions; IR file will be written once for the
  // entire module for a specific 'Pass'.
  bool IsModuleWritten = false;
  std::string Pass = "";
};

class AutoTuningCompileFunctionLegacy : public FunctionPass {
public:
  static char ID;
  explicit AutoTuningCompileFunctionLegacy(std::string Pass = "unknown");
  bool runOnFunction(Function &F) override;
  StringRef getPassName() const override;
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

private:
  std::string Pass = "";
};

class AutoTuningCompileFunctionPass
    : public PassInfoMixin<AutoTuningCompileFunctionPass> {
public:
  explicit AutoTuningCompileFunctionPass(std::string Pass = "unknown")
      : Pass(Pass){};
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

private:
  std::string Pass = "";
};

class AutoTuningCompileLoopPass
    : public PassInfoMixin<AutoTuningCompileLoopPass> {
public:
  explicit AutoTuningCompileLoopPass(std::string Pass = "unknown")
      : Pass(Pass){};
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);

private:
  std::string Pass = "";
};

} // end namespace llvm

#endif /* LLVM_AUTOTUNER_AUTOTUNING_COMPILE_H_ */
#endif
