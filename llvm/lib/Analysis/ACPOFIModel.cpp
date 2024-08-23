//===- ACPOFIModel.cpp - AI-Enabled Continuous Program Optimization -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the interface between ACPO and ML-guided optimizations.
// It delegates decision making to inference with a pre-trained model.
//
//===----------------------------------------------------------------------===//

#if defined(ENABLE_ACPO)
#include "llvm/Analysis/ACPOFIModel.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Process.h"

using namespace llvm;

#define DEBUG_TYPE "acpo"
#define ACPO_ENV_VAR_DIR "ACPO_DIR"

cl::opt<bool>
    EnableACPOFI("enable-acpo-fi", cl::init(false), cl::Hidden,
                 cl::desc("Leverage ACPO ML model to decide inlining."));

cl::opt<bool>
    EnableAOTFI("enable-acpo-fi-aot", cl::init(false), cl::Hidden,
                cl::desc("Leverage AOT ML model to decide inlining."));

ACPOFIModel::ACPOFIModel(CallBase *CB, InlineAdvisor *IA,
                         OptimizationRemarkEmitter *ORE, bool OnlyMandatory,
                         bool UseML)
    : ACPOModel(ORE, UseML), CurrentCB(CB), NotACPOAdvisor(IA),
      OnlyMandatory(OnlyMandatory) {
  Function *Caller = CB->getCaller();
  LLVMContext *Context = &(Caller->getContext());
  setContextPtr(Context);
  if (EnableACPOFI)
    // ACPO Python support
    setMLIF(createPersistentPythonMLIF());
  else if (EnableAOTFI)
    // ACPO AOT support
    setMLIF(createPersistentCompiledMLIF());
}

ACPOFIModel::~ACPOFIModel() {}

void ACPOFIModel::setMLCustomFeatures(
    std::vector<std::pair<std::string, std::string>> FeatureValues) {
  CustomFeatureValues = FeatureValues;
}

void ACPOFIModel::sendCustomFeatures() {
  // Get an ACPOMLInterface to communicate with the Python side
  std::shared_ptr<ACPOMLInterface> MLIF = getMLIF();
  MLIF->initializeFeatures("FI", CustomFeatureValues);
}

void ACPOFIModel::recordUnattemptedInlining() {
  if (NotACPOAdvice)
    NotACPOAdvice->recordUnattemptedInlining();
}

void ACPOFIModel::recordInlining() {
  if (NotACPOAdvice)
    NotACPOAdvice->recordInlining();
}

void ACPOFIModel::recordUnsuccessfulInlining(InlineResult &IR) {
  if (NotACPOAdvice)
    NotACPOAdvice->recordUnsuccessfulInlining(IR);
}

void ACPOFIModel::recordInliningWithCalleeDeleted() {
  if (NotACPOAdvice)
    NotACPOAdvice->recordInliningWithCalleeDeleted();
}

void ACPOFIModel::invalidateCache(CallBase *CB) {
  if (CB) {
    invalidateCache(CB->getCaller());
  }
}

InlineAdvisor *ACPOFIModel::getNotACPOAdvisor() { return NotACPOAdvisor; }

void ACPOFIModel::invalidateCache(const Function *F) {
  for (ACPOFIExtendedFeatures::NamedFeatureIndex feature =
           ACPOFIExtendedFeatures::NamedFeatureIndex(0);
       feature != ACPOFIExtendedFeatures::NamedFeatureIndex::NumNamedFeatures;
       ++feature) {
    FeatureCache[feature].erase(F);
  }
  for (ACPOFIExtendedFeatures::NamedFloatFeatureIndex feature =
           ACPOFIExtendedFeatures::NamedFloatFeatureIndex(0);
       feature !=
       ACPOFIExtendedFeatures::NamedFloatFeatureIndex::NumNamedFloatFeatures;
       ++feature) {
    FeatureCache[feature].erase(F);
  }
  FunctionAnalysisCache.DomCache.erase(F);
  FunctionAnalysisCache.LICache.erase(F);
  FunctionAnalysisCache.TTICache.erase(F);
}

void ACPOFIModel::clearCache() {
  for (ACPOFIExtendedFeatures::NamedFeatureIndex feature =
           ACPOFIExtendedFeatures::NamedFeatureIndex(0);
       feature != ACPOFIExtendedFeatures::NamedFeatureIndex::NumNamedFeatures;
       ++feature) {
    FeatureCache[feature].clear();
  }
  for (ACPOFIExtendedFeatures::NamedFloatFeatureIndex feature =
           ACPOFIExtendedFeatures::NamedFloatFeatureIndex(0);
       feature !=
       ACPOFIExtendedFeatures::NamedFloatFeatureIndex::NumNamedFloatFeatures;
       ++feature) {
    FeatureCache[feature].clear();
  }
  FunctionAnalysisCache.DomCache.clear();
  FunctionAnalysisCache.LICache.clear();
  FunctionAnalysisCache.TTICache.clear();
}

std::optional<size_t>
ACPOFIModel::getCachedSize(const Function *F,
                           ACPOFIExtendedFeatures::NamedFeatureIndex idx) {
  auto it = FeatureCache[idx].find(F);
  return it != FeatureCache[idx].end() ? std::optional<size_t>(it->second)
                                       : std::nullopt;
}

