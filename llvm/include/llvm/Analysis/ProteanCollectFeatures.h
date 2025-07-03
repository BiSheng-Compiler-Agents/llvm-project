//===- ProteanCollectFeatures.h - Class for Feature Collection ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2021-2022. Huawei Technologies Co., Ltd. All rights reserved.
//
//===----------------------------------------------------------------------===//
//
// This header file defines the type, scope, and number of features to be
// collected on a given ACPOModel class from all available features.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_PROTEANCOLLECTFEATURES_H
#define LLVM_ANALYSIS_PROTEANCOLLECTFEATURES_H

#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <unordered_map>
#include <vector>

const int IR2VEC_DIMENSION = 300;
const int IR2VEC_EMBEDDING_SIZE = IR2VEC_DIMENSION * sizeof(double);

namespace llvm {
class LPMUpdater;
class Loop;

class LoopCollectFeaturesPass : public PassInfoMixin<LoopCollectFeaturesPass> {
public:
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &LAR, LPMUpdater &U);
};

struct CollectFeaturesPass : PassInfoMixin<CollectFeaturesPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

class ProteanFIExtendedFeatures {
public:
  enum class NamedFeatureIndex : size_t {
    InitialSize,
    Blocks,
    Calls,
    IsLocal,
    IsLinkOnceODR,
    IsLinkOnce,
    Loops,
    MaxLoopDepth,
    MaxDomTreeLevel,
    PtrArgs,
    PtrCallee,
    CallReturnPtr,
    ConditionalBranch,
    CBwithArg,
    CallerHeight,
    CallUsage,
    IsRecursive,
    NumCallsiteInLoop,
    NumOfCallUsesInLoop,
    EntryBlockFreq,
    MaxCallsiteBlockFreq,
    NumNamedFeatures
  };

  enum class NamedFloatFeatureIndex : size_t {
    InstructionPerBlock,
    SuccessorPerBlock,
    AvgVecInstr,
    AvgNestedLoopLevel,
    InstrPerLoop,
    BlockWithMultipleSuccessorsPerLoop,
    NumNamedFloatFeatures
  };

  struct FunctionFeatures {
    static const size_t FeatureCount;

    std::array<uint64_t,
               static_cast<size_t>(NamedFeatureIndex::NumNamedFeatures)>
        NamedFeatures = {{0}};
    std::array<float, static_cast<size_t>(
                          NamedFloatFeatureIndex::NumNamedFloatFeatures)>
        NamedFloatFeatures = {{0}};
    std::vector<int32_t> InstructionHistogram;
    std::vector<int32_t> InstructionPairHistogram;

    void fillTensor(int32_t *Ptr) const;
    uint64_t &operator[](NamedFeatureIndex Pos) {
      return NamedFeatures[static_cast<size_t>(Pos)];
    }
    float &operator[](NamedFloatFeatureIndex Pos) {
      return NamedFloatFeatures[static_cast<size_t>(Pos)];
    }
  };

  ProteanFIExtendedFeatures() = default;

  // Collect a number of features from the function F
  static FunctionFeatures
  getFunctionFeatures(Function &F, DominatorTree &DomTree,
                      TargetTransformInfo &TTI, LoopInfo &LI,
                      FunctionAnalysisManager &FAM, bool ValidSize = false,
                      bool ValidLoop = false, bool ValidTree = false);

private:
  // Loop related features, will update FF
  static void updateLoopRelatedFeatures(Function &F, LoopInfo &LI,
                                        FunctionFeatures &FF);
  // Instruction and BasicBlock related features, will update FF
  static void updateInstBBRelatedFeatures(Function &F, FunctionFeatures &FF);

  // This function should mimic the behaviour of updating all features below at
  // once:
  //    getMaxCallsiteBlockFreq
  //    updateCallsiteRelatedFeatures
  //    updateInstBBRelatedFeatures
  static void updateBBLoopCallsiteBFFeatures(Function &F, FunctionFeatures &FF,
                                             LoopInfo &LI,
                                             FunctionAnalysisManager &FAM);
};

class ProteanCollectFeatures {
public:
  // A feature is related to one of the following scope
  enum class Scope {
    Module,
    Function,
    Loop,
    CallGraph,
    CallSite,
    NumOfScope,
  };

