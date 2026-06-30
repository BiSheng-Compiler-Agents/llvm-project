//===- ProteanCollectFeatures.cpp - Class for Feature Collection ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2024. Huawei Technologies Co., Ltd. All rights reserved.
//
//===----------------------------------------------------------------------===//
//
// This file implements the ProteanCollectFeatures pass
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ProteanCollectFeatures.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/FunctionPropertiesAnalysis.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/InlineOrder.h"
#include "llvm/Analysis/ModelDataCollector.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/ReplayInlineAdvisor.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO.h"
#include <optional>
#define DEBUG_TYPE "proteanFC"

// In "llvm/lib/Analysis/ModelDataCollector.cpp"
extern llvm::cl::opt<std::string> ProteanModelFile;

namespace llvm {
static cl::opt<bool> FeatureDump("enable-protean-feature-dump",
                                 cl::init(false));

class ModelDataProteanCollector : public ModelDataCollector {
public:
  ModelDataProteanCollector(formatted_raw_ostream &OS, bool OnlyMandatory,
                            std::string OutputFileName)
      : ModelDataCollector(OS, OutputFileName), OnlyMandatory(OnlyMandatory) {}

  void collectFeatures(CallBase *CB, InlineAdvisor *IA,
                       FunctionAnalysisManager *FAM,
                       ModuleAnalysisManager *MAM) {
    bool MandatoryOnly = getOnlyMandatory();
    resetRegisteredFeatures();
    BasicBlock *GlobalBB = CB->getParent();
    Function *GlobalF = GlobalBB->getParent();
    Module *GlobalM = GlobalF->getParent();
    ProteanCollectFeatures::FeatureInfo GlobalFeatureInfo{
        ProteanCollectFeatures::FeatureIndex::NumOfFeatures,
        {FAM, MAM},
        {GlobalF, CB, GlobalBB, GlobalM, nullptr},
        {MandatoryOnly, IA}};
    ProteanCollectFeatures::FeatureInfo CallerInfo{
        ProteanCollectFeatures::FeatureIndex::NumOfFeatures,
        {FAM, MAM},
        {CB->getCaller(), CB, GlobalBB, GlobalM, nullptr},
        {MandatoryOnly, IA}};
    ProteanCollectFeatures::FeatureInfo CalleeInfo{
        ProteanCollectFeatures::FeatureIndex::NumOfFeatures,
        {FAM, MAM},
        {CB->getCalledFunction(), CB, GlobalBB, GlobalM, nullptr},
        {MandatoryOnly, IA}};

    registerFeature({ProteanCollectFeatures::Scope::Function}, CalleeInfo,
                    "callee");
    registerFeature({ProteanCollectFeatures::Scope::Function}, CallerInfo,
                    "caller");
    registerFeature({ProteanCollectFeatures::Scope::CallSite},
                    GlobalFeatureInfo);
    registerFeature({ProteanCollectFeatures::Scope::Module}, GlobalFeatureInfo);
    ModelDataCollector::proteanCollectFeatures();
  }

  void printRow(bool printHeader, Module &M, Function &F, CallBase &CB) {
    // Print the IR file names first
    std::string Out = "";
    if (printHeader)
      Out += "Module,Function,Callee,Caller,";
    else
      Out += M.getName().str() + "," + F.getName().str() + "," +
             CB.getCalledFunction()->getName().str() + "," +
             CB.getCaller()->getName().str() + ",";
    for (const auto &P : getIRFileNameMap()) {
      if (printHeader)
        Out += P.getKey();
      else
        Out += P.getValue();

      Out += ",";
    }

    for (unsigned I = 0, E = Features.size(); I != E; ++I) {
      // First value does not get a comma
      if (I)
        Out += ",";

      if (printHeader)
        Out += Features.at(I).first;
      else
        Out += Features.at(I).second;
    }

    Out += "\n";
    ModelDataCollector::setOutput(Out);
  }

