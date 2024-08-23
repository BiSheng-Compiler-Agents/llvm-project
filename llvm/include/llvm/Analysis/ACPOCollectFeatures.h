//===- ACPOCollectFeatures.h - ACPO Class for Feature Collection ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header file defines the type, scope, and number of features to be
// collected on a given ACPOModel class from all available features.
//
//===----------------------------------------------------------------------===//

#if defined(ENABLE_ACPO)
#ifndef LLVM_ANALYSIS_ACPOCOLLECTFEATURES_H
#define LLVM_ANALYSIS_ACPOCOLLECTFEATURES_H
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"

#include <ios>
#include <memory>
#include <ostream>
#include <set>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace llvm {
class ACPOCollectFeatures {
public:
  // A feature is related to one of the following scope
  enum class Scope {
    Module,
    Function,
    Loop,
    Callgraph,
    CallSite,
    NumOfScope,
  };

  // In the future as more features are added, features can be calculated
  // simultanously.
  // Suppose feature A and B could be calculated in the same loop,
  // then it would make sense to calculate both the features at the same time
  // and save it in a cache system
  // (which could be implemented similarly like Dumpfeatures.h/cpp).
  enum class GroupID {
    EdgeNodeCount,
    FPIRelated,
    HotColdCallSite,
    InlineCostFeatureGroup,
    ACPOFIExtendedFeatures,
    NumOfGroupID
  };

  // List of features we support to be calculated.
  // (1) For each feature there should be a corresponding scope on which it
  // depends on for calculating.
  // (2) A feature may belong in a group for which those features could be
  //     calculated together.
  // (3) Once you decided to add a feature you should register it to all the
  //     static maps in the .cpp file. Except for some special indicator enum's
  //     like InlineCostFeatureGroupBegin/End
  enum class FeatureIndex {
    // Begin: InlineCostFeatureGroup
    InlineCostFeatureGroupBegin,
    SROASavings,
    SROALosses,
    LoadElimination,
    CallPenalty,
    CallArgumentSetup,
    LoadRelativeIntrinsic,
    LoweredCallArgSetup,
    IndirectCallPenalty,
    JumpTablePenalty,
    CaseClusterPenalty,
    SwitchPenalty,
    UnsimplifiedCommonInstructions,
    NumLoops,
    DeadBlocks,
    SimplifiedInstructions,
    ConstantArgs,
    ConstantOffsetPtrArgs,
    CallSiteCost,
    ColdCcPenalty,
    LastCallToStaticBonus,
    IsMultipleBlocks,
    NestedInlines,
    NestedInlineCostEstimate,
    Threshold,
    InlineCostFeatureGroupEnd,
    // End: InlineCostFeatureGroup

    // Begin: FPIRelated
    BasicBlockCount,
    BlocksReachedFromConditionalInstruction,
    Uses,
    // End: FPIRelated

    // Begin: EdgeNodeCount
    EdgeCount,
    NodeCount,
    // End: EdgeNodeCount

    // Begin: HotColdCallsite
    ColdCallSite,
    HotCallSite,
    // End: HotColdCallsite

    // Begin: ACPOFIExtendedFeatures
    ACPOFIExtendedFeaturesNamedFeatureBegin,
    ACPOFIExtendedFeaturesInitialSize,
    ACPOFIExtendedFeaturesBlocks,
    ACPOFIExtendedFeaturesCalls,
    ACPOFIExtendedFeaturesIsLocal,
    ACPOFIExtendedFeaturesIsLinkOnceODR,
    ACPOFIExtendedFeaturesIsLinkOnce,
    ACPOFIExtendedFeaturesLoops,
    ACPOFIExtendedFeaturesMaxLoopDepth,
    ACPOFIExtendedFeaturesMaxDomTreeLevel,
    ACPOFIExtendedFeaturesPtrArgs,
    ACPOFIExtendedFeaturesPtrCallee,
    ACPOFIExtendedFeaturesCallReturnPtr,
    ACPOFIExtendedFeaturesConditionalBranch,
    ACPOFIExtendedFeaturesCBwithArg,
    ACPOFIExtendedFeaturesCallerHeight,
    ACPOFIExtendedFeaturesCallUsage,
    ACPOFIExtendedFeaturesIsRecursive,
    ACPOFIExtendedFeaturesNumCallsiteInLoop,
    ACPOFIExtendedFeaturesNumOfCallUsesInLoop,
    ACPOFIExtendedFeaturesEntryBlockFreq,
    ACPOFIExtendedFeaturesMaxCallsiteBlockFreq,
    ACPOFIExtendedFeaturesNamedFeatureEnd,
    ACPOFIExtendedFeaturesFloatFeatureBegin,
    ACPOFIExtendedFeaturesInstructionPerBlock,
    ACPOFIExtendedFeaturesSuccessorPerBlock,
    ACPOFIExtendedFeaturesAvgVecInstr,
    ACPOFIExtendedFeaturesAvgNestedLoopLevel,
    ACPOFIExtendedFeaturesInstrPerLoop,
    ACPOFIExtendedFeaturesBlockWithMultipleSuccecorsPerLoop,
    ACPOFIExtendedFeaturesFloatFeatureEnd,
    // End: ACPOFIExtendedFeatures

    CallerBlockFreq,
    CallSiteHeight,
    ConstantParam,
    CostEstimate,
    LoopLevel,
    MandatoryKind,
    MandatoryOnly,
    OptCode,
    IsIndirectCall,
    IsInInnerLoop,
    IsMustTailCall,
    IsTailCall,
    NumOfFeatures
  };

