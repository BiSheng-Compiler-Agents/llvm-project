//===- ACPOFIModel.h - AI-Enabled Continuous Program Optimization ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(ENABLE_ACPO)
#ifndef LLVM_ANALYSIS_ACPOFIMODEL_H
#define LLVM_ANALYSIS_ACPOFIMODEL_H

#include "llvm/Analysis/ACPOModel.h"
#include "llvm/Analysis/DumpFeature.h"
#include "llvm/Analysis/FunctionPropertiesAnalysis.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/InlineSizeEstimatorAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"

#include <map>

namespace llvm {

//class ACPOmodel;

class ACPOFIModel : public ACPOModel {
public:
  ACPOFIModel(CallBase *CB, InlineAdvisor *IA, OptimizationRemarkEmitter *ORE,
              bool OnlyMandatory, bool UseML = true);

  ~ACPOFIModel();

  void setMLCustomFeatures(
      std::vector<std::pair<std::string, std::string>> FeatureValues);

  void sendCustomFeatures() override;

  InlineAdvisor *getNotACPOAdvisor();

  // Recorder's to micmic the behavior for default InlineAdvice.
  // If the model is turned off or was decided to fall back to
  // default inline advisor then we need to make sure the advice returned
  // is properly recorded. Or else there would be an error.
  void recordUnattemptedInlining();

  void recordInlining();

  void recordUnsuccessfulInlining(InlineResult &IR);

  void recordInliningWithCalleeDeleted();

  // Interface for IRToPerf Cache system.
  struct FunctionFeaturesCache {
    using FunctionSizeMap = DenseMap<const Function *, size_t>;
    using FunctionFloatMap = DenseMap<const Function *, float>;

    std::array<FunctionSizeMap,
               static_cast<size_t>(
                   ACPOFIExtendedFeatures::NamedFeatureIndex::NumNamedFeatures)>
        NamedFeatures;
    std::array<FunctionFloatMap,
               static_cast<size_t>(
                   ACPOFIExtendedFeatures::NamedFloatFeatureIndex::
                       NumNamedFloatFeatures)>
        NamedFloatFeatures;

    FunctionSizeMap &operator[](ACPOFIExtendedFeatures::NamedFeatureIndex Pos) {
      return NamedFeatures[static_cast<size_t>(Pos)];
    }
    FunctionFloatMap &
    operator[](ACPOFIExtendedFeatures::NamedFloatFeatureIndex Pos) {
      return NamedFloatFeatures[static_cast<size_t>(Pos)];
    }
  };

  struct FunctionAnalysisMap {
    DenseMap<const Function *, const DominatorTree *> DomCache;
    DenseMap<const Function *, const LoopInfo *> LICache;
    DenseMap<const Function *, const TargetTransformInfo *> TTICache;
  };

  // Invalidation mechanisms
  static void invalidateCache(CallBase *CB);

  static void invalidateCache(const Function *F);

  static void clearCache();

  // Getters/setters for the cache system.
  static std::optional<size_t>
  getCachedSize(const Function *F,
                ACPOFIExtendedFeatures::NamedFeatureIndex idx);

  static std::optional<float>
  getCachedFloat(const Function *F,
                 ACPOFIExtendedFeatures::NamedFloatFeatureIndex idx);

  static void insertSizeCache(const Function *F,
                              ACPOFIExtendedFeatures::NamedFeatureIndex idx,
                              size_t val);

  static void
  insertFloatCache(const Function *F,
                   ACPOFIExtendedFeatures::NamedFloatFeatureIndex idx,
                   float val);

  static const DominatorTree *getDomCachedAnalysis(const Function *F);

  static const LoopInfo *getLICachedAnalysis(const Function *F);

  static const TargetTransformInfo *getTTICachedAnalysis(const Function *F);

  static void insertAnalysisCache(const Function *F, const DominatorTree *Tree);

  static void insertAnalysisCache(const Function *F, const LoopInfo *LI);

  static void insertAnalysisCache(const Function *F,
                                  const TargetTransformInfo *TTI);

protected:
  // Interface to run the MLInference/default advisor and get advice from the
  // model/default advisor
  virtual std::unique_ptr<ACPOAdvice> getAdviceML() override;

  virtual std::unique_ptr<ACPOAdvice> getAdviceNoML() override;

private:
  static FunctionFeaturesCache FeatureCache;
  static FunctionAnalysisMap FunctionAnalysisCache;
  CallBase *CurrentCB = nullptr;
  InlineAdvisor *NotACPOAdvisor = nullptr;
  bool ShouldInline = false;
  bool OnlyMandatory = false;
  std::unique_ptr<InlineAdvice> NotACPOAdvice = nullptr;
  std::vector<std::pair<std::string, std::string>> CustomFeatureValues;
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_ACPOFIMODEL_H

#endif // ENABLE_ACPO