  bool getOnlyMandatory() { return OnlyMandatory; }

private:
  bool OnlyMandatory = false;
};

static void
calculateFPIRelated(ProteanCollectFeatures &ACF,
                    const ProteanCollectFeatures::FeatureInfo &info);
static void
calculateCallerBlockFreq(ProteanCollectFeatures &ACF,
                         const ProteanCollectFeatures::FeatureInfo &info);
static void
calculateCallSiteHeight(ProteanCollectFeatures &ACF,
                        const ProteanCollectFeatures::FeatureInfo &info);
static void
calculateConstantParam(ProteanCollectFeatures &ACF,
                       const ProteanCollectFeatures::FeatureInfo &info);
static void
calculateCostEstimate(ProteanCollectFeatures &ACF,
                      const ProteanCollectFeatures::FeatureInfo &info);
static void
calculateEdgeNodeCount(ProteanCollectFeatures &ACF,
                       const ProteanCollectFeatures::FeatureInfo &info);
static void
calculateHotColdCallSite(ProteanCollectFeatures &ACF,
                         const ProteanCollectFeatures::FeatureInfo &info);
static void calculateLoopLevel(ProteanCollectFeatures &ACF,
                               const ProteanCollectFeatures::FeatureInfo &info);
static void
calculateMandatoryKind(ProteanCollectFeatures &ACF,
                       const ProteanCollectFeatures::FeatureInfo &info);
static void
calculateMandatoryOnly(ProteanCollectFeatures &ACF,
                       const ProteanCollectFeatures::FeatureInfo &info);
static void
calculateInlineCostFeatures(ProteanCollectFeatures &ACF,
                            const ProteanCollectFeatures::FeatureInfo &info);
static void calculateProteanFIExtendedFeaturesFeatures(
    ProteanCollectFeatures &ACF,
    const ProteanCollectFeatures::FeatureInfo &info);
static void
calculateIsIndirectCall(ProteanCollectFeatures &ACF,
                        const ProteanCollectFeatures::FeatureInfo &info);
static void
calculateIsInInnerLoop(ProteanCollectFeatures &ACF,
                       const ProteanCollectFeatures::FeatureInfo &info);
static void
calculateIsMustTailCall(ProteanCollectFeatures &ACF,
                        const ProteanCollectFeatures::FeatureInfo &info);
static void
calculateIsTailCall(ProteanCollectFeatures &ACF,
                    const ProteanCollectFeatures::FeatureInfo &info);
static void calculateOptCode(ProteanCollectFeatures &ACF,
                             const ProteanCollectFeatures::FeatureInfo &info);

// Register FeatureIdx -> Feature name
//          FeatureIdx -> Scope, Scope -> FeatureIdx
//          FeatureIdx -> Group, Group -> FeatureIdx
//          FeatureIdx -> Calculating function
#define REGISTER_NAME(INDEX_NAME, NAME)                                        \
  { ProteanCollectFeatures::FeatureIndex::INDEX_NAME, NAME }
const std::unordered_map<ProteanCollectFeatures::FeatureIndex, std::string>
    ProteanCollectFeatures::FeatureIndexToName{
        REGISTER_NAME(SROASavings, "sroa_savings"),
        REGISTER_NAME(SROALosses, "sroa_losses"),
        REGISTER_NAME(LoadElimination, "load_elimination"),
        REGISTER_NAME(CallPenalty, "call_penalty"),
        REGISTER_NAME(CallArgumentSetup, "call_argument_setup"),
        REGISTER_NAME(LoadRelativeIntrinsic, "load_relative_intrinsic"),
        REGISTER_NAME(LoweredCallArgSetup, "lowered_call_arg_setup"),
        REGISTER_NAME(IndirectCallPenalty, "indirect_call_penalty"),
        REGISTER_NAME(JumpTablePenalty, "jump_table_penalty"),
        REGISTER_NAME(CaseClusterPenalty, "case_cluster_penalty"),
        REGISTER_NAME(SwitchPenalty, "switch_penalty"),
        REGISTER_NAME(UnsimplifiedCommonInstructions,
                      "unsimplified_common_instructions"),
        REGISTER_NAME(NumLoops, "num_loops"),
        REGISTER_NAME(DeadBlocks, "dead_blocks"),
        REGISTER_NAME(SimplifiedInstructions, "simplified_instructions"),
        REGISTER_NAME(ConstantArgs, "constant_args"),
        REGISTER_NAME(ConstantOffsetPtrArgs, "constant_offset_ptr_args"),
        REGISTER_NAME(CallSiteCost, "callsite_cost"),
        REGISTER_NAME(ColdCcPenalty, "cold_cc_penalty"),
        REGISTER_NAME(LastCallToStaticBonus, "last_call_to_static_bonus"),
        REGISTER_NAME(IsMultipleBlocks, "is_multiple_blocks"),
        REGISTER_NAME(NestedInlines, "nested_inlines"),
        REGISTER_NAME(NestedInlineCostEstimate, "nested_inline_cost_estimate"),
        REGISTER_NAME(Threshold, "threshold"),
        REGISTER_NAME(BasicBlockCount, "basic_block_count"),
        REGISTER_NAME(BlocksReachedFromConditionalInstruction,
                      "conditionally_executed_blocks"),
        REGISTER_NAME(Uses, "users"),
        REGISTER_NAME(EdgeCount, "edge_count"),
        REGISTER_NAME(NodeCount, "node_count"),
        REGISTER_NAME(ColdCallSite, "cold_callsite"),
        REGISTER_NAME(HotCallSite, "hot_callsite"),
        REGISTER_NAME(ProteanFIExtendedFeaturesInitialSize, "InitialSize"),
        REGISTER_NAME(ProteanFIExtendedFeaturesBlocks, "Blocks"),
        REGISTER_NAME(ProteanFIExtendedFeaturesCalls, "Calls"),
        REGISTER_NAME(ProteanFIExtendedFeaturesIsLocal, "IsLocal"),
        REGISTER_NAME(ProteanFIExtendedFeaturesIsLinkOnceODR, "IsLinkOnceODR"),
        REGISTER_NAME(ProteanFIExtendedFeaturesIsLinkOnce, "IsLinkOnce"),
        REGISTER_NAME(ProteanFIExtendedFeaturesLoops, "Loops"),
        REGISTER_NAME(ProteanFIExtendedFeaturesMaxLoopDepth, "MaxLoopDepth"),
        REGISTER_NAME(ProteanFIExtendedFeaturesMaxDomTreeLevel,
                      "MaxDomTreeLevel"),
        REGISTER_NAME(ProteanFIExtendedFeaturesPtrArgs, "PtrArgs"),
        REGISTER_NAME(ProteanFIExtendedFeaturesPtrCallee, "PtrCallee"),
        REGISTER_NAME(ProteanFIExtendedFeaturesCallReturnPtr, "CallReturnPtr"),
        REGISTER_NAME(ProteanFIExtendedFeaturesConditionalBranch,
                      "ConditionalBranch"),
        REGISTER_NAME(ProteanFIExtendedFeaturesCBwithArg, "CBwithArg"),
        REGISTER_NAME(ProteanFIExtendedFeaturesCallerHeight, "CallerHeight"),
        REGISTER_NAME(ProteanFIExtendedFeaturesCallUsage, "CallUsage"),
        REGISTER_NAME(ProteanFIExtendedFeaturesIsRecursive, "IsRecursive"),
        REGISTER_NAME(ProteanFIExtendedFeaturesNumCallsiteInLoop,
                      "NumCallsiteInLoop"),
        REGISTER_NAME(ProteanFIExtendedFeaturesNumOfCallUsesInLoop,
                      "NumOfCallUsesInLoop"),
        REGISTER_NAME(ProteanFIExtendedFeaturesEntryBlockFreq,
                      "EntryBlockFreq"),
        REGISTER_NAME(ProteanFIExtendedFeaturesMaxCallsiteBlockFreq,
                      "MaxCallsiteBlockFreq"),
        REGISTER_NAME(ProteanFIExtendedFeaturesInstructionPerBlock,
                      "InstructionPerBlock"),
        REGISTER_NAME(ProteanFIExtendedFeaturesSuccessorPerBlock,
                      "SuccessorPerBlock"),
        REGISTER_NAME(ProteanFIExtendedFeaturesAvgVecInstr, "AvgVecInstr"),
        REGISTER_NAME(ProteanFIExtendedFeaturesAvgNestedLoopLevel,
                      "AvgNestedLoopLevel"),
        REGISTER_NAME(ProteanFIExtendedFeaturesInstrPerLoop, "InstrPerLoop"),
        REGISTER_NAME(
            ProteanFIExtendedFeaturesBlockWithMultipleSuccessorsPerLoop,
            "BlockWithMultipleSuccessorsPerLoop"),
        REGISTER_NAME(CallerBlockFreq, "block_freq"),
        REGISTER_NAME(CallSiteHeight, "callsite_height"),
        REGISTER_NAME(ConstantParam, "nr_ctant_params"),
        REGISTER_NAME(CostEstimate, "cost_estimate"),
        REGISTER_NAME(LoopLevel, "loop_level"),
        REGISTER_NAME(MandatoryKind, "mandatory_kind"),
        REGISTER_NAME(MandatoryOnly, "mandatory_only"),
        REGISTER_NAME(OptCode, "opt_code"),
        REGISTER_NAME(IsIndirectCall, "is_indirect"),
        REGISTER_NAME(IsInInnerLoop, "is_in_inner_loop"),
        REGISTER_NAME(IsMustTailCall, "is_must_tail"),
        REGISTER_NAME(IsTailCall, "is_tail"),
        REGISTER_NAME(NumOfFeatures, "num_features"),
    };
#undef REGISTER_NAME

#define REGISTER_SCOPE(INDEX_NAME, NAME)                                       \
  {                                                                            \
    ProteanCollectFeatures::FeatureIndex::INDEX_NAME,                          \
        ProteanCollectFeatures::Scope::NAME                                    \
  }
const std::unordered_map<ProteanCollectFeatures::FeatureIndex,
                         ProteanCollectFeatures::Scope>
    ProteanCollectFeatures::FeatureIndexToScope{
        REGISTER_SCOPE(SROASavings, CallSite),
        REGISTER_SCOPE(SROALosses, CallSite),
        REGISTER_SCOPE(LoadElimination, CallSite),
        REGISTER_SCOPE(CallPenalty, CallSite),
        REGISTER_SCOPE(CallArgumentSetup, CallSite),
        REGISTER_SCOPE(LoadRelativeIntrinsic, CallSite),
        REGISTER_SCOPE(LoweredCallArgSetup, CallSite),
        REGISTER_SCOPE(IndirectCallPenalty, CallSite),
        REGISTER_SCOPE(JumpTablePenalty, CallSite),
        REGISTER_SCOPE(CaseClusterPenalty, CallSite),
        REGISTER_SCOPE(SwitchPenalty, CallSite),
        REGISTER_SCOPE(UnsimplifiedCommonInstructions, CallSite),
        REGISTER_SCOPE(NumLoops, CallSite),
        REGISTER_SCOPE(DeadBlocks, CallSite),
        REGISTER_SCOPE(SimplifiedInstructions, CallSite),
        REGISTER_SCOPE(ConstantArgs, CallSite),
        REGISTER_SCOPE(ConstantOffsetPtrArgs, CallSite),
        REGISTER_SCOPE(CallSiteCost, CallSite),
        REGISTER_SCOPE(ColdCcPenalty, CallSite),
        REGISTER_SCOPE(LastCallToStaticBonus, CallSite),
        REGISTER_SCOPE(IsMultipleBlocks, CallSite),
        REGISTER_SCOPE(NestedInlines, CallSite),
        REGISTER_SCOPE(NestedInlineCostEstimate, CallSite),
        REGISTER_SCOPE(Threshold, CallSite),
        REGISTER_SCOPE(BasicBlockCount, Function),
        REGISTER_SCOPE(BlocksReachedFromConditionalInstruction, Function),
        REGISTER_SCOPE(Uses, Function),
        REGISTER_SCOPE(EdgeCount, Module),
        REGISTER_SCOPE(NodeCount, Module),
        REGISTER_SCOPE(ColdCallSite, CallSite),
        REGISTER_SCOPE(HotCallSite, CallSite),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesInitialSize, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesBlocks, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesCalls, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesIsLocal, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesIsLinkOnceODR, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesIsLinkOnce, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesLoops, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesMaxLoopDepth, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesMaxDomTreeLevel, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesPtrArgs, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesPtrCallee, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesCallReturnPtr, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesConditionalBranch, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesCBwithArg, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesCallerHeight, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesCallUsage, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesIsRecursive, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesNumCallsiteInLoop, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesNumOfCallUsesInLoop, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesEntryBlockFreq, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesMaxCallsiteBlockFreq, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesInstructionPerBlock, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesSuccessorPerBlock, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesAvgVecInstr, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesAvgNestedLoopLevel, Function),
        REGISTER_SCOPE(ProteanFIExtendedFeaturesInstrPerLoop, Function),
        REGISTER_SCOPE(
            ProteanFIExtendedFeaturesBlockWithMultipleSuccessorsPerLoop,
            Function),
        REGISTER_SCOPE(CallerBlockFreq, CallSite),
        REGISTER_SCOPE(CallSiteHeight, CallSite),
        REGISTER_SCOPE(ConstantParam, CallSite),
        REGISTER_SCOPE(CostEstimate, CallSite),
        REGISTER_SCOPE(LoopLevel, CallSite),
        REGISTER_SCOPE(MandatoryKind, CallSite),
        REGISTER_SCOPE(MandatoryOnly, CallSite),
        REGISTER_SCOPE(OptCode, CallSite),
        REGISTER_SCOPE(IsIndirectCall, CallSite),
        REGISTER_SCOPE(IsInInnerLoop, CallSite),
        REGISTER_SCOPE(IsMustTailCall, CallSite),
        REGISTER_SCOPE(IsTailCall, CallSite),
    };
#undef REGISTER_SCOPE

#define REGISTER_GROUP(INDEX_NAME, NAME)                                       \
  {                                                                            \
    ProteanCollectFeatures::FeatureIndex::INDEX_NAME,                          \
        ProteanCollectFeatures::GroupID::NAME                                  \
  }