  // In the future as more features are added, features can be calculated
  // simultaneously.
  // Suppose feature A and B could be calculated in the same loop,
  // then it would make sense to calculate both the features at the same time
  // and save it in a cache system
  // (which could be implemented similarly like DumpFeatures.h/cpp).
  enum class GroupID {
    EdgeNodeCount,
    ModuleInfoCount,
    FunctionInfo,
    FPIRelated,
    HotColdCallSite,
    InlineCostFeatureGroup,
    ProteanFIExtendedFeatures,
    LoopInstFeatures,
    TripCountFeatures,
    IVRelatedFeatures,
    LoopSetSizeFeatures,
    InnerOuterFeatures,

    NumOfGroupID
  };

  // List of features we support to be calculated.
  // (1) For each feature there should be a corresponding scope on which it
  // depends
  //     on for calculating.
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

    // Begin: ModuleInfoCount
    FunctionCount,
    TotalBBCount,
    AverageBBPerFunction,
    TotalInstructionCount,
    TotalFunctionCalls,
    AverageCallsPerFunction,
    MedianCallsPerFunction,
    LoopCount,
    TotalEdgeCount,
    CriticalEdgeCount,
    GlobalVariableCount,
    AverageInstructionsPerFunction,
    AverageLoadInstructionsPerFunction,
    AverageStoreInstructionsPerFunction,
    // End: ModuleInfoCount

    // Begin: HotColdCallsite
    ColdCallSite,
    HotCallSite,
    // End: HotColdCallsite

    // Begin: ProteanFIExtendedFeatures
    ProteanFIExtendedFeaturesNamedFeatureBegin,
    ProteanFIExtendedFeaturesInitialSize,
    ProteanFIExtendedFeaturesBlocks,
    ProteanFIExtendedFeaturesCalls,
    ProteanFIExtendedFeaturesIsLocal,
    ProteanFIExtendedFeaturesIsLinkOnceODR,
    ProteanFIExtendedFeaturesIsLinkOnce,
    ProteanFIExtendedFeaturesLoops,
    ProteanFIExtendedFeaturesMaxLoopDepth,
    ProteanFIExtendedFeaturesMaxDomTreeLevel,
    ProteanFIExtendedFeaturesPtrArgs,
    ProteanFIExtendedFeaturesPtrCallee,
    ProteanFIExtendedFeaturesCallReturnPtr,
    ProteanFIExtendedFeaturesConditionalBranch,
    ProteanFIExtendedFeaturesCBwithArg,
    ProteanFIExtendedFeaturesCallerHeight,
    ProteanFIExtendedFeaturesCallUsage,
    ProteanFIExtendedFeaturesIsRecursive,
    ProteanFIExtendedFeaturesNumCallsiteInLoop,
    ProteanFIExtendedFeaturesNumOfCallUsesInLoop,
    ProteanFIExtendedFeaturesEntryBlockFreq,
    ProteanFIExtendedFeaturesMaxCallsiteBlockFreq,
    ProteanFIExtendedFeaturesNamedFeatureEnd,
    ProteanFIExtendedFeaturesFloatFeatureBegin,
    ProteanFIExtendedFeaturesInstructionPerBlock,
    ProteanFIExtendedFeaturesSuccessorPerBlock,
    ProteanFIExtendedFeaturesAvgVecInstr,
    ProteanFIExtendedFeaturesAvgNestedLoopLevel,
    ProteanFIExtendedFeaturesInstrPerLoop,
    ProteanFIExtendedFeaturesBlockWithMultipleSuccessorsPerLoop,
    ProteanFIExtendedFeaturesFloatFeatureEnd,
    // End: ProteanFIExtendedFeatures

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

    // Begin: TripCountFeatures
    TripCount,
    MaxTripCount,
    IsFixedTripCount,
    // End: TripcountFeatures

    LoopSize,

    // Begin: IVRelatedFeatures
    InitialIVValueInt,
    FinalIVValueInt,
    StepValueInt,
    // End: IVRelatedFeatures

    // Begin: LoopSetSizeFeatures
    NumPartitions,
    IndVarSetSize,
    AvgStoreSetSize,
    AvgNumInsts,
    // End: LoopSetSizeFeatures

    // Begin: LoopInstFeatures
    NumLoadInstPerLoopNest,
    NumStoreInstPerLoopNest,
    TotLoopNestInstCount,
    AvgNumLoadInstPerLoopNest,
    NumLoadInstPerLoop,
    NumStoreInstPerLoop,
    TotLoopInstCount,
    AvgNumLoadInstPerLoop,
    TotBlocksPerLoop,
    // End: LoopInstFeatures

    // Begin: InnerOuterFeatures
    IsInnerMostLoop,
    IsOuterMostLoop,
    // End: InnerOuterFeatures

    MaxLoopHeight,

