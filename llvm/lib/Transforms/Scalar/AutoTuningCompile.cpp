#if defined(ENABLE_AUTOTUNER)
//===--------------- AutoTuningCompile.cpp - Auto-Tuning ------------------===//
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
/// This pass implements incremental compilation for AutoTuner to reduce the
/// compilation time for tuning process.
/// This pass performs 2 operations.
/// 1. Writing module level IR files which can be used in subsequent
///    compilations for AutoTuner flow. So clang frontend don't have to process
///    the source code from scratch.
/// 2. Add/Remove attributes for modules and functions to enable/disable
///    execution of optimization pass(es). It further reduces the compilation
///    time by skipping optimization pass(es) (If feasible).
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/AutoTuningCompile.h"
#include "llvm/Analysis/AutotuningDump.h"
#include "llvm/AutoTuner/AutoTuning.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Scalar.h"
#include <string>

// Enable debug messages for AutoTuning Compilation.
#define DEBUG_TYPE "autotuning-compile"

using namespace llvm;

extern cl::opt<AutoTuningCompileOpt> AutoTuningCompileMode;

AutoTuningOptPassGate SkipPasses = AutoTuningOptPassGate(true);
AutoTuningOptPassGate RunPasses = AutoTuningOptPassGate(false);
bool AutoTuningCompileModule::SkipCompilation = false;

static void writeFiles(Module &M, std::string Pass) {
  if (autotuning::Engine.isGenerateOutput()) {
    switch (AutoTuningCompileMode) {
    case Basic:
    case CoarseGrain:
      if (Pass == autotuning::CompileOptionStart) {
        LLVM_DEBUG(dbgs() << "AutoTuningCompile: IR files writing before Pass: "
                          << Pass << ".\n");
        auto ATD = new AutotuningDumpLegacy(/* Incremental Compilation */ true);
        ATD->runOnModule(M);
      }
      break;
    case FineGrain:
      if (autotuning::Engine.hasOpportunities()) {
        LLVM_DEBUG(dbgs() << "AutoTuningCompile: IR files writing before Pass: "
                          << Pass << ".\n");
        auto ATD = new AutotuningDumpLegacy(/* Incremental Compilation */ true);
        ATD->runOnModule(M);
      }
      break;
    default:
      llvm_unreachable("AutoTuningCompile: Unknown AutoTuner Incremental "
                       "Compilation mode.\n");
    }
  }
}

bool AutoTuningOptPassGate::shouldRunPass(const StringRef PassName,
                                          StringRef IRDescription) {
  LLVM_DEBUG(dbgs() << "Skip pass '" << PassName
                    << "': " << (Skip ? "True" : "False") << '\n');
  return !Skip;
}

bool AutoTuningOptPassGate::checkPass(const StringRef PassName,
                                      const StringRef TargetDesc) {
  if (PassName.startswith("AutoTuningCompile")) {
    LLVM_DEBUG(dbgs() << "Running '" << PassName << "'pass.\n");
    return true;
  }

  LLVM_DEBUG(dbgs() << "Skip pass '" << PassName
                    << "': " << (Skip ? "True" : "False") << '\n');
  return !Skip;
}

AutoTuningCompileModule::AutoTuningCompileModule(std::string Pass) {
  this->Pass = Pass;
}

void AutoTuningCompileModule::writeIRFiles(Module &M) const {
  writeFiles(M, Pass);
}