const std::unordered_map<ProteanCollectFeatures::FeatureIndex,
                         ProteanCollectFeatures::GroupID>
    ProteanCollectFeatures::FeatureIndexToGroup{
        REGISTER_GROUP(SROASavings, InlineCostFeatureGroup),
        REGISTER_GROUP(SROALosses, InlineCostFeatureGroup),
        REGISTER_GROUP(LoadElimination, InlineCostFeatureGroup),
        REGISTER_GROUP(CallPenalty, InlineCostFeatureGroup),
        REGISTER_GROUP(CallArgumentSetup, InlineCostFeatureGroup),
        REGISTER_GROUP(LoadRelativeIntrinsic, InlineCostFeatureGroup),
        REGISTER_GROUP(LoweredCallArgSetup, InlineCostFeatureGroup),
        REGISTER_GROUP(IndirectCallPenalty, InlineCostFeatureGroup),
        REGISTER_GROUP(JumpTablePenalty, InlineCostFeatureGroup),
        REGISTER_GROUP(CaseClusterPenalty, InlineCostFeatureGroup),
        REGISTER_GROUP(SwitchPenalty, InlineCostFeatureGroup),
        REGISTER_GROUP(UnsimplifiedCommonInstructions, InlineCostFeatureGroup),
        REGISTER_GROUP(NumLoops, InlineCostFeatureGroup),
        REGISTER_GROUP(DeadBlocks, InlineCostFeatureGroup),
        REGISTER_GROUP(SimplifiedInstructions, InlineCostFeatureGroup),
        REGISTER_GROUP(ConstantArgs, InlineCostFeatureGroup),
        REGISTER_GROUP(ConstantOffsetPtrArgs, InlineCostFeatureGroup),
        REGISTER_GROUP(CallSiteCost, InlineCostFeatureGroup),
        REGISTER_GROUP(ColdCcPenalty, InlineCostFeatureGroup),
        REGISTER_GROUP(LastCallToStaticBonus, InlineCostFeatureGroup),
        REGISTER_GROUP(IsMultipleBlocks, InlineCostFeatureGroup),
        REGISTER_GROUP(NestedInlines, InlineCostFeatureGroup),
        REGISTER_GROUP(NestedInlineCostEstimate, InlineCostFeatureGroup),
        REGISTER_GROUP(Threshold, InlineCostFeatureGroup),
        REGISTER_GROUP(BasicBlockCount, FPIRelated),
        REGISTER_GROUP(BlocksReachedFromConditionalInstruction, FPIRelated),
        REGISTER_GROUP(Uses, FPIRelated),
        REGISTER_GROUP(EdgeCount, EdgeNodeCount),
        REGISTER_GROUP(NodeCount, EdgeNodeCount),
        REGISTER_GROUP(ColdCallSite, HotColdCallSite),
        REGISTER_GROUP(HotCallSite, HotColdCallSite),
        REGISTER_GROUP(ProteanFIExtendedFeaturesInitialSize,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesBlocks,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesCalls,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesIsLocal,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesIsLinkOnceODR,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesIsLinkOnce,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesLoops,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesMaxLoopDepth,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesMaxDomTreeLevel,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesPtrArgs,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesPtrCallee,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesCallReturnPtr,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesConditionalBranch,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesCBwithArg,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesCallerHeight,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesCallUsage,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesIsRecursive,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesNumCallsiteInLoop,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesNumOfCallUsesInLoop,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesEntryBlockFreq,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesMaxCallsiteBlockFreq,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesInstructionPerBlock,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesSuccessorPerBlock,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesAvgVecInstr,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesAvgNestedLoopLevel,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(ProteanFIExtendedFeaturesInstrPerLoop,
                       ProteanFIExtendedFeatures),
        REGISTER_GROUP(
            ProteanFIExtendedFeaturesBlockWithMultipleSuccessorsPerLoop,
            ProteanFIExtendedFeatures),
    };
#undef REGISTER_GROUP

// Given a map that may not be one to one. Returns the inverse mapping.
// EX: Input:  A -> 1, B -> 1
//     Output: 1 -> A, 1 -> B
template <class K, class V>
static std::multimap<K, V> inverseMap(std::unordered_map<V, K> Map) {
  std::multimap<K, V> InverseMap;
  for (const auto &It : Map) {
    InverseMap.insert(std::pair<K, V>(It.second, It.first));
  }
  return InverseMap;
}

const std::multimap<ProteanCollectFeatures::GroupID,
                    ProteanCollectFeatures::FeatureIndex>
    ProteanCollectFeatures::GroupToFeatureIndices{
        inverseMap<ProteanCollectFeatures::GroupID,
                   ProteanCollectFeatures::FeatureIndex>(FeatureIndexToGroup)};

const std::multimap<ProteanCollectFeatures::Scope,
                    ProteanCollectFeatures::FeatureIndex>
    ProteanCollectFeatures::ScopeToFeatureIndices{
        inverseMap<ProteanCollectFeatures::Scope,
                   ProteanCollectFeatures::FeatureIndex>(FeatureIndexToScope)};

#define REGISTER_FUNCTION(INDEX_NAME, NAME)                                    \
  { ProteanCollectFeatures::FeatureIndex::INDEX_NAME, NAME }
const std::unordered_map<ProteanCollectFeatures::FeatureIndex,
                         ProteanCollectFeatures::CalculateFeatureFunction>
    ProteanCollectFeatures::CalculateFeatureMap{
        REGISTER_FUNCTION(SROASavings, calculateInlineCostFeatures),
        REGISTER_FUNCTION(SROALosses, calculateInlineCostFeatures),
        REGISTER_FUNCTION(LoadElimination, calculateInlineCostFeatures),
        REGISTER_FUNCTION(CallPenalty, calculateInlineCostFeatures),
        REGISTER_FUNCTION(CallArgumentSetup, calculateInlineCostFeatures),
        REGISTER_FUNCTION(LoadRelativeIntrinsic, calculateInlineCostFeatures),
        REGISTER_FUNCTION(LoweredCallArgSetup, calculateInlineCostFeatures),
        REGISTER_FUNCTION(IndirectCallPenalty, calculateInlineCostFeatures),
        REGISTER_FUNCTION(JumpTablePenalty, calculateInlineCostFeatures),
        REGISTER_FUNCTION(CaseClusterPenalty, calculateInlineCostFeatures),
        REGISTER_FUNCTION(SwitchPenalty, calculateInlineCostFeatures),
        REGISTER_FUNCTION(UnsimplifiedCommonInstructions,
                          calculateInlineCostFeatures),
        REGISTER_FUNCTION(NumLoops, calculateInlineCostFeatures),
        REGISTER_FUNCTION(DeadBlocks, calculateInlineCostFeatures),
        REGISTER_FUNCTION(SimplifiedInstructions, calculateInlineCostFeatures),
        REGISTER_FUNCTION(ConstantArgs, calculateInlineCostFeatures),
        REGISTER_FUNCTION(ConstantOffsetPtrArgs, calculateInlineCostFeatures),
        REGISTER_FUNCTION(CallSiteCost, calculateInlineCostFeatures),
        REGISTER_FUNCTION(ColdCcPenalty, calculateInlineCostFeatures),
        REGISTER_FUNCTION(LastCallToStaticBonus, calculateInlineCostFeatures),
        REGISTER_FUNCTION(IsMultipleBlocks, calculateInlineCostFeatures),
        REGISTER_FUNCTION(NestedInlines, calculateInlineCostFeatures),
        REGISTER_FUNCTION(NestedInlineCostEstimate,
                          calculateInlineCostFeatures),
        REGISTER_FUNCTION(Threshold, calculateInlineCostFeatures),
        REGISTER_FUNCTION(BasicBlockCount, calculateFPIRelated),
        REGISTER_FUNCTION(BlocksReachedFromConditionalInstruction,
                          calculateFPIRelated),
        REGISTER_FUNCTION(Uses, calculateFPIRelated),
        REGISTER_FUNCTION(EdgeCount, calculateEdgeNodeCount),
        REGISTER_FUNCTION(NodeCount, calculateEdgeNodeCount),
        REGISTER_FUNCTION(ColdCallSite, calculateHotColdCallSite),
        REGISTER_FUNCTION(HotCallSite, calculateHotColdCallSite),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesInitialSize,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesBlocks,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesCalls,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesIsLocal,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesIsLinkOnceODR,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesIsLinkOnce,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesLoops,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesMaxLoopDepth,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesMaxDomTreeLevel,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesPtrArgs,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesPtrCallee,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesCallReturnPtr,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesConditionalBranch,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesCBwithArg,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesCallerHeight,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesCallUsage,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesIsRecursive,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesNumCallsiteInLoop,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesNumOfCallUsesInLoop,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesEntryBlockFreq,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesMaxCallsiteBlockFreq,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesInstructionPerBlock,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesSuccessorPerBlock,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesAvgVecInstr,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesAvgNestedLoopLevel,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ProteanFIExtendedFeaturesInstrPerLoop,
                          calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(
            ProteanFIExtendedFeaturesBlockWithMultipleSuccessorsPerLoop,
            calculateProteanFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(CallerBlockFreq, calculateCallerBlockFreq),
        REGISTER_FUNCTION(CallSiteHeight, calculateCallSiteHeight),
        REGISTER_FUNCTION(ConstantParam, calculateConstantParam),
        REGISTER_FUNCTION(CostEstimate, calculateCostEstimate),
        REGISTER_FUNCTION(LoopLevel, calculateLoopLevel),
        REGISTER_FUNCTION(MandatoryKind, calculateMandatoryKind),
        REGISTER_FUNCTION(MandatoryOnly, calculateMandatoryOnly),
        REGISTER_FUNCTION(OptCode, calculateOptCode),
        REGISTER_FUNCTION(IsIndirectCall, calculateIsIndirectCall),
        REGISTER_FUNCTION(IsInInnerLoop, calculateIsInInnerLoop),
        REGISTER_FUNCTION(IsMustTailCall, calculateIsMustTailCall),
        REGISTER_FUNCTION(IsTailCall, calculateIsTailCall),
    };
#undef REGISTER_FUNCTION

std::map<const Function *, unsigned> ProteanCollectFeatures::FunctionLevels{};

ProteanCollectFeatures::ProteanCollectFeatures() {}

ProteanCollectFeatures::ProteanCollectFeatures(
    ProteanCollectFeatures::FeatureInfo GlobalInfo)
    : GlobalFeatureInfo(GlobalInfo) {
  assert(GlobalFeatureInfo.Idx == FeatureIndex::NumOfFeatures &&
         "When setting global FeatureInfo the Idx should always be "
         "NumOfFeatures");
}

ProteanCollectFeatures::~ProteanCollectFeatures() {}

void ProteanCollectFeatures::setFeatureValue(
    ProteanCollectFeatures::FeatureIndex Idx, std::string Val) {
  FeatureToValue[Idx] = Val;
}

void ProteanCollectFeatures::setFeatureInfo(
    ProteanCollectFeatures::FeatureIndex Idx,
    ProteanCollectFeatures::FeatureInfo Info) {
  assert(
      (Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
       Info.Idx == Idx || getFeatureGroup(Info.Idx) == getFeatureGroup(Idx)) &&
      "When setting FeatureToInfo map the key and value pair should both refer "
      "to the same Feature or the FeatureInfo.Idx should be NumOfFeatures.");
  FeatureToInfo[Idx] = Info;
}

void ProteanCollectFeatures::setFeatureValueAndInfo(
    ProteanCollectFeatures::FeatureIndex Idx,
    ProteanCollectFeatures::FeatureInfo Info, std::string Val) {
  setFeatureValue(Idx, Val);
  setFeatureInfo(Idx, Info);
}

void ProteanCollectFeatures::setGlobalFeatureInfo(
    ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == FeatureIndex::NumOfFeatures &&
         "When setting global FeatureInfo the Idx should always be "
         "NumOfFeatures");
  GlobalFeatureInfo = Info;
}

std::string ProteanCollectFeatures::getFeature(
    ProteanCollectFeatures::FeatureIndex Idx) const {
  assert(registeredFeature(Idx) && "Feature not registered");
  return FeatureToValue.find(Idx)->second;
}

std::string ProteanCollectFeatures::getFeatureName(
    ProteanCollectFeatures::FeatureIndex Idx) {
  return FeatureIndexToName.find(Idx)->second;
}

ProteanCollectFeatures::GroupID ProteanCollectFeatures::getFeatureGroup(
    ProteanCollectFeatures::FeatureIndex Idx) {
  return FeatureIndexToGroup.find(Idx)->second;
}

ProteanCollectFeatures::Scope ProteanCollectFeatures::getFeatureScope(
    ProteanCollectFeatures::FeatureIndex Idx) {
  return FeatureIndexToScope.find(Idx)->second;
}

std::set<ProteanCollectFeatures::FeatureIndex>
ProteanCollectFeatures::getGroupFeatures(
    ProteanCollectFeatures::GroupID Group) {
  std::set<ProteanCollectFeatures::FeatureIndex> FeatureIndices;
  auto Range = GroupToFeatureIndices.equal_range(Group);
  for (auto It = Range.first; It != Range.second; ++It) {
    FeatureIndices.insert(It->second);
  }
  return FeatureIndices;
}

std::set<ProteanCollectFeatures::FeatureIndex>
ProteanCollectFeatures::getScopeFeatures(ProteanCollectFeatures::Scope S) {
  std::set<ProteanCollectFeatures::FeatureIndex> FeatureIndices;
  auto Range = ScopeToFeatureIndices.equal_range(S);
  for (auto It = Range.first; It != Range.second; ++It) {
    FeatureIndices.insert(It->second);
  }
  return FeatureIndices;
}

bool ProteanCollectFeatures::containsFeature(
    ProteanCollectFeatures::FeatureIndex Idx) {
  return FeatureToValue.count(Idx) > 0;
}

bool ProteanCollectFeatures::containsFeature(
    ProteanCollectFeatures::GroupID GroupID) {
  for (auto FeatureIdx : getGroupFeatures(GroupID)) {
    if (!containsFeature(FeatureIdx))
      return false;
  }
  return true;
}

void ProteanCollectFeatures::clearFeatureValueMap() { FeatureToValue.clear(); }

bool ProteanCollectFeatures::registeredFeature(
    ProteanCollectFeatures::FeatureIndex Idx) const {
  return FeatureToValue.find(Idx) != FeatureToValue.end();
}

void calculateFPIRelated(ProteanCollectFeatures &ACF,
                         const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ProteanCollectFeatures::FeatureIndex::BasicBlockCount);

  auto *FAM = Info.Managers.FAM;
  auto *F = Info.SI.F;

  assert(F && FAM && "Function or FAM is nullptr");

  auto &FPI = FAM->getResult<FunctionPropertiesAnalysis>(*F);

  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::BasicBlockCount, Info,
      std::to_string(FPI.BasicBlockCount));
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::
          BlocksReachedFromConditionalInstruction,
      Info, std::to_string(FPI.BlocksReachedFromConditionalInstruction));
  ACF.setFeatureValueAndInfo(ProteanCollectFeatures::FeatureIndex::Uses, Info,
                             std::to_string(FPI.Uses));
}