    SCCSize,
    AverageComponentSize,
    NumOfFeatures
  };

  struct FunctionFeaturesCache {
    using FunctionSizeMap = DenseMap<const Function *, size_t>;
    using FunctionFloatMap = DenseMap<const Function *, float>;

    std::array<
        FunctionSizeMap,
        static_cast<size_t>(
            ProteanFIExtendedFeatures::NamedFeatureIndex::NumNamedFeatures)>
        NamedFeatures;
    std::array<FunctionFloatMap,
               static_cast<size_t>(
                   ProteanFIExtendedFeatures::NamedFloatFeatureIndex::
                       NumNamedFloatFeatures)>
        NamedFloatFeatures;

    FunctionSizeMap &
    operator[](ProteanFIExtendedFeatures::NamedFeatureIndex Pos) {
      return NamedFeatures[static_cast<size_t>(Pos)];
    }
    FunctionFloatMap &
    operator[](ProteanFIExtendedFeatures::NamedFloatFeatureIndex Pos) {
      return NamedFloatFeatures[static_cast<size_t>(Pos)];
    }
  };

  struct FunctionAnalysisMap {
    DenseMap<const Function *, const DominatorTree *> DomCache;
    DenseMap<const Function *, const LoopInfo *> LICache;
    DenseMap<const Function *, const TargetTransformInfo *> TTICache;
  };

  struct AnalysisManagers {
    FunctionAnalysisManager *FAM = nullptr;
    ModuleAnalysisManager *MAM = nullptr;
    LoopStandardAnalysisResults *AR = nullptr;
  };

  // ScopeInfo is a struct that contains the corresponding needed information to
  // calculate the corresponding feature.
  struct ScopeInfo {
    Function *F = nullptr;
    CallBase *CB = nullptr;
    BasicBlock *BB = nullptr;
    Module *M = nullptr;
    Loop *L = nullptr;
    // Can add Instructions or other types later.
  };

  struct InlineInfo {
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
    InlineInfo II;
  };

  using FeatureValueMap = std::unordered_map<FeatureIndex, std::string>;
  using FeatureInfoMap = std::unordered_map<FeatureIndex, FeatureInfo>;
  using FeaturesInfo = std::vector<FeatureInfo>;
  using Scopes = std::vector<Scope>;
  using GroupIDs = std::vector<GroupID>;
  typedef void (*CalculateFeatureFunction)(ProteanCollectFeatures &,
                                           const FeatureInfo &);

  // Constructors/Destructors
  ProteanCollectFeatures();
  ProteanCollectFeatures(FeatureInfo GlobalInfo);
  ~ProteanCollectFeatures();

  // Invalidation mechanisms
  static void invalidateCache(CallBase *CB);

  static void invalidateCache(const Function *F);

  static void clearCache();

  // Getters/setters for the cache system.
  static std::optional<size_t>
  getCachedSize(const Function *F,
                ProteanFIExtendedFeatures::NamedFeatureIndex idx);

  static std::optional<float>
  getCachedFloat(const Function *F,
                 ProteanFIExtendedFeatures::NamedFloatFeatureIndex idx);

  static void insertSizeCache(const Function *F,
                              ProteanFIExtendedFeatures::NamedFeatureIndex idx,
                              size_t val);

  static void
  insertFloatCache(const Function *F,
                   ProteanFIExtendedFeatures::NamedFloatFeatureIndex idx,
                   float val);

  static const DominatorTree *getDomCachedAnalysis(const Function *F);

  static const LoopInfo *getLICachedAnalysis(const Function *F);

  static const TargetTransformInfo *getTTICachedAnalysis(const Function *F);

  static void insertAnalysisCache(const Function *F, const DominatorTree *Tree);

  static void insertAnalysisCache(const Function *F, const LoopInfo *LI);

  static void insertAnalysisCache(const Function *F,
                                  const TargetTransformInfo *TTI);
  // Setters/getters
  void setFeatureValue(FeatureIndex Idx, std::string Val);

  void setFeatureInfo(FeatureIndex Idx, FeatureInfo Info);

  void setFeatureValueAndInfo(FeatureIndex Idx, FeatureInfo Info,
                              std::string Val);

  void setGlobalFeatureInfo(FeatureInfo &Info);

  std::string getFeature(FeatureIndex Idx) const;

  // Check if the feature is already calculated.
  bool containsFeature(FeatureIndex);
  bool containsFeature(GroupID);

  static std::string getFeatureName(FeatureIndex Idx);
  static GroupID getFeatureGroup(FeatureIndex Idx);
  static Scope getFeatureScope(FeatureIndex Idx);
  static std::set<FeatureIndex> getGroupFeatures(GroupID Group);
  static std::set<FeatureIndex> getScopeFeatures(Scope S);

  static std::vector<std::string> getAllFeatures();

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
  // Notice I've only cached the FunctionLevels.
  // But in the future this should be generalized for all features.
  // One way to do this is to define a map from FeatureIndex -> Mapping.
  // Inside this mapping, the key should be the Scope and a set of analysis it
  // depends on.

  static std::map<const Function *, unsigned> FunctionLevels;

  // Saved FeatureValues when we collect the features.
  FeatureValueMap FeatureToValue;
  // TODO: Check if FeatureToInfo is needed or else delete it.
  FeatureInfoMap FeatureToInfo;
  FeatureInfo GlobalFeatureInfo;
  static FunctionFeaturesCache FeatureCache;
  static FunctionAnalysisMap FunctionAnalysisCache;
};