  struct AnalysisManagers {
    FunctionAnalysisManager *FAM = nullptr;
    ModuleAnalysisManager *MAM = nullptr;
  };

  // ScopeInfo is a struct that contains the correpsonding needed information to
  // calculate the corresponding feature.
  struct ScopeInfo {
    Function *F = nullptr;
    CallBase *CB = nullptr;
    BasicBlock *BB = nullptr;
    Module *M = nullptr;
    Loop *L = nullptr;
    // Can add Instructions or other types later.
  };

  struct OtherInfo {
    bool MandatoryOnly = false;
    InlineAdvisor *IA = nullptr;
  };

  // FeatureInfo should contain all the relevant information to calculate
  // the corresponding FeatureIndex.
  struct FeatureInfo {
    // When Idx = NumOfFeatures. We assume this is a global FeatureInfo.
    FeatureIndex Idx;
    // Once we have the Idx we should know the following two attribute.
    // Scope ScopeIdx //
    // GroupID Group //
    AnalysisManagers Managers;
    ScopeInfo SI;
    OtherInfo OI;
  };

  using FeatureValueMap = std::unordered_map<FeatureIndex, std::string>;
  using FeatureInfoMap = std::unordered_map<FeatureIndex, FeatureInfo>;
  using FeaturesInfo = std::vector<FeatureInfo>;
  using Scopes = std::vector<Scope>;
  using GroupIDs = std::vector<GroupID>;
  typedef void (*CalculateFeatureFunction)(ACPOCollectFeatures &,
                                           const FeatureInfo &);

  // Constructors/Destructors
  ACPOCollectFeatures();
  ACPOCollectFeatures(FeatureInfo GlobalInfo);
  ~ACPOCollectFeatures();

  // Setters/getters
  void setFeatureValue(FeatureIndex Idx, std::string Val);

  void setFeatureInfo(FeatureIndex Idx, FeatureInfo Info);

  void setFeatureValueAndInfo(FeatureIndex Idx, FeatureInfo Info,
                              std::string Val);

  void setGlobalFeatureInfo(FeatureInfo &Info);

  std::string getFeature(FeatureIndex Idx) const;

  // Check if the feature is alrady calculated.
  bool containsFeature(FeatureIndex);
  bool containsFeature(GroupID);

  static std::string getFeatureName(FeatureIndex Idx);
  static GroupID getFeatureGroup(FeatureIndex Idx);
  static Scope getFeatureScope(FeatureIndex Idx);
  static std::set<FeatureIndex> getGroupFeatures(GroupID Group);
  static std::set<FeatureIndex> getScopeFeatures(Scope S);

  void clearFeatureValueMap();
  bool registeredFeature(FeatureIndex Idx) const;

  // Calculate and Return the feature values specified by FeaturesInfo
  FeatureValueMap getFeaturesPair(FeaturesInfo Features);

  // Calculate and Return the feature values specified from [Beg, End)
  // TODO: Make a similar method for Scopes and GroupIDs
  FeatureValueMap getFeaturesPair(FeatureIndex Beg, FeatureIndex End);

  // Calculate and Return the feature values specified by Scope.
  FeatureValueMap getFeaturesPair(Scopes);

  // Calculate and Return the feature values specified by GroupID.
  FeatureValueMap getFeaturesPair(GroupIDs);

  static InlineAdvisor::MandatoryInliningKind
  getMandatoryKind(CallBase &CB, FunctionAnalysisManager &FAM,
                   OptimizationRemarkEmitter &ORE);

  static void clearFunctionLevel();
  static void insertFunctionLevel(const Function *, unsigned);
  static std::optional<unsigned> getFunctionLevel(const Function *);

private:
  // Global mappings.
  // FeatureIndexToName and FeatureIndexToScope should be a one to one mapping.
  static const std::unordered_map<FeatureIndex, std::string> FeatureIndexToName;
  static const std::unordered_map<FeatureIndex, Scope> FeatureIndexToScope;
  static const std::unordered_map<FeatureIndex, GroupID> FeatureIndexToGroup;
  static const std::multimap<GroupID, FeatureIndex> GroupToFeatureIndices;
  static const std::multimap<Scope, FeatureIndex> ScopeToFeatureIndices;
  // The CalculateFeatureMap maps each feature to a corresponding function that
  // calculates the feature and also sets the feature value inside
  // FeatureValues field.
  static const std::unordered_map<FeatureIndex, CalculateFeatureFunction>
      CalculateFeatureMap;

  // TODO:
  // Implement the cache systems here. See similar example in DumpFeature.cpp
  // Notice we've only cached the FunctionLevels.
  // But in the future this should be generalized for all features.
  // One way to do this is to define a map from FeatureIndex -> Mapping.
  // Inside this mapping, the key should be the Scope and a set of analysis it
  // depends on.

  static std::map<const Function *, unsigned> FunctionLevels;

  // Saved FeatureValues when we collect the features.
  FeatureValueMap FeatureToValue;
  FeatureInfoMap FeatureToInfo;
  FeatureInfo GlobalFeatureInfo;
};

ACPOCollectFeatures::FeatureIndex operator+(ACPOCollectFeatures::FeatureIndex,
                                            int);
ACPOCollectFeatures::FeatureIndex operator-(ACPOCollectFeatures::FeatureIndex,
                                            int);
ACPOCollectFeatures::FeatureIndex &
operator++(ACPOCollectFeatures::FeatureIndex &);
ACPOCollectFeatures::FeatureIndex
operator++(ACPOCollectFeatures::FeatureIndex &, int);

} // namespace llvm
#endif // LLVM_ANALYSIS_ACPOCOLLECTFEATURES_H
#endif // ENABLE_ACPO