void calculateCallerBlockFreq(ProteanCollectFeatures &ACF,
                              const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ProteanCollectFeatures::FeatureIndex::CallerBlockFreq);

  auto *CB = Info.SI.CB;
  auto *FAM = Info.Managers.FAM;

  assert(CB && FAM && "CallSite or FAM is nullptr");

  Function *F = CB->getCaller();
  BasicBlock *BB = CB->getParent();
  BlockFrequencyInfo &BFI = FAM->getResult<BlockFrequencyAnalysis>(*F);

  uint64_t CallerBlockFreq = BFI.getBlockFreq(BB).getFrequency();
  // The model uses signed 64-bit thus we need to take care of int overflow.
  if (CallerBlockFreq >= std::numeric_limits<int64_t>::max()) {
    CallerBlockFreq = std::numeric_limits<int64_t>::max() - 1;
  }

  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::CallerBlockFreq, Info,
      std::to_string(CallerBlockFreq));
}

static CallBase *getInlinableCS(Instruction &I) {
  if (auto *CS = dyn_cast<CallBase>(&I))
    if (Function *Callee = CS->getCalledFunction()) {
      if (!Callee->isDeclaration()) {
        return CS;
      }
    }
  return nullptr;
}

void calculateCallSiteHeight(ProteanCollectFeatures &ACF,
                             const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ProteanCollectFeatures::FeatureIndex::CallSiteHeight);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::FeatureIndex::CallSiteHeight))
    return;

  auto *CB = Info.SI.CB;
  auto *IA = Info.OI.IA;

  assert(CB && IA && "CallSite or IA is nullptr");

  if (IA) {
    ACF.setFeatureValueAndInfo(
        ProteanCollectFeatures::FeatureIndex::CallSiteHeight, Info,
        std::to_string(IA->getCallSiteHeight(CB)));
    return;
  }
  LLVM_DEBUG(dbgs() << "IA was nullptr & callsite height is not set!\n");
}

void calculateConstantParam(ProteanCollectFeatures &ACF,
                            const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ProteanCollectFeatures::FeatureIndex::ConstantParam);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::FeatureIndex::ConstantParam))
    return;

  auto *CB = Info.SI.CB;
  assert(CB && "CallSite is nullptr");

  size_t NrCtantParams = 0;
  for (auto I = CB->arg_begin(), E = CB->arg_end(); I != E; ++I) {
    NrCtantParams += (isa<Constant>(*I));
  }

  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::ConstantParam, Info,
      std::to_string(NrCtantParams));
}

void calculateCostEstimate(ProteanCollectFeatures &ACF,
                           const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ProteanCollectFeatures::FeatureIndex::CostEstimate);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::FeatureIndex::CostEstimate))
    return;

  auto *CB = Info.SI.CB;
  auto *FAM = Info.Managers.FAM;

  assert(CB && FAM && "CallBase or FAM is nullptr");

  auto &Callee = *CB->getCalledFunction();
  auto &TIR = FAM->getResult<TargetIRAnalysis>(Callee);

  auto GetAssumptionCache = [&](Function &F) -> AssumptionCache & {
    return FAM->getResult<AssumptionAnalysis>(F);
  };

  int CostEstimate = 0;
  auto IsCallSiteInlinable =
      llvm::getInliningCostEstimate(*CB, TIR, GetAssumptionCache);
  if (IsCallSiteInlinable)
    CostEstimate = *IsCallSiteInlinable;

  ACF.setFeatureValueAndInfo(ProteanCollectFeatures::FeatureIndex::CostEstimate,
                             Info, std::to_string(CostEstimate));
}

static int64_t getLocalCalls(Function &F, FunctionAnalysisManager &FAM) {
  return FAM.getResult<FunctionPropertiesAnalysis>(F)
      .DirectCallsToDefinedFunctions;
}

void calculateEdgeNodeCount(ProteanCollectFeatures &ACF,
                            const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         ProteanCollectFeatures::getFeatureGroup(Info.Idx) ==
             ProteanCollectFeatures::GroupID::EdgeNodeCount);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::GroupID::EdgeNodeCount))
    return;

  auto *M = Info.SI.M;
  auto *FAM = Info.Managers.FAM;

  assert(M && FAM && "Module or FAM is nullptr");

  int NodeCount = 0;
  int EdgeCount = 0;
  for (auto &F : *M)
    if (!F.isDeclaration()) {
      ++NodeCount;
      EdgeCount += getLocalCalls(F, *FAM);
    }

  std::string EdgeCountStr = std::to_string(EdgeCount);
  std::string NodeCountStr = std::to_string(NodeCount);
  ACF.setFeatureValueAndInfo(ProteanCollectFeatures::FeatureIndex::EdgeCount,
                             Info, EdgeCountStr);
  ACF.setFeatureValueAndInfo(ProteanCollectFeatures::FeatureIndex::NodeCount,
                             Info, NodeCountStr);
}

void calculateHotColdCallSite(ProteanCollectFeatures &ACF,
                              const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         ProteanCollectFeatures::getFeatureGroup(Info.Idx) ==
             ProteanCollectFeatures::GroupID::HotColdCallSite);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::GroupID::HotColdCallSite))
    return;

  auto *CB = Info.SI.CB;
  auto *FAM = Info.Managers.FAM;

  assert(CB && FAM && "Module or FAM is nullptr");

  auto &Caller = *CB->getCaller();
  auto GetBFI = [&](Function &F) -> BlockFrequencyInfo & {
    return FAM->getResult<BlockFrequencyAnalysis>(F);
  };

  BlockFrequencyInfo &CallerBFI = GetBFI(Caller);
  const BranchProbability ColdProb(2, 100);
  auto *CallSiteBB = CB->getParent();
  auto CallSiteFreq = CallerBFI.getBlockFreq(CallSiteBB);
  auto CallerEntryFreq =
      CallerBFI.getBlockFreq(&(CB->getCaller()->getEntryBlock()));
  bool ColdCallSite = CallSiteFreq < CallerEntryFreq * ColdProb;
  auto CallerEntryFreqHot = CallerBFI.getEntryFreq().getFrequency();
  bool HotCallSite = (CallSiteFreq.getFrequency() >= CallerEntryFreqHot * 60);

  ACF.setFeatureValueAndInfo(ProteanCollectFeatures::FeatureIndex::ColdCallSite,
                             Info, std::to_string(ColdCallSite));
  ACF.setFeatureValueAndInfo(ProteanCollectFeatures::FeatureIndex::HotCallSite,
                             Info, std::to_string(HotCallSite));
}

void calculateLoopLevel(ProteanCollectFeatures &ACF,
                        const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ProteanCollectFeatures::FeatureIndex::LoopLevel);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::FeatureIndex::LoopLevel))
    return;

  auto *CB = Info.SI.CB;
  auto *FAM = Info.Managers.FAM;

  assert(CB && FAM && "CallBase or FAM is nullptr");

  Function *F = CB->getCaller();
  BasicBlock *BB = CB->getParent();
  LoopInfo &LI = FAM->getResult<LoopAnalysis>(*F);

  std::string OptCode = std::to_string(CB->getOpcode());
  ACF.setFeatureValueAndInfo(ProteanCollectFeatures::FeatureIndex::LoopLevel,
                             Info, std::to_string(LI.getLoopDepth(BB)));
}