ProteanCollectFeatures::FeatureIndex
operator+(ProteanCollectFeatures::FeatureIndex, int);
ProteanCollectFeatures::FeatureIndex
operator-(ProteanCollectFeatures::FeatureIndex, int);
ProteanCollectFeatures::FeatureIndex &
operator++(ProteanCollectFeatures::FeatureIndex &);
ProteanCollectFeatures::FeatureIndex
operator++(ProteanCollectFeatures::FeatureIndex &, int);

const std::map<ProteanFIExtendedFeatures::NamedFeatureIndex, std::string>
    ProteanNamedFeatureIndexToName = {
        {ProteanFIExtendedFeatures::NamedFeatureIndex::InitialSize,
         "InitialSize"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::Blocks, "Blocks"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::Calls, "Calls"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::IsLocal, "IsLocal"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::IsLinkOnceODR,
         "IsLinkOnceODR"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::IsLinkOnce,
         "IsLinkOnce"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::Loops, "Loops"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::MaxLoopDepth,
         "MaxLoopDepth"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::MaxDomTreeLevel,
         "MaxDomTreeLevel"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::PtrArgs, "PtrArgs"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::PtrCallee, "PtrCallee"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::CallReturnPtr,
         "CallReturnPtr"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::ConditionalBranch,
         "ConditionalBranch"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::CBwithArg, "CBwithArg"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::CallerHeight,
         "CallerHeight"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::CallUsage, "CallUsage"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::IsRecursive,
         "IsRecursive"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::NumCallsiteInLoop,
         "NumCallsiteInLoop"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::NumOfCallUsesInLoop,
         "NumOfCallUsesInLoop"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::EntryBlockFreq,
         "EntryBlockFreq"},
        {ProteanFIExtendedFeatures::NamedFeatureIndex::MaxCallsiteBlockFreq,
         "MaxCallsiteBlockFreq"}};

const std::map<ProteanFIExtendedFeatures::NamedFloatFeatureIndex, std::string>
    ProteanFloatFeatureIndexToName = {
        {ProteanFIExtendedFeatures::NamedFloatFeatureIndex::InstructionPerBlock,
         "InstructionPerBlock"},
        {ProteanFIExtendedFeatures::NamedFloatFeatureIndex::SuccessorPerBlock,
         "SuccessorPerBlock"},
        {ProteanFIExtendedFeatures::NamedFloatFeatureIndex::AvgVecInstr,
         "AvgVecInstr"},
        {ProteanFIExtendedFeatures::NamedFloatFeatureIndex::AvgNestedLoopLevel,
         "AvgNestedLoopLevel"},
        {ProteanFIExtendedFeatures::NamedFloatFeatureIndex::InstrPerLoop,
         "InstrPerLoop"},
        {ProteanFIExtendedFeatures::NamedFloatFeatureIndex::
             BlockWithMultipleSuccessorsPerLoop,
         "BlockWithMultipleSuccessorsPerLoop"}};

ProteanFIExtendedFeatures::NamedFeatureIndex &
operator++(ProteanFIExtendedFeatures::NamedFeatureIndex &n);

ProteanFIExtendedFeatures::NamedFeatureIndex
operator++(ProteanFIExtendedFeatures::NamedFeatureIndex &n, int);

ProteanFIExtendedFeatures::NamedFloatFeatureIndex &
operator++(ProteanFIExtendedFeatures::NamedFloatFeatureIndex &n);

ProteanFIExtendedFeatures::NamedFloatFeatureIndex
operator++(ProteanFIExtendedFeatures::NamedFloatFeatureIndex &n, int);
} // namespace llvm

#endif // LLVM_ANALYSIS_ProteanCollectFeatures_H
