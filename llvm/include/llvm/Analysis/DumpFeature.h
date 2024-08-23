//===- DumpFeature.h - Dump features for a function -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header file defines passes to dump features for functions in an scc.
//
//===----------------------------------------------------------------------===//

#if defined(ENABLE_ACPO)
#ifndef LLVM_ANALYSIS_DUMPFEATURE
#define LLVM_ANALYSIS_DUMPFEATURE

#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

#include <map>

// EnableFeatureDump - This boolean is set to true if '-enable-feature-dump' is
// used as command line option. And we dump function features.
extern bool EnableFeatureDump;

namespace llvm {

class DumpFeaturePass : public PassInfoMixin<DumpFeaturePass> {
public:
  PreservedAnalyses run(LazyCallGraph::SCC &C, CGSCCAnalysisManager &AM,
                        LazyCallGraph &CG, CGSCCUpdateResult &UR);

private:
  /// Get the caller height from cache or calculate from scratch
  /// for a specific function F
  int getCallHeight(LazyCallGraph::SCC &C, CGSCCAnalysisManager &AM,
                    LazyCallGraph &CG, Function *F);
};

class ACPOFIExtendedFeatures {
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
    BlockWithMultipleSuccecorsPerLoop,
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

  ACPOFIExtendedFeatures() = default;

  // Collect a number of features from the function F
  static FunctionFeatures getFunctionFeatures(
      Function &F, DominatorTree &DomTree, TargetTransformInfo &TTI,
      LoopInfo &LI, FunctionAnalysisManager *FAM = nullptr, bool ValidSize = false,
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
  static void
  updateBBLoopCallsiteBFFeatures(Function &F, FunctionFeatures &FF,
                                 LoopInfo &LI,
                                 FunctionAnalysisManager *FAM = nullptr);
};

const std::map<ACPOFIExtendedFeatures::NamedFeatureIndex, std::string>
    NamedFeatureIndexToName = {
        {ACPOFIExtendedFeatures::NamedFeatureIndex::InitialSize, "InitialSize"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::Blocks, "Blocks"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::Calls, "Calls"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::IsLocal, "IsLocal"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::IsLinkOnceODR,
         "IsLinkOnceODR"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::IsLinkOnce, "IsLinkOnce"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::Loops, "Loops"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::MaxLoopDepth,
         "MaxLoopDepth"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::MaxDomTreeLevel,
         "MaxDomTreeLevel"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::PtrArgs, "PtrArgs"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::PtrCallee, "PtrCallee"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::CallReturnPtr,
         "CallReturnPtr"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::ConditionalBranch,
         "ConditionalBranch"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::CBwithArg, "CBwithArg"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::CallerHeight,
         "CallerHeight"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::CallUsage, "CallUsage"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::IsRecursive, "IsRecursive"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::NumCallsiteInLoop,
         "NumCallsiteInLoop"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::NumOfCallUsesInLoop,
         "NumOfCallUsesInLoop"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::EntryBlockFreq,
         "EntryBlockFreq"},
        {ACPOFIExtendedFeatures::NamedFeatureIndex::MaxCallsiteBlockFreq,
         "MaxCallsiteBlockFreq"}};

const std::map<ACPOFIExtendedFeatures::NamedFloatFeatureIndex, std::string>
    FloatFeatureIndexToName = {
        {ACPOFIExtendedFeatures::NamedFloatFeatureIndex::InstructionPerBlock,
         "InstructionPerBlock"},
        {ACPOFIExtendedFeatures::NamedFloatFeatureIndex::SuccessorPerBlock,
         "SuccessorPerBlock"},
        {ACPOFIExtendedFeatures::NamedFloatFeatureIndex::AvgVecInstr,
         "AvgVecInstr"},
        {ACPOFIExtendedFeatures::NamedFloatFeatureIndex::AvgNestedLoopLevel,
         "AvgNestedLoopLevel"},
        {ACPOFIExtendedFeatures::NamedFloatFeatureIndex::InstrPerLoop,
         "InstrPerLoop"},
        {ACPOFIExtendedFeatures::NamedFloatFeatureIndex::
             BlockWithMultipleSuccecorsPerLoop,
         "BlockWithMultipleSuccecorsPerLoop"}};

ACPOFIExtendedFeatures::NamedFeatureIndex &
operator++(ACPOFIExtendedFeatures::NamedFeatureIndex &n);

ACPOFIExtendedFeatures::NamedFeatureIndex
operator++(ACPOFIExtendedFeatures::NamedFeatureIndex &n, int);

ACPOFIExtendedFeatures::NamedFloatFeatureIndex &
operator++(ACPOFIExtendedFeatures::NamedFloatFeatureIndex &n);

ACPOFIExtendedFeatures::NamedFloatFeatureIndex
operator++(ACPOFIExtendedFeatures::NamedFloatFeatureIndex &n, int);

} // namespace llvm

#endif
#endif // ENABLE_ACPO