std::optional<float> ACPOFIModel::getCachedFloat(
    const Function *F, ACPOFIExtendedFeatures::NamedFloatFeatureIndex idx) {
  auto it = FeatureCache[idx].find(F);
  return it != FeatureCache[idx].end() ? std::optional<float>(it->second)
                                       : std::nullopt;
}

void ACPOFIModel::insertSizeCache(const Function *F,
                                  ACPOFIExtendedFeatures::NamedFeatureIndex idx,
                                  size_t val) {
  FeatureCache[idx].insert(std::make_pair(F, val));
}

void ACPOFIModel::insertFloatCache(
    const Function *F, ACPOFIExtendedFeatures::NamedFloatFeatureIndex idx,
    float val) {
  FeatureCache[idx].insert(std::make_pair(F, val));
}

const DominatorTree *ACPOFIModel::getDomCachedAnalysis(const Function *F) {
  auto it = FunctionAnalysisCache.DomCache.find(F);
  return it != FunctionAnalysisCache.DomCache.end() ? it->second : nullptr;
}

const LoopInfo *ACPOFIModel::getLICachedAnalysis(const Function *F) {
  auto it = FunctionAnalysisCache.LICache.find(F);
  return it != FunctionAnalysisCache.LICache.end() ? it->second : nullptr;
}

const TargetTransformInfo *
ACPOFIModel::getTTICachedAnalysis(const Function *F) {
  auto it = FunctionAnalysisCache.TTICache.find(F);
  return it != FunctionAnalysisCache.TTICache.end() ? it->second : nullptr;
}

void ACPOFIModel::insertAnalysisCache(const Function *F,
                                      const DominatorTree *Tree) {
  FunctionAnalysisCache.DomCache.insert(std::make_pair(F, Tree));
}

void ACPOFIModel::insertAnalysisCache(const Function *F, const LoopInfo *LI) {
  FunctionAnalysisCache.LICache.insert(std::make_pair(F, LI));
}

void ACPOFIModel::insertAnalysisCache(const Function *F,
                                      const TargetTransformInfo *TTI) {
  FunctionAnalysisCache.TTICache.insert(std::make_pair(F, TTI));
}

std::unique_ptr<ACPOAdvice> ACPOFIModel::getAdviceML() {
  std::shared_ptr<ACPOMLInterface> MLIF = getMLIF();
  // Generate result.
  std::unique_ptr<ACPOAdvice> Advice = std::make_unique<ACPOAdvice>();
  // handle mandatory case, forcestop, never inline  or not inlinable cases
  if (OnlyMandatory)
    return getAdviceNoML();
  if (NotACPOAdvisor->neverInline(*CurrentCB) ||
      !NotACPOAdvisor->isCSInlinable(*CurrentCB)) {
    Advice->addField("FI-ShouldInline",
                     ConstantInt::get(Type::getInt64Ty(*(getContextPtr())),
                                      (int64_t) false));
    NotACPOAdvice = nullptr;
    return Advice;
  }
  std::optional<std::string> Env = llvm::sys::Process::GetEnv(ACPO_ENV_VAR_DIR);
  if (!Env || *Env == "") {
    std::optional<std::string> LLVMDIROpt =
        llvm::sys::Process::GetEnv("LLVM_DIR");
    if (!LLVMDIROpt) {
      outs() << "ACPO_DIR not found. "
             << "Did you export ACPO_DIR to $LLVM_DIR/acpo ?\n"
             << "Falling back to default advisor. \n";
      return getAdviceNoML();
    }
  }
  assert(MLIF != nullptr);
  if (!MLIF->loadModel("model-fi.acpo")) {
    outs() << "Model not loaded correctly. \n";
    return getAdviceNoML();
  }
  if (!MLIF->initializeFeatures("FI", CustomFeatureValues)) {
    outs() << "Features not initialized correctly. \n";
    return getAdviceNoML();
  }
  bool ModelRunOK = MLIF->runModel("FI");
  assert(ModelRunOK);
  ShouldInline = MLIF->getModelResultI("FI-ShouldInline");
  assert(getContextPtr() != nullptr);
  Advice->addField("FI-ShouldInline",
                   ConstantInt::get(Type::getInt64Ty(*(getContextPtr())),
                                    (int64_t)ShouldInline));
  return Advice;
}

std::unique_ptr<ACPOAdvice> ACPOFIModel::getAdviceNoML() {
  // Use the advisor used by default inlining
  std::unique_ptr<ACPOAdvice> Advice = std::make_unique<ACPOAdvice>();
  assert(getContextPtr() != nullptr);
  NotACPOAdvice = NotACPOAdvisor->getAdvice(*CurrentCB, OnlyMandatory);
  bool ShouldInline = NotACPOAdvice->isInliningRecommended();
  Advice->addField("FI-ShouldInline",
                   ConstantInt::get(Type::getInt64Ty(*(getContextPtr())),
                                    (int64_t)ShouldInline));
  return Advice;
}

ACPOFIModel::FunctionFeaturesCache ACPOFIModel::FeatureCache;
ACPOFIModel::FunctionAnalysisMap ACPOFIModel::FunctionAnalysisCache;
#endif // ENABLE_ACPO