InlineAdvisor::MandatoryInliningKind
ProteanCollectFeatures::getMandatoryKind(CallBase &CB,
                                         FunctionAnalysisManager &FAM,
                                         OptimizationRemarkEmitter &ORE) {
  return InlineAdvisor::getMandatoryKind(CB, FAM, ORE);
}

void calculateMandatoryKind(ProteanCollectFeatures &ACF,
                            const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ProteanCollectFeatures::FeatureIndex::MandatoryKind);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::FeatureIndex::MandatoryKind))
    return;

  auto *CB = Info.SI.CB;
  auto *FAM = Info.Managers.FAM;

  assert(CB && FAM && "CallBase or FAM is nullptr");

  auto &Caller = *CB->getCaller();
  auto &ORE = FAM->getResult<OptimizationRemarkEmitterAnalysis>(Caller);
  auto MandatoryKind = ProteanCollectFeatures::getMandatoryKind(*CB, *FAM, ORE);

  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::MandatoryKind, Info,
      std::to_string((int)MandatoryKind));
}

void calculateMandatoryOnly(ProteanCollectFeatures &ACF,
                            const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ProteanCollectFeatures::FeatureIndex::MandatoryOnly);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::FeatureIndex::MandatoryOnly))
    return;

  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::MandatoryOnly, Info,
      std::to_string((int)Info.OI.MandatoryOnly));
}

void calculateOptCode(ProteanCollectFeatures &ACF,
                      const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ProteanCollectFeatures::FeatureIndex::OptCode);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::FeatureIndex::OptCode))
    return;

  auto *CB = Info.SI.CB;

  assert(CB && "CallBase is nullptr");

  std::string OptCode = std::to_string(CB->getOpcode());
  ACF.setFeatureValueAndInfo(ProteanCollectFeatures::FeatureIndex::OptCode,
                             Info, OptCode);
}

void calculateInlineCostFeatures(
    ProteanCollectFeatures &ACF,
    const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         (ProteanCollectFeatures::getFeatureGroup(Info.Idx) ==
          ProteanCollectFeatures::GroupID::InlineCostFeatureGroup));

  // Check if we already calculated the values.
  if (ACF.containsFeature(
          ProteanCollectFeatures::GroupID::InlineCostFeatureGroup))
    return;

  auto *CB = Info.SI.CB;
  auto *FAM = Info.Managers.FAM;

  assert(CB && FAM && "CallBase or FAM is nullptr");

  auto &Callee = *CB->getCalledFunction();
  auto &TIR = FAM->getResult<TargetIRAnalysis>(Callee);

  auto GetAssumptionCache = [&](Function &F) -> AssumptionCache & {
    return FAM->getResult<AssumptionAnalysis>(F);
  };

  const auto CostFeaturesOpt =
      getInliningCostFeatures(*CB, TIR, GetAssumptionCache);

  const auto ICFGroupBegin =
      ProteanCollectFeatures::FeatureIndex::InlineCostFeatureGroupBegin;
  const auto ICFGroupEnd =
      ProteanCollectFeatures::FeatureIndex::InlineCostFeatureGroupEnd;

  for (auto Idx = ICFGroupBegin + 1; Idx != ICFGroupEnd; ++Idx) {
    size_t TmpIdx =
        static_cast<size_t>(Idx) - static_cast<size_t>(ICFGroupBegin) - 1;
    ACF.setFeatureValueAndInfo(
        Idx, Info,
        std::to_string(CostFeaturesOpt ? CostFeaturesOpt.value()[TmpIdx] : 0));
  }
}

static void
checkValidFFCache(Function &F,
                  struct ProteanFIExtendedFeatures::FunctionFeatures &FF,
                  DominatorTree &Tree, TargetTransformInfo &TTI, LoopInfo &LI,
                  bool &ValidSize, bool &ValidLoop, bool &ValidTree) {
  std::optional<size_t> SizeCache = ProteanCollectFeatures::getCachedSize(
      &F, ProteanFIExtendedFeatures::NamedFeatureIndex::InitialSize);
  auto TTIAnalysisCache = ProteanCollectFeatures::getTTICachedAnalysis(&F);
  if (SizeCache && TTIAnalysisCache == &TTI) {
    ValidSize = true;
  }

  std::optional<size_t> MaxDomTreeLevelCache =
      ProteanCollectFeatures::getCachedSize(
          &F, ProteanFIExtendedFeatures::NamedFeatureIndex::MaxDomTreeLevel);
  auto DomCache = ProteanCollectFeatures::getDomCachedAnalysis(&F);
  if (MaxDomTreeLevelCache && DomCache == &Tree) {
    ValidTree = true;
  }

  std::optional<size_t> LoopNumCache = ProteanCollectFeatures::getCachedSize(
      &F, ProteanFIExtendedFeatures::NamedFeatureIndex::Loops);
  auto LIAnalysisCache = ProteanCollectFeatures::getLICachedAnalysis(&F);
  if (LoopNumCache && LIAnalysisCache == &LI) {
    ValidLoop = true;
  }
}

static void getCachedFF(Function &F,
                        struct ProteanFIExtendedFeatures::FunctionFeatures &FF,
                        DominatorTree &Tree, TargetTransformInfo &TTI,
                        LoopInfo &LI) {
  std::optional<size_t> SizeCache = ProteanCollectFeatures::getCachedSize(
      &F, ProteanFIExtendedFeatures::NamedFeatureIndex::InitialSize);
  auto TTIAnalysisCache = ProteanCollectFeatures::getTTICachedAnalysis(&F);
  if (SizeCache && TTIAnalysisCache == &TTI) {
    FF[ProteanFIExtendedFeatures::NamedFeatureIndex::InitialSize] =
        SizeCache.value();
  }

  std::optional<size_t> MaxDomTreeLevelCache =
      ProteanCollectFeatures::getCachedSize(
          &F, ProteanFIExtendedFeatures::NamedFeatureIndex::MaxDomTreeLevel);
  auto DomCache = ProteanCollectFeatures::getDomCachedAnalysis(&F);
  if (MaxDomTreeLevelCache && DomCache == &Tree) {
    FF[ProteanFIExtendedFeatures::NamedFeatureIndex::MaxDomTreeLevel] =
        MaxDomTreeLevelCache.value();
  }

  std::optional<size_t> LoopNumCache = ProteanCollectFeatures::getCachedSize(
      &F, ProteanFIExtendedFeatures::NamedFeatureIndex::Loops);
  auto LIAnalysisCache = ProteanCollectFeatures::getLICachedAnalysis(&F);
  if (LoopNumCache && LIAnalysisCache == &LI) {
    FF[ProteanFIExtendedFeatures::NamedFeatureIndex::Loops] =
        LoopNumCache.value();
    FF[ProteanFIExtendedFeatures::NamedFeatureIndex::MaxLoopDepth] =
        ProteanCollectFeatures::getCachedSize(
            &F, ProteanFIExtendedFeatures::NamedFeatureIndex::MaxLoopDepth)
            .value();
    if (LoopNumCache.value() != 0) {
      FF[ProteanFIExtendedFeatures::NamedFloatFeatureIndex::InstrPerLoop] =
          ProteanCollectFeatures::getCachedFloat(
              &F,
              ProteanFIExtendedFeatures::NamedFloatFeatureIndex::InstrPerLoop)
              .value();
      FF[ProteanFIExtendedFeatures::NamedFloatFeatureIndex::
             BlockWithMultipleSuccessorsPerLoop] =
          ProteanCollectFeatures::getCachedFloat(
              &F, ProteanFIExtendedFeatures::NamedFloatFeatureIndex::
                      BlockWithMultipleSuccessorsPerLoop)
              .value();
      FF[ProteanFIExtendedFeatures::NamedFloatFeatureIndex::
             AvgNestedLoopLevel] =
          ProteanCollectFeatures::getCachedFloat(
              &F, ProteanFIExtendedFeatures::NamedFloatFeatureIndex::
                      AvgNestedLoopLevel)
              .value();
    }
  }
}

static void
updateCachedFF(Function &F,
               struct ProteanFIExtendedFeatures::FunctionFeatures &FF,
               DominatorTree &Tree, TargetTransformInfo &TTI, LoopInfo &LI) {
  ProteanCollectFeatures::insertSizeCache(
      &F, ProteanFIExtendedFeatures::NamedFeatureIndex::InitialSize,
      FF[ProteanFIExtendedFeatures::NamedFeatureIndex::InitialSize]);
  ProteanCollectFeatures::insertAnalysisCache(&F, &TTI);
  ProteanCollectFeatures::insertSizeCache(
      &F, ProteanFIExtendedFeatures::NamedFeatureIndex::MaxDomTreeLevel,
      FF[ProteanFIExtendedFeatures::NamedFeatureIndex::MaxDomTreeLevel]);
  ProteanCollectFeatures::insertAnalysisCache(&F, &Tree);
  ProteanCollectFeatures::insertSizeCache(
      &F, ProteanFIExtendedFeatures::NamedFeatureIndex::Loops,
      FF[ProteanFIExtendedFeatures::NamedFeatureIndex::Loops]);
  ProteanCollectFeatures::insertSizeCache(
      &F, ProteanFIExtendedFeatures::NamedFeatureIndex::MaxLoopDepth,
      FF[ProteanFIExtendedFeatures::NamedFeatureIndex::MaxLoopDepth]);
  ProteanCollectFeatures::insertFloatCache(
      &F, ProteanFIExtendedFeatures::NamedFloatFeatureIndex::InstrPerLoop,
      FF[ProteanFIExtendedFeatures::NamedFloatFeatureIndex::InstrPerLoop]);
  ProteanCollectFeatures::insertFloatCache(
      &F,
      ProteanFIExtendedFeatures::NamedFloatFeatureIndex::
          BlockWithMultipleSuccessorsPerLoop,
      FF[ProteanFIExtendedFeatures::NamedFloatFeatureIndex::
             BlockWithMultipleSuccessorsPerLoop]);
  ProteanCollectFeatures::insertFloatCache(
      &F, ProteanFIExtendedFeatures::NamedFloatFeatureIndex::AvgNestedLoopLevel,
      FF[ProteanFIExtendedFeatures::NamedFloatFeatureIndex::
             AvgNestedLoopLevel]);
  ProteanCollectFeatures::insertAnalysisCache(&F, &LI);
}

