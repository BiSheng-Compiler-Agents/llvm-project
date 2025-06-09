//===- AI4CAnalysis.cpp - AI4C Class for AOT ML model ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implement AOT ML model to decide function hotness
//
//===----------------------------------------------------------------------===//
#if defined(ENABLE_ACPO)

#include "llvm/Transforms/Instrumentation/AI4CAnalysis.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/ModelDataCollector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Instrumentation/ACPOAI4CFHModel.h"
#include "llvm/Transforms/Utils/CallGraphUpdater.h"
#include "ValueProfileCollector.h"
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "ai4c-analysis"

static cl::opt<std::string> AI4CDumpFile(
    "ai4c-dump-file", cl::init("-"), cl::Hidden,
    cl::desc("Name of a file to store AI4C feature/result data in."));

cl::opt<bool>
    EnableAI4CFH("enable-ai4c-fh", cl::init(false), cl::Hidden,
                 cl::desc("Levarage AOT ML model to decide Function hotness."));

namespace {
/// Class for collecting AI4C FH model data
class ModelDataAI4CFHCollector : public ModelDataCollector {
public:
  ModelDataAI4CFHCollector(formatted_raw_ostream &OS,
                           std::string OutputFileName)
      : ModelDataCollector(OS, OutputFileName) {}
  
  void collectFeatures(Function *GlobalF, FunctionAnalysisManager *FAM) {
    resetRegisteredFeatures();
    Module *GlobalM = GlobalF->getParent();
    ACPOCollectFeatures::FeatureInfo GlobalFeatureInfo {
      ACPOCollectFeatures::FeatureIndex::NumOfFeatures,
      {FAM, nullptr},
      {GlobalF, nullptr, nullptr, GlobalM, nullptr}};

    registerFeature({ACPOCollectFeatures::Scope::Function}, GlobalFeatureInfo);
    ModelDataCollector::collectFeatures();
  }
};

llvm::SmallDenseSet<std::pair<CallGraphNode *, CallGraphSCC *>, 4>
    InlinedInternalEdges =
        llvm::SmallDenseSet<std::pair<CallGraphNode *, CallGraphSCC *>, 4>();
} // end anonymous namespace

int64_t getACPOAdvice(Function *F, FunctionAnalysisManager *FAM,
                      ModelDataAI4CFHCollector *MDC) {
  auto &ORE = FAM->getResult<OptimizationRemarkEmitterAnalysis>(*F);
  std::unique_ptr<ACPOAI4CFHModel> AI4CFH = 
      std::make_unique<ACPOAI4CFHModel>(&(F->getContext()), &ORE);
  std::vector<std::pair<std::string, std::string>> Features = 
      MDC->getFeatures();
  AI4CFH->setMLCustomFeatures(Features);
  std::unique_ptr<ACPOAdvice> Advice = AI4CFH->getAdvice();
  Constant *Val = Advice->getField("FH");
  assert(Val != nullptr);
  assert(isa<ConstantInt>(Val));
  ConstantInt *FH = dyn_cast<ConstantInt>(Val);
  return FH->getSExtValue();
}

AI4CAnalysis::AI4CAnalysis() {}

static bool skipAnalysis(const Function &F) {
  if (F.isDeclaration())
    return true;
  if (F.hasFnAttribute(llvm::Attribute::NoProfile))
    return true;
  if (F.hasFnAttribute(llvm::Attribute::SkipProfile))
    return true;

  return false;
}

PreservedAnalyses  AI4CAnalysis::run(Module &M, ModuleAnalysisManager &MAM) {
  if (EnableAI4CFH) {
    LLVM_DEBUG(dbgs << "Annotate function hotness by ACPO: ");
    // Initialize Feature Data Collector
    std::error_code EC;
    raw_fd_ostream RawOS(AI4CDumpFile.getValue(), EC, sys::fs::CD_OpenAlways,
                        sys::fs::FA_Write, sys::fs::OF_Append);
    formatted_raw_ostream OS(RawOS);
    ModelDataAI4CFHCollector MDC(OS, AI4CDumpFile);
    FunctionAnalysisManager &FAM =
        MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
    std::vector<Function *> HotFunctions;
    std::vector<Function *> ColdFunctions;

    for (auto &F: M) {
      if (skipAnalysis(F))
        continue;
      MDC.collectFeatures(&F, &FAM);
      FuncFreqAttr FreqAttr = (FuncFreqAttr)getACPOAdvice(&F, &FAM, &MDC);
      if (FreqAttr == FFA_Cold)
        ColdFunctions.push_back(&F);
      else if (FreqAttr == FFA_Hot)
        HotFunctions.push_back(&F);
    }

    for (auto &F : HotFunctions) {
      F->addFnAttr(Attribute::AlwaysInline);
      LLVM_DEBUG(dbgs << "Set inline attribute to function " << F->getName()
                      << "\n");
    }
    for (auto &F : ColdFunctions) {
      // Only set when there is no Attribute::Hot set by the user. For hot
      // attribute, user's annotation has the precedence over the profile.
      if (F->hasFnAttribute(Attribute::Hot)) {
        auto &Ctx = M.getContext();
        std::string Msg = std::string("Function ") + F->getName().str() +
                          std::string(" is annotated as a hot function but"
                                      " the profile is cold");
        Ctx.diagnose(
            DiagnosticInfoPGOProfile(M.getName().data(), Msg, DS_Warning));
        continue;
      }
      F->addFnAttr(Attribute::Cold);
      LLVM_DEBUG(dbgs() << "Set cold attribute to function: " << F->getName()
                        << "\n");
    }
    
    return PreservedAnalyses::none();
  } else {
    return PreservedAnalyses::all();
  }
}
#endif //Enable_ACPO