bool AutoTuningCompileModule::modifyCompilationPipeline(Module &M) const {
  bool Changed = false;
  LLVM_DEBUG(dbgs() << "AutoTuningCompile: Deciding to enable/disable "
                       "optimization of module/functions. Pass: "
                    << Pass << '\n');

  StringRef Filename = M.getName();
  size_t Pos = Filename.rfind(".ll");
  if (Pos == StringRef::npos) {
    errs() << "AutoTuningCompile: Source file is not IR (.ll) file. "
              "Disabling incremental compilation.\n";
    AutoTuningCompileMode = Inactive;
    return Changed;
  }
  Filename = Filename.substr(0, Pos);

  switch (AutoTuningCompileMode) {
  case Basic:
  case CoarseGrain:
    LLVM_DEBUG(dbgs() << "AutoTuningCompile: No change in opt pipeline for "
                         "Basic/CoarseGrain incremental compilation mode.\n");
    break;
  case FineGrain: {
    if (Pass == autotuning::CompileOptionStart) {
      M.getContext().setOptPassGate(SkipPasses);
      getAutoTuningOptPassGate().setSkip(true);
      setSkipCompilation(true);
      LLVM_DEBUG(dbgs() << "AutoTuningCompile: SkipPasses enabled.\n");
    } else if (getSkipCompilation() &&
               (autotuning::Engine.shouldRunOptPass(Filename.str(), Pass) ||
                Pass == "end")) {
      M.getContext().setOptPassGate(RunPasses);
      getAutoTuningOptPassGate().setSkip(false);
      setSkipCompilation(false);
      LLVM_DEBUG(dbgs() << "AutoTuningCompile: SkipPasses disabled.\n");
    } else
      LLVM_DEBUG(dbgs() << "AutoTuningCompile: Old decision (SkipPasses = "
                        << (getSkipCompilation() ? "True" : "False")
                        << " ) continued.\n");

    Changed = true;
    break;
  }
  default:
    llvm_unreachable(
        "AutoTuningCompile: Unknown AutoTuner Incremental Compilation mode.\n");
  }

  return Changed;
}

bool AutoTuningCompileModule::run(Module &M) {
  bool Changed = false;
  if (AutoTuningCompileMode == Inactive)
    return Changed;

  if (!autotuning::Engine.isEnabled()) {
    LLVM_DEBUG(dbgs() << "AutoTuningCompile: AutoTuner is not enabled.\n");
    return Changed;
  }

  writeIRFiles(M);

  if (autotuning::Engine.isParseInput())
    Changed |= modifyCompilationPipeline(M);

  return Changed;
}

AutoTuningCompileModuleLegacy::AutoTuningCompileModuleLegacy(std::string Pass)
    : ModulePass(AutoTuningCompileModuleLegacy::ID) {
  this->Pass = Pass;
}

bool AutoTuningCompileModuleLegacy::runOnModule(Module &M) {
  AutoTuningCompileModule Impl(Pass);
  return Impl.run(M);
}

char AutoTuningCompileModuleLegacy::ID = 0;

StringRef AutoTuningCompileModuleLegacy::getPassName() const {
  return "AutoTuner Incremental Compilation";
}

INITIALIZE_PASS(AutoTuningCompileModuleLegacy, "autotuning-compile-module",
                "AutoTuner Incremental Compilation", false, false)

// Public interface to the AutoTuningCompile pass
ModulePass *llvm::createAutoTuningCompileModuleLegacyPass(std::string Pass) {
  return new AutoTuningCompileModuleLegacy(Pass);
}

PreservedAnalyses AutoTuningCompileModulePass::run(Module &M,
                                                   ModuleAnalysisManager &) {
  AutoTuningCompileModule Impl(Pass);
  Impl.run(M);
  return PreservedAnalyses::all();
}

AutoTuningCompileFunction::AutoTuningCompileFunction(std::string Pass) {
  this->Pass = Pass;
}

void AutoTuningCompileFunction::writeIRFiles(Module &M) {
  if (IsModuleWritten)
    return;
  IsModuleWritten = true;
  writeFiles(M, Pass);
}