void calculateProteanFIExtendedFeaturesFeatures(
    ProteanCollectFeatures &ACF,
    const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         ProteanCollectFeatures::getFeatureGroup(Info.Idx) ==
             ProteanCollectFeatures::GroupID::ProteanFIExtendedFeatures);

  // Check if we already calculated the values.
  if (ACF.containsFeature(
          ProteanCollectFeatures::GroupID::ProteanFIExtendedFeatures))
    return;

  auto F = Info.SI.F;
  auto *FAM = Info.Managers.FAM;

  assert(F && FAM && "F or FAM is nullptr");

  struct ProteanFIExtendedFeatures::FunctionFeatures FF;
  auto &DomTree = FAM->getResult<DominatorTreeAnalysis>(*F);
  auto &TTI = FAM->getResult<TargetIRAnalysis>(*F);
  auto &LI = FAM->getResult<LoopAnalysis>(*F);
  bool ValidSize = false;
  bool ValidLoop = false;
  bool ValidTree = false;
  checkValidFFCache(*F, FF, DomTree, TTI, LI, ValidSize, ValidLoop, ValidTree);
  FF = ProteanFIExtendedFeatures::getFunctionFeatures(
      *F, DomTree, TTI, LI, *FAM, ValidSize, ValidLoop, ValidTree);
  getCachedFF(*F, FF, DomTree, TTI, LI);
  updateCachedFF(*F, FF, DomTree, TTI, LI);
  const auto ProteanNamedFeaturesBegin = ProteanCollectFeatures::FeatureIndex::
      ProteanFIExtendedFeaturesNamedFeatureBegin;
  const auto ProteanNamedFeaturesEnd = ProteanCollectFeatures::FeatureIndex::
      ProteanFIExtendedFeaturesNamedFeatureEnd;
  const auto ProteanFloatFeaturesBegin = ProteanCollectFeatures::FeatureIndex::
      ProteanFIExtendedFeaturesFloatFeatureBegin;
  const auto ProteanFloatFeaturesEnd = ProteanCollectFeatures::FeatureIndex::
      ProteanFIExtendedFeaturesFloatFeatureEnd;

  for (auto Idx = ProteanNamedFeaturesBegin + 1; Idx != ProteanNamedFeaturesEnd;
       ++Idx) {
    size_t TmpIdx = static_cast<size_t>(Idx) -
                    static_cast<size_t>(ProteanNamedFeaturesBegin) - 1;
    ACF.setFeatureValueAndInfo(Idx, Info,
                               std::to_string(FF.NamedFeatures[TmpIdx]));
  }
  for (auto Idx = ProteanFloatFeaturesBegin + 1; Idx != ProteanFloatFeaturesEnd;
       ++Idx) {
    size_t TmpIdx = static_cast<size_t>(Idx) -
                    static_cast<size_t>(ProteanFloatFeaturesBegin) - 1;
    ACF.setFeatureValueAndInfo(Idx, Info,
                               std::to_string(FF.NamedFloatFeatures[TmpIdx]));
  }
}

void calculateIsIndirectCall(ProteanCollectFeatures &ACF,
                             const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ProteanCollectFeatures::FeatureIndex::IsIndirectCall);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::FeatureIndex::IsIndirectCall))
    return;

  auto *CB = Info.SI.CB;

  assert(CB && "CallBase is nullptr");

  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::IsIndirectCall, Info,
      std::to_string(CB->isIndirectCall()));
}

void calculateIsInInnerLoop(ProteanCollectFeatures &ACF,
                            const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ProteanCollectFeatures::FeatureIndex::IsInInnerLoop);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::FeatureIndex::IsInInnerLoop))
    return;

  auto *CB = Info.SI.CB;
  auto *FAM = Info.Managers.FAM;

  assert(CB && FAM && "CallBase or FAM is nullptr");

  auto &Caller = *CB->getCaller();
  auto &CallerLI = FAM->getResult<LoopAnalysis>(Caller);

  // Get loop for CB's BB. And check whether the loop is an inner most loop.
  bool CallSiteInInnerLoop = false;
  for (auto &L : CallerLI) {
    if (L->isInnermost() && L->contains(CB))
      CallSiteInInnerLoop = true;
  }

  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::IsInInnerLoop, Info,
      std::to_string(CallSiteInInnerLoop));
}

void calculateIsMustTailCall(ProteanCollectFeatures &ACF,
                             const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ProteanCollectFeatures::FeatureIndex::IsMustTailCall);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::FeatureIndex::IsMustTailCall))
    return;

  auto *CB = Info.SI.CB;

  assert(CB && "CallBase is nullptr");

  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::IsMustTailCall, Info,
      std::to_string(CB->isMustTailCall()));
}

void calculateIsTailCall(ProteanCollectFeatures &ACF,
                         const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ProteanCollectFeatures::FeatureIndex::IsTailCall);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::FeatureIndex::IsTailCall))
    return;

  auto *CB = Info.SI.CB;

  assert(CB && "CallBase is nullptr");

  ACF.setFeatureValueAndInfo(ProteanCollectFeatures::FeatureIndex::IsTailCall,
                             Info, std::to_string(CB->isTailCall()));
}

ProteanCollectFeatures::FeatureValueMap ProteanCollectFeatures::getFeaturesPair(
    ProteanCollectFeatures::FeaturesInfo FeatureInfoVec) {
  clearFeatureValueMap();
  for (auto &FeatureInfo : FeatureInfoVec) {
    auto It = CalculateFeatureMap.find(FeatureInfo.Idx);
    if (It == CalculateFeatureMap.end()) {
      assert("Could not find the corresponding function to calculate feature");
    }
    auto CalculateFunction = It->second;
    CalculateFunction(*this, FeatureInfo);
    LLVM_DEBUG(dbgs() << "Protean Feature " << getFeatureName(FeatureInfo.Idx)
                      << ": " << FeatureToValue[FeatureInfo.Idx] << "\n");
  }

  return FeatureToValue;
}

ProteanCollectFeatures::FeatureValueMap ProteanCollectFeatures::getFeaturesPair(
    ProteanCollectFeatures::Scopes ScopeVec) {
  clearFeatureValueMap();
  for (auto Scope : ScopeVec) {
    for (auto FeatureIdx : getScopeFeatures(Scope)) {
      auto It = CalculateFeatureMap.find(FeatureIdx);
      if (It == CalculateFeatureMap.end()) {
        assert(
            "Could not find the corresponding function to calculate feature");
      }
      auto CalculateFunction = It->second;
      CalculateFunction(*this, GlobalFeatureInfo);
      LLVM_DEBUG(dbgs() << "Protean Feature " << getFeatureName(FeatureIdx)
                        << ": " << FeatureToValue[FeatureIdx] << "\n");
    }
  }

  return FeatureToValue;
}

ProteanCollectFeatures::FeatureValueMap ProteanCollectFeatures::getFeaturesPair(
    ProteanCollectFeatures::GroupIDs GroupIDVec) {
  clearFeatureValueMap();
  for (auto GroupID : GroupIDVec) {
    for (auto FeatureIdx : getGroupFeatures(GroupID)) {
      auto It = CalculateFeatureMap.find(FeatureIdx);
      if (It == CalculateFeatureMap.end()) {
        assert(
            "Could not find the corresponding function to calculate feature");
      }
      auto CalculateFunction = It->second;
      CalculateFunction(*this, GlobalFeatureInfo);
      LLVM_DEBUG(dbgs() << "Protean Feature " << getFeatureName(FeatureIdx)
                        << ": " << FeatureToValue[FeatureIdx] << "\n");
    }
  }

  return FeatureToValue;
}

ProteanCollectFeatures::FeatureValueMap ProteanCollectFeatures::getFeaturesPair(
    ProteanCollectFeatures::FeatureIndex Beg,
    ProteanCollectFeatures::FeatureIndex End) {
  assert(Beg <= End);
  for (auto Idx = Beg; Idx != End; ++Idx) {
    auto It = CalculateFeatureMap.find(Idx);
    if (It == CalculateFeatureMap.end()) {
      assert("Could not find the corresponding function to calculate feature");
    }
    auto CalculateFunction = It->second;
    CalculateFunction(*this, GlobalFeatureInfo);
  }

  return FeatureToValue;
}

void ProteanCollectFeatures::clearFunctionLevel() { FunctionLevels.clear(); }

void ProteanCollectFeatures::insertFunctionLevel(const Function *F,
                                                 unsigned FL) {
  FunctionLevels[F] = FL;
}

std::optional<unsigned>
ProteanCollectFeatures::getFunctionLevel(const Function *F) {
  auto It = FunctionLevels.find(F);
  if (It == FunctionLevels.end()) {
    return std::nullopt;
  } else {
    return It->second;
  }
}

ProteanCollectFeatures::FeatureIndex
operator+(ProteanCollectFeatures::FeatureIndex N, int Counter) {
  return static_cast<ProteanCollectFeatures::FeatureIndex>((int)N + Counter);
}

ProteanCollectFeatures::FeatureIndex
operator-(ProteanCollectFeatures::FeatureIndex N, int Counter) {
  return static_cast<ProteanCollectFeatures::FeatureIndex>((int)N - Counter);
}

ProteanCollectFeatures::FeatureIndex &
operator++(ProteanCollectFeatures::FeatureIndex &N) {
  return N = static_cast<ProteanCollectFeatures::FeatureIndex>((int)N + 1);
}

ProteanCollectFeatures::FeatureIndex
operator++(ProteanCollectFeatures::FeatureIndex &N, int) {
  ProteanCollectFeatures::FeatureIndex Res = N;
  ++N;
  return Res;
}

void ProteanCollectFeatures::invalidateCache(CallBase *CB) {
  if (CB) {
    invalidateCache(CB->getCaller());
  }
}

void ProteanCollectFeatures::invalidateCache(const Function *F) {
  for (ProteanFIExtendedFeatures::NamedFeatureIndex feature =
           ProteanFIExtendedFeatures::NamedFeatureIndex(0);
       feature !=
       ProteanFIExtendedFeatures::NamedFeatureIndex::NumNamedFeatures;
       ++feature) {
    FeatureCache[feature].erase(F);
  }
  for (ProteanFIExtendedFeatures::NamedFloatFeatureIndex feature =
           ProteanFIExtendedFeatures::NamedFloatFeatureIndex(0);
       feature !=
       ProteanFIExtendedFeatures::NamedFloatFeatureIndex::NumNamedFloatFeatures;
       ++feature) {
    FeatureCache[feature].erase(F);
  }
  FunctionAnalysisCache.DomCache.erase(F);
  FunctionAnalysisCache.LICache.erase(F);
  FunctionAnalysisCache.TTICache.erase(F);
}
void ProteanCollectFeatures::clearCache() {
  for (ProteanFIExtendedFeatures::NamedFeatureIndex feature =
           ProteanFIExtendedFeatures::NamedFeatureIndex(0);
       feature !=
       ProteanFIExtendedFeatures::NamedFeatureIndex::NumNamedFeatures;
       ++feature) {
    FeatureCache[feature].clear();
  }
  for (ProteanFIExtendedFeatures::NamedFloatFeatureIndex feature =
           ProteanFIExtendedFeatures::NamedFloatFeatureIndex(0);
       feature !=
       ProteanFIExtendedFeatures::NamedFloatFeatureIndex::NumNamedFloatFeatures;
       ++feature) {
    FeatureCache[feature].clear();
  }
  FunctionAnalysisCache.DomCache.clear();
  FunctionAnalysisCache.LICache.clear();
  FunctionAnalysisCache.TTICache.clear();
}

std::optional<size_t> ProteanCollectFeatures::getCachedSize(
    const Function *F, ProteanFIExtendedFeatures::NamedFeatureIndex idx) {
  auto it = FeatureCache[idx].find(F);
  return it != FeatureCache[idx].end() ? std::optional<size_t>(it->second)
                                       : std::nullopt;
}

std::optional<float> ProteanCollectFeatures::getCachedFloat(
    const Function *F, ProteanFIExtendedFeatures::NamedFloatFeatureIndex idx) {
  auto it = FeatureCache[idx].find(F);
  return it != FeatureCache[idx].end() ? std::optional<float>(it->second)
                                       : std::nullopt;
}

void ProteanCollectFeatures::insertSizeCache(
    const Function *F, ProteanFIExtendedFeatures::NamedFeatureIndex idx,
    size_t val) {
  FeatureCache[idx].insert(std::make_pair(F, val));
}

void ProteanCollectFeatures::insertFloatCache(
    const Function *F, ProteanFIExtendedFeatures::NamedFloatFeatureIndex idx,
    float val) {
  FeatureCache[idx].insert(std::make_pair(F, val));
}

const DominatorTree *
ProteanCollectFeatures::getDomCachedAnalysis(const Function *F) {
  auto it = FunctionAnalysisCache.DomCache.find(F);
  return it != FunctionAnalysisCache.DomCache.end() ? it->second : nullptr;
}

const LoopInfo *ProteanCollectFeatures::getLICachedAnalysis(const Function *F) {
  auto it = FunctionAnalysisCache.LICache.find(F);
  return it != FunctionAnalysisCache.LICache.end() ? it->second : nullptr;
}

const TargetTransformInfo *
ProteanCollectFeatures::getTTICachedAnalysis(const Function *F) {
  auto it = FunctionAnalysisCache.TTICache.find(F);
  return it != FunctionAnalysisCache.TTICache.end() ? it->second : nullptr;
}

void ProteanCollectFeatures::insertAnalysisCache(const Function *F,
                                                 const DominatorTree *Tree) {
  FunctionAnalysisCache.DomCache.insert(std::make_pair(F, Tree));
}

void ProteanCollectFeatures::insertAnalysisCache(const Function *F,
                                                 const LoopInfo *LI) {
  FunctionAnalysisCache.LICache.insert(std::make_pair(F, LI));
}

void ProteanCollectFeatures::insertAnalysisCache(
    const Function *F, const TargetTransformInfo *TTI) {
  FunctionAnalysisCache.TTICache.insert(std::make_pair(F, TTI));
}

unsigned getMaxInstructionID() {
#define LAST_OTHER_INST(NR) return NR;
#include "llvm/IR/Instruction.def"
}

size_t getSize(Function &F, TargetTransformInfo &TTI) {
  size_t SumOfAllInstCost = 0;
  for (const auto &BB : F)
    for (const auto &I : BB) {
      std::optional<long int> cost =
          TTI.getInstructionCost(
                 &I, TargetTransformInfo::TargetCostKind::TCK_CodeSize)
              .getValue();
      if (cost.has_value())
        SumOfAllInstCost += cost.value();
    }
  return SumOfAllInstCost;
}

static const std::array<std::pair<size_t, size_t>, 137>
    ImportantInstructionSuccessions{
        {{1, 1},   {1, 4},   {1, 5},   {1, 7},   {1, 8},   {1, 9},   {1, 11},
         {1, 12},  {1, 13},  {1, 14},  {1, 18},  {1, 20},  {1, 22},  {1, 24},
         {1, 25},  {1, 26},  {1, 27},  {1, 28},  {1, 29},  {1, 30},  {1, 31},
         {1, 32},  {1, 33},  {1, 34},  {1, 39},  {1, 40},  {1, 42},  {1, 45},
         {2, 1},   {2, 2},   {2, 13},  {2, 28},  {2, 29},  {2, 32},  {2, 33},
         {2, 34},  {2, 38},  {2, 48},  {2, 49},  {2, 53},  {2, 55},  {2, 56},
         {13, 2},  {13, 13}, {13, 26}, {13, 33}, {13, 34}, {13, 56}, {15, 27},
         {28, 2},  {28, 48}, {28, 53}, {29, 2},  {29, 33}, {29, 56}, {31, 31},
         {31, 33}, {31, 34}, {31, 49}, {32, 1},  {32, 2},  {32, 13}, {32, 15},
         {32, 28}, {32, 29}, {32, 32}, {32, 33}, {32, 34}, {32, 39}, {32, 40},
         {32, 48}, {32, 49}, {32, 53}, {32, 56}, {33, 1},  {33, 2},  {33, 32},
         {33, 33}, {33, 34}, {33, 49}, {33, 53}, {33, 56}, {34, 1},  {34, 2},
         {34, 32}, {34, 33}, {34, 34}, {34, 49}, {34, 53}, {34, 56}, {38, 34},
         {39, 57}, {40, 34}, {47, 15}, {47, 49}, {48, 2},  {48, 34}, {48, 56},
         {49, 1},  {49, 2},  {49, 28}, {49, 32}, {49, 33}, {49, 34}, {49, 39},
         {49, 49}, {49, 56}, {53, 1},  {53, 2},  {53, 28}, {53, 34}, {53, 53},
         {53, 57}, {55, 1},  {55, 28}, {55, 34}, {55, 53}, {55, 55}, {55, 56},
         {56, 1},  {56, 2},  {56, 7},  {56, 13}, {56, 32}, {56, 33}, {56, 34},
         {56, 49}, {56, 53}, {56, 56}, {56, 64}, {57, 34}, {57, 56}, {57, 57},
         {64, 1},  {64, 64}, {65, 1},  {65, 65}}};

unsigned getMaxDominatorTreeDepth(const Function &F,
                                  const DominatorTree &Tree) {
  unsigned MaxBBDepth = 0;
  for (const auto &BB : F)
    if (const auto *TN = Tree.getNode(&BB))
      MaxBBDepth = std::max(MaxBBDepth, TN->getLevel());

  return MaxBBDepth;
}

std::pair<int, int>
getValidCallUsesAndInLoopCounts(Function &F, FunctionAnalysisManager &FAM) {
  unsigned CallUses = 0;
  unsigned CallUsesInLoop = 0;

  for (User *U : F.users()) {
    if (CallBase *CB = dyn_cast<CallBase>(U)) {
      ++CallUses;
      BasicBlock *BB = CB->getParent();
      Function *FUser = CB->getCaller();
      auto &LI = FAM.getResult<LoopAnalysis>(*FUser);
      if (LI.getLoopFor(BB) != nullptr) {
        ++CallUsesInLoop;
      }
    }
  }
  return std::make_pair(CallUses, CallUsesInLoop);
}

void ProteanFIExtendedFeatures::updateLoopRelatedFeatures(
    Function &F, LoopInfo &LI, FunctionFeatures &FF) {
  uint64_t LoopNum = std::distance(LI.begin(), LI.end());

  uint64_t LoopInstrCount = 0;
  uint64_t BlockWithMulSuccNum = 0;
  uint64_t LoopLevelSum = 0;
  for (auto &L : LI) {
    LoopLevelSum += static_cast<uint64_t>(L->getLoopDepth());
    FF[NamedFeatureIndex::MaxLoopDepth] =
        std::max(FF[NamedFeatureIndex::MaxLoopDepth],
                 static_cast<uint64_t>(L->getLoopDepth()));
    for (const BasicBlock *BB : L->getBlocks()) {
      unsigned SuccCount = std::distance(succ_begin(BB), succ_end(BB));
      if (SuccCount > 1)
        BlockWithMulSuccNum++;
      LoopInstrCount += std::distance(BB->instructionsWithoutDebug().begin(),
                                      BB->instructionsWithoutDebug().end());
    }
  }

  FF[NamedFeatureIndex::Loops] = LoopNum;
  if (LoopNum != 0) {
    uint64_t q = LoopInstrCount / LoopNum;
    FF[NamedFloatFeatureIndex::InstrPerLoop] =
        q + ((float)(LoopInstrCount - q * LoopNum)) / LoopNum;
    q = BlockWithMulSuccNum / LoopNum;
    FF[NamedFloatFeatureIndex::BlockWithMultipleSuccessorsPerLoop] =
        q + ((float)(BlockWithMulSuccNum - q * LoopNum)) / LoopNum;
    q = LoopLevelSum / LoopNum;
    FF[NamedFloatFeatureIndex::AvgNestedLoopLevel] =
        q + ((float)(LoopLevelSum - q * LoopNum)) / LoopNum;
  }
}