bool AutoTuningCompileFunction::modifyCompilationPipeline(Function &F) {
  bool Changed = false;
  LLVM_DEBUG(dbgs() << "AutoTuningCompile: Deciding to enable/disable "
                       "optimization of module/functions. Pass: "
                    << Pass << '\n');
  Module *M = F.getParent();
  StringRef Filename = M->getName();
  size_t Pos = Filename.rfind(".ll");
  if (Pos == StringRef::npos) {
    errs() << "AutoTuningCompile: Source file is not IR (.ll) file. "
              "Disabling incremental compilation.\n";
    AutoTuningCompileMode = Inactive;
    return Changed;
  }
  Filename = Filename.substr(0, Pos);

  switch (AutoTuningCompileMode) {
  case Basic:
  case CoarseGrain:
    LLVM_DEBUG(dbgs() << "AutoTuningCompile: No change in opt pipeline for "
                         "Basic/CoarseGrain incremental compilation mode.\n");
    break;
  case FineGrain: {
    if (!AutoTuningCompileModule::getSkipCompilation() &&
        Pass == autotuning::CompileOptionStart) {
      if (!SkipDecision) {
        M->getContext().setOptPassGate(SkipPasses);
        getAutoTuningOptPassGate().setSkip(true);
        SkipDecision = true;
      }
      AutoTuningCompileModule::setSkipCompilation(true);
      LLVM_DEBUG(dbgs() << "AutoTuningCompile: SkipPasses enabled.\n");
    } else if (AutoTuningCompileModule::getSkipCompilation() &&
               Pass != autotuning::CompileOptionStart &&
               (autotuning::Engine.shouldRunOptPass(Filename.str(), Pass) ||
                Pass == autotuning::CompileOptionEnd)) {
      M->getContext().setOptPassGate(RunPasses);
      getAutoTuningOptPassGate().setSkip(false);
      SkipDecision = false;
      AutoTuningCompileModule::setSkipCompilation(false);
      LLVM_DEBUG(dbgs() << "AutoTuningCompile: SkipPasses disabled.\n");
    } else
      LLVM_DEBUG(dbgs() << "AutoTuningCompile: Old decision (SkipPasses = "
                        << (AutoTuningCompileModule::getSkipCompilation()
                                ? "True"
                                : "False")
                        << " ) continued.\n");

    Changed = true;
    break;
  }
  default:
    llvm_unreachable(
        "AutoTuningCompile: Unknown AutoTuner Incremental Compilation mode.\n");
  }

  return Changed;
}

bool AutoTuningCompileFunction::run(Function &F) {
  bool Changed = false;
  if (AutoTuningCompileMode == Inactive)
    return Changed;

  if (!autotuning::Engine.isEnabled()) {
    LLVM_DEBUG(dbgs() << "AutoTuningCompile: AutoTuner is not enabled.\n");
    return Changed;
  }

  writeIRFiles(*F.getParent());

  if (autotuning::Engine.isParseInput())
    Changed |= modifyCompilationPipeline(F);

  return Changed;
}

AutoTuningCompileFunctionLegacy::AutoTuningCompileFunctionLegacy(
    std::string Pass)
    : FunctionPass(AutoTuningCompileFunctionLegacy::ID) {
  this->Pass = Pass;
}

bool AutoTuningCompileFunctionLegacy::runOnFunction(Function &F) {
  AutoTuningCompileFunction Impl(Pass);
  return Impl.run(F);
}

char AutoTuningCompileFunctionLegacy::ID = 0;

StringRef AutoTuningCompileFunctionLegacy::getPassName() const {
  return "AutoTuner Incremental Compilation";
}

INITIALIZE_PASS(AutoTuningCompileFunctionLegacy, "autotuning-compile-function",
                "AutoTuner Incremental Compilation", false, false)

// Public interface to the AutoTuningCompile pass
FunctionPass *
llvm::createAutoTuningCompileFunctionLegacyPass(std::string Pass) {
  return new AutoTuningCompileFunctionLegacy(Pass);
}

PreservedAnalyses
AutoTuningCompileFunctionPass::run(Function &F, FunctionAnalysisManager &AM) {
  AutoTuningCompileFunction Impl(Pass);
  Impl.run(F);
  return PreservedAnalyses::all();
}

PreservedAnalyses
AutoTuningCompileLoopPass::run(Loop &L, LoopAnalysisManager &AM,
                               LoopStandardAnalysisResults &AR, LPMUpdater &U) {
  AutoTuningCompileFunction Impl(Pass);
  Function *F = L.getHeader()->getParent();
  Impl.run(*F);
  return PreservedAnalyses::all();
}

AutoTuningOptPassGate &llvm::getAutoTuningOptPassGate() {
  static AutoTuningOptPassGate AutoTuningGate;
  return AutoTuningGate;
}

#endif