void ProteanFIExtendedFeatures::updateBBLoopCallsiteBFFeatures(
    Function &F, FunctionFeatures &FF, LoopInfo &LI,
    FunctionAnalysisManager &FAM) {
  // Initializations before looping
  unsigned NumCallsiteInLoop = 0;
  unsigned NumCallsite = 0;
  uint64_t MaxCallsiteBlockFreq = 0;
  uint64_t InstrNum = 0;
  uint64_t SuccNum = 0;
  uint64_t VecNum = 0;
  uint64_t BlockNum = F.size();
  auto getPairIndex = [](size_t a, size_t b) {
    auto I = llvm::find(ImportantInstructionSuccessions, std::make_pair(a, b));
    if (I == ImportantInstructionSuccessions.end())
      return -1;
    return static_cast<int>(
        std::distance(ImportantInstructionSuccessions.begin(), I));
  };
  int StartID = 0;
  int LastID = StartID;

  // We don't want debug calls, because they'd just add noise.
  // Sum number of instructions and successors on the way
  for (auto &BB : F) {
    SuccNum += std::distance(succ_begin(&BB), succ_end(&BB));
    for (auto &I : BB.instructionsWithoutDebug()) {
      if (CallBase *CB = dyn_cast<CallBase>(&I)) {
        Function *Callee = CB->getCalledFunction();
        if (Callee && !Callee->isIntrinsic()) {
          ++NumCallsite;
          if (!Callee->isDeclaration()) {
            // Check all the functions that was called and get the max block
            // frequency.
            uint64_t EntryFreq = FAM.getResult<BlockFrequencyAnalysis>(*Callee)
                                     .getEntryFreq()
                                     .getFrequency();
            MaxCallsiteBlockFreq = std::max(EntryFreq, MaxCallsiteBlockFreq);
          }

          if (Callee != nullptr) {
            // Collect the number of callsites that were invoked with a pointer
            // argument.
            for (auto arg = Callee->arg_begin(); arg != Callee->arg_end();
                 arg++)
              if (isa<PointerType>(arg->getType())) {
                FF[NamedFeatureIndex::PtrCallee]++;
                break;
              }
          }

          // Collect the number of callsites that returns a pointer type.
          if (isa<PointerType>(CB->getType())) {
            FF[NamedFeatureIndex::CallReturnPtr]++;
          }

          // Check if the given function is recursive.
          if (&F == Callee) {
            FF[NamedFeatureIndex::IsRecursive] = 1;
          }

          BasicBlock *BB = CB->getParent();
          // if we found a loop for the BB that Call is in, we do +1
          if (LI.getLoopFor(BB) != nullptr) {
            ++NumCallsiteInLoop;
          }
        }
      }

      auto ID = I.getOpcode();
      ++FF.InstructionHistogram[ID];
      int PairIndex = getPairIndex(LastID, ID);
      if (PairIndex >= 0)
        ++FF.InstructionPairHistogram[PairIndex];
      LastID = ID;
      InstrNum++;
      unsigned NumOp = I.getNumOperands();

      // If instruction contains vector operand, consider it as a vector
      // instruction
      for (unsigned i = 0; i < NumOp; i++) {
        if (isa<VectorType>(I.getOperand(i)->getType())) {
          VecNum++;
          break;
        }
      }

      // If this is a conditional branch, check if it uses an argument
      if (const auto II = dyn_cast<BranchInst>(&I))
        if (II->isConditional()) {
          FF[NamedFeatureIndex::ConditionalBranch]++;
          // find the instruction where the condition is defined.
          if (auto def = dyn_cast<Instruction>(II->getCondition())) {
            // For all operands of def check if isa<Argument> (operand) then
            // increment CBwithArg.
            bool found = false;
            for (unsigned i = 0; i < def->getNumOperands(); i++) {
              if (isa<Argument>(def->getOperand(i))) {
                FF[NamedFeatureIndex::CBwithArg]++;
                found = true;
                break;
              }
            }
            if (found)
              break;
          }
        }
    }
  }

  FF[NamedFloatFeatureIndex::AvgVecInstr] = (float)VecNum / InstrNum;
  FF[NamedFeatureIndex::Blocks] = BlockNum;
  if (BlockNum > 0) {
    uint64_t q = InstrNum / BlockNum;
    FF[NamedFloatFeatureIndex::InstructionPerBlock] =
        q + ((float)(InstrNum - q * BlockNum)) / BlockNum;
    q = SuccNum / BlockNum;
    FF[NamedFloatFeatureIndex::SuccessorPerBlock] =
        q + ((float)(SuccNum - q * BlockNum)) / BlockNum;
  }

  FF[NamedFeatureIndex::MaxCallsiteBlockFreq] = MaxCallsiteBlockFreq;
  FF[NamedFeatureIndex::NumCallsiteInLoop] = NumCallsiteInLoop;
  FF[NamedFeatureIndex::Calls] = NumCallsite;
}

ProteanFIExtendedFeatures::FunctionFeatures
ProteanFIExtendedFeatures::getFunctionFeatures(
    Function &F, DominatorTree &DomTree, TargetTransformInfo &TTI, LoopInfo &LI,
    FunctionAnalysisManager &FAM, bool ValidSize, bool ValidLoop,
    bool ValidTree) {
  assert(llvm::is_sorted(ImportantInstructionSuccessions) &&
         "expected function features are sorted");

  FunctionFeatures FF;
  size_t InstrCount = getMaxInstructionID() + 1;
  FF.InstructionHistogram.resize(InstrCount);
  FF.InstructionPairHistogram.resize(ImportantInstructionSuccessions.size());

  // check all the argument to see if there is a pointer type
  for (auto arg = F.arg_begin(); arg != F.arg_end(); arg++) {
    if (isa<PointerType>(arg->getType())) {
      FF[NamedFeatureIndex::PtrArgs]++;
    }
  }

  std::pair<int, int> ValidCallAndInLoopCounts =
      getValidCallUsesAndInLoopCounts(F, FAM);
  if (!ValidSize)
    FF[NamedFeatureIndex::InitialSize] = getSize(F, TTI);
  FF[NamedFeatureIndex::IsLocal] = F.hasLocalLinkage();
  FF[NamedFeatureIndex::IsLinkOnceODR] = F.hasLinkOnceODRLinkage();
  FF[NamedFeatureIndex::IsLinkOnce] = F.hasLinkOnceLinkage();
  if (!ValidTree)
    FF[NamedFeatureIndex::MaxDomTreeLevel] =
        getMaxDominatorTreeDepth(F, DomTree);
  FF[NamedFeatureIndex::CallUsage] = ValidCallAndInLoopCounts.first;
  FF[NamedFeatureIndex::NumOfCallUsesInLoop] = ValidCallAndInLoopCounts.second;
  FF[NamedFeatureIndex::EntryBlockFreq] =
      FAM.getResult<BlockFrequencyAnalysis>(F).getEntryFreq().getFrequency();
  ProteanFIExtendedFeatures::updateBBLoopCallsiteBFFeatures(F, FF, LI, FAM);
  if (!ValidLoop)
    ProteanFIExtendedFeatures::updateLoopRelatedFeatures(F, LI, FF);
  return FF;
}

ProteanFIExtendedFeatures::NamedFeatureIndex &
operator++(ProteanFIExtendedFeatures::NamedFeatureIndex &n) {
  return n = static_cast<ProteanFIExtendedFeatures::NamedFeatureIndex>((int)n +
                                                                       1);
}

ProteanFIExtendedFeatures::NamedFeatureIndex
operator++(ProteanFIExtendedFeatures::NamedFeatureIndex &n, int) {
  ProteanFIExtendedFeatures::NamedFeatureIndex res = n;
  ++n;
  return res;
}

ProteanFIExtendedFeatures::NamedFloatFeatureIndex &
operator++(ProteanFIExtendedFeatures::NamedFloatFeatureIndex &n) {
  return n = static_cast<ProteanFIExtendedFeatures::NamedFloatFeatureIndex>(
             (int)n + 1);
}

ProteanFIExtendedFeatures::NamedFloatFeatureIndex
operator++(ProteanFIExtendedFeatures::NamedFloatFeatureIndex &n, int) {
  ProteanFIExtendedFeatures::NamedFloatFeatureIndex res = n;
  ++n;
  return res;
}
PreservedAnalyses CollectFeaturesPass::run(Module &M,
                                           ModuleAnalysisManager &AM) {
  // Add stuff from inliner run(), like psi
  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  auto &IAA = AM.getResult<InlineAdvisorAnalysis>(M);
  InlineParams Params = getInlineParams(3, 3);
  IAA.tryCreate(
      Params, InliningAdvisorMode::Default, {},
      InlineContext{ThinOrFullLTOPhase::None, InlinePass::ModuleInliner});
  InlineAdvisor *IA = IAA.getAdvisor();
  assert(IA && "IA is null!");

  std::error_code EC;
  raw_fd_ostream RawOS(ProteanModelFile.getValue(), EC, sys::fs::CD_OpenAlways,
                       sys::fs::FA_Write, sys::fs::OF_Append);
  formatted_raw_ostream OS(RawOS);
  ModelDataProteanCollector MDC(OS, false, ProteanModelFile);
  bool IsCollected = false;
  for (Function &F : M) {
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *CB = dyn_cast<CallBase>(&I)) {
          if (Function *Callee = CB->getCalledFunction()) {
            if (!Callee->isDeclaration()) {
              MDC.collectFeatures(CB, IA, &FAM, &AM);
              if (FeatureDump) {
                if (MDC.isEmptyOutputFile() && !IsCollected) {
                  IsCollected = true;
                  MDC.printRow(true, M, F, *CB);
                }
                std::vector<std::pair<std::string, std::string>> Features =
                    MDC.getFeatures();

                MDC.printRow(false, M, F, *CB);
              }
            }
          }
        }
      }
    }
  }
  return PreservedAnalyses::all();
}
ProteanCollectFeatures::FunctionFeaturesCache
    ProteanCollectFeatures::FeatureCache;
ProteanCollectFeatures::FunctionAnalysisMap
    ProteanCollectFeatures::FunctionAnalysisCache;
} // namespace llvm
