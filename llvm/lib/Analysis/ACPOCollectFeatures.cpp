//===- ACPOCollectFeatures.cpp - ACPO Class for Feature Collection -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements ACPOCollectFeatures class
//
//===----------------------------------------------------------------------===//

#if defined(ENABLE_ACPO)
#include "llvm/Analysis/ACPOCollectFeatures.h"
#include "llvm/ADT/SCCIterator.h"
// The ACPOFIModel.h currently contains only the cache system for
// ACPOFIExtendedFeatures.
#include "llvm/Analysis/ACPOFIModel.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/DumpFeature.h"
#include "llvm/Analysis/FunctionPropertiesAnalysis.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "ACPOCollectFeatures"

namespace llvm {

// Helper function that is used to calculate features and each function should
// registered in the CalculateFeatureMap.
static void calculateFPIRelated(ACPOCollectFeatures &ACF,
                                const ACPOCollectFeatures::FeatureInfo &info);
static void
calculateCallerBlockFreq(ACPOCollectFeatures &ACF,
                         const ACPOCollectFeatures::FeatureInfo &info);
static void
calculateCallSiteHeight(ACPOCollectFeatures &ACF,
                        const ACPOCollectFeatures::FeatureInfo &info);
static void
calculateConstantParam(ACPOCollectFeatures &ACF,
                       const ACPOCollectFeatures::FeatureInfo &info);
static void calculateCostEstimate(ACPOCollectFeatures &ACF,
                                  const ACPOCollectFeatures::FeatureInfo &info);
static void
calculateEdgeNodeCount(ACPOCollectFeatures &ACF,
                       const ACPOCollectFeatures::FeatureInfo &info);
static void
calculateHotColdCallSite(ACPOCollectFeatures &ACF,
                         const ACPOCollectFeatures::FeatureInfo &info);
static void calculateLoopLevel(ACPOCollectFeatures &ACF,
                               const ACPOCollectFeatures::FeatureInfo &info);
static void
calculateMandatoryKind(ACPOCollectFeatures &ACF,
                       const ACPOCollectFeatures::FeatureInfo &info);
static void
calculateMandatoryOnly(ACPOCollectFeatures &ACF,
                       const ACPOCollectFeatures::FeatureInfo &info);
static void
calculateInlineCostFeatures(ACPOCollectFeatures &ACF,
                            const ACPOCollectFeatures::FeatureInfo &info);
static void calculateACPOFIExtendedFeaturesFeatures(
    ACPOCollectFeatures &ACF, const ACPOCollectFeatures::FeatureInfo &info);
static void
calculateIsIndirectCall(ACPOCollectFeatures &ACF,
                        const ACPOCollectFeatures::FeatureInfo &info);
static void
calculateIsInInnerLoop(ACPOCollectFeatures &ACF,
                       const ACPOCollectFeatures::FeatureInfo &info);
static void
calculateIsMustTailCall(ACPOCollectFeatures &ACF,
                        const ACPOCollectFeatures::FeatureInfo &info);
static void calculateIsTailCall(ACPOCollectFeatures &ACF,
                                const ACPOCollectFeatures::FeatureInfo &info);
static void calculateOptCode(ACPOCollectFeatures &ACF,
                             const ACPOCollectFeatures::FeatureInfo &info);

// Register FeatureIdx -> Feature name
//          FeatureIdx -> Scope, Scope -> FeatureIdx
//          FeatureIdx -> Group, Group -> FeatureIdx
//          FeatureIdx -> Calculating function
#define REGISTER_NAME(INDEX_NAME, NAME)                                        \
  { ACPOCollectFeatures::FeatureIndex::INDEX_NAME, NAME }
const std::unordered_map<ACPOCollectFeatures::FeatureIndex, std::string>
    ACPOCollectFeatures::FeatureIndexToName{
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
        REGISTER_NAME(ACPOFIExtendedFeaturesInitialSize, "InitialSize"),
        REGISTER_NAME(ACPOFIExtendedFeaturesBlocks, "Blocks"),
        REGISTER_NAME(ACPOFIExtendedFeaturesCalls, "Calls"),
        REGISTER_NAME(ACPOFIExtendedFeaturesIsLocal, "IsLocal"),
        REGISTER_NAME(ACPOFIExtendedFeaturesIsLinkOnceODR, "IsLinkOnceODR"),
        REGISTER_NAME(ACPOFIExtendedFeaturesIsLinkOnce, "IsLinkOnce"),
        REGISTER_NAME(ACPOFIExtendedFeaturesLoops, "Loops"),
        REGISTER_NAME(ACPOFIExtendedFeaturesMaxLoopDepth, "MaxLoopDepth"),
        REGISTER_NAME(ACPOFIExtendedFeaturesMaxDomTreeLevel, "MaxDomTreeLevel"),
        REGISTER_NAME(ACPOFIExtendedFeaturesPtrArgs, "PtrArgs"),
        REGISTER_NAME(ACPOFIExtendedFeaturesPtrCallee, "PtrCallee"),
        REGISTER_NAME(ACPOFIExtendedFeaturesCallReturnPtr, "CallReturnPtr"),
        REGISTER_NAME(ACPOFIExtendedFeaturesConditionalBranch,
                      "ConditionalBranch"),
        REGISTER_NAME(ACPOFIExtendedFeaturesCBwithArg, "CBwithArg"),
        REGISTER_NAME(ACPOFIExtendedFeaturesCallerHeight, "CallerHeight"),
        REGISTER_NAME(ACPOFIExtendedFeaturesCallUsage, "CallUsage"),
        REGISTER_NAME(ACPOFIExtendedFeaturesIsRecursive, "IsRecursive"),
        REGISTER_NAME(ACPOFIExtendedFeaturesNumCallsiteInLoop,
                      "NumCallsiteInLoop"),
        REGISTER_NAME(ACPOFIExtendedFeaturesNumOfCallUsesInLoop,
                      "NumOfCallUsesInLoop"),
        REGISTER_NAME(ACPOFIExtendedFeaturesEntryBlockFreq, "EntryBlockFreq"),
        REGISTER_NAME(ACPOFIExtendedFeaturesMaxCallsiteBlockFreq,
                      "MaxCallsiteBlockFreq"),
        REGISTER_NAME(ACPOFIExtendedFeaturesInstructionPerBlock,
                      "InstructionPerBlock"),
        REGISTER_NAME(ACPOFIExtendedFeaturesSuccessorPerBlock,
                      "SuccessorPerBlock"),
        REGISTER_NAME(ACPOFIExtendedFeaturesAvgVecInstr, "AvgVecInstr"),
        REGISTER_NAME(ACPOFIExtendedFeaturesAvgNestedLoopLevel,
                      "AvgNestedLoopLevel"),
        REGISTER_NAME(ACPOFIExtendedFeaturesInstrPerLoop, "InstrPerLoop"),
        REGISTER_NAME(ACPOFIExtendedFeaturesBlockWithMultipleSuccecorsPerLoop,
                      "BlockWithMultipleSuccecorsPerLoop"),
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
        REGISTER_NAME(NumOfFeatures,"num_features"),
    };
#undef REGISTER_NAME

#define REGISTER_SCOPE(INDEX_NAME, NAME)                                       \
  {                                                                            \
    ACPOCollectFeatures::FeatureIndex::INDEX_NAME,                             \
        ACPOCollectFeatures::Scope::NAME                                       \
  }
const std::unordered_map<ACPOCollectFeatures::FeatureIndex,
                         ACPOCollectFeatures::Scope>
    ACPOCollectFeatures::FeatureIndexToScope{
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
        REGISTER_SCOPE(ACPOFIExtendedFeaturesInitialSize, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesBlocks, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesCalls, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesIsLocal, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesIsLinkOnceODR, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesIsLinkOnce, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesLoops, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesMaxLoopDepth, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesMaxDomTreeLevel, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesPtrArgs, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesPtrCallee, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesCallReturnPtr, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesConditionalBranch, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesCBwithArg, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesCallerHeight, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesCallUsage, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesIsRecursive, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesNumCallsiteInLoop, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesNumOfCallUsesInLoop, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesEntryBlockFreq, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesMaxCallsiteBlockFreq, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesInstructionPerBlock, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesSuccessorPerBlock, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesAvgVecInstr, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesAvgNestedLoopLevel, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesInstrPerLoop, Function),
        REGISTER_SCOPE(ACPOFIExtendedFeaturesBlockWithMultipleSuccecorsPerLoop,
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
    ACPOCollectFeatures::FeatureIndex::INDEX_NAME,                             \
        ACPOCollectFeatures::GroupID::NAME                                     \
  }
const std::unordered_map<ACPOCollectFeatures::FeatureIndex,
                         ACPOCollectFeatures::GroupID>
    ACPOCollectFeatures::FeatureIndexToGroup{
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
        REGISTER_GROUP(ACPOFIExtendedFeaturesInitialSize,
                       ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesBlocks, ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesCalls, ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesIsLocal, ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesIsLinkOnceODR,
                       ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesIsLinkOnce,
                       ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesLoops, ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesMaxLoopDepth,
                       ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesMaxDomTreeLevel,
                       ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesPtrArgs, ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesPtrCallee, ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesCallReturnPtr,
                       ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesConditionalBranch,
                       ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesCBwithArg, ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesCallerHeight,
                       ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesCallUsage, ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesIsRecursive,
                       ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesNumCallsiteInLoop,
                       ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesNumOfCallUsesInLoop,
                       ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesEntryBlockFreq,
                       ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesMaxCallsiteBlockFreq,
                       ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesInstructionPerBlock,
                       ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesSuccessorPerBlock,
                       ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesAvgVecInstr,
                       ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesAvgNestedLoopLevel,
                       ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesInstrPerLoop,
                       ACPOFIExtendedFeatures),
        REGISTER_GROUP(ACPOFIExtendedFeaturesBlockWithMultipleSuccecorsPerLoop,
                       ACPOFIExtendedFeatures),
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

const std::multimap<ACPOCollectFeatures::GroupID,
                    ACPOCollectFeatures::FeatureIndex>
    ACPOCollectFeatures::GroupToFeatureIndices{
        inverseMap<ACPOCollectFeatures::GroupID,
                   ACPOCollectFeatures::FeatureIndex>(FeatureIndexToGroup)};

const std::multimap<ACPOCollectFeatures::Scope,
                    ACPOCollectFeatures::FeatureIndex>
    ACPOCollectFeatures::ScopeToFeatureIndices{
        inverseMap<ACPOCollectFeatures::Scope,
                   ACPOCollectFeatures::FeatureIndex>(FeatureIndexToScope)};

#define REGISTER_FUNCTION(INDEX_NAME, NAME)                                    \
  { ACPOCollectFeatures::FeatureIndex::INDEX_NAME, NAME }
const std::unordered_map<ACPOCollectFeatures::FeatureIndex,
                         ACPOCollectFeatures::CalculateFeatureFunction>
    ACPOCollectFeatures::CalculateFeatureMap{
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
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesInitialSize,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesBlocks,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesCalls,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesIsLocal,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesIsLinkOnceODR,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesIsLinkOnce,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesLoops,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesMaxLoopDepth,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesMaxDomTreeLevel,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesPtrArgs,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesPtrCallee,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesCallReturnPtr,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesConditionalBranch,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesCBwithArg,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesCallerHeight,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesCallUsage,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesIsRecursive,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesNumCallsiteInLoop,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesNumOfCallUsesInLoop,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesEntryBlockFreq,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesMaxCallsiteBlockFreq,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesInstructionPerBlock,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesSuccessorPerBlock,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesAvgVecInstr,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesAvgNestedLoopLevel,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(ACPOFIExtendedFeaturesInstrPerLoop,
                          calculateACPOFIExtendedFeaturesFeatures),
        REGISTER_FUNCTION(
            ACPOFIExtendedFeaturesBlockWithMultipleSuccecorsPerLoop,
            calculateACPOFIExtendedFeaturesFeatures),
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

std::map<const Function *, unsigned> ACPOCollectFeatures::FunctionLevels{};

ACPOCollectFeatures::ACPOCollectFeatures() {}

ACPOCollectFeatures::ACPOCollectFeatures(
    ACPOCollectFeatures::FeatureInfo GlobalInfo)
    : GlobalFeatureInfo(GlobalInfo) {
  assert(GlobalFeatureInfo.Idx == FeatureIndex::NumOfFeatures &&
         "When setting glboal FeatureInfo the Idx should always be "
         "NumOfFeatures");
}

ACPOCollectFeatures::~ACPOCollectFeatures() {}

void ACPOCollectFeatures::setFeatureValue(ACPOCollectFeatures::FeatureIndex Idx,
                                          std::string Val) {
  FeatureToValue[Idx] = Val;
}

void ACPOCollectFeatures::setFeatureInfo(
    ACPOCollectFeatures::FeatureIndex Idx,
    ACPOCollectFeatures::FeatureInfo Info) {
  assert(
      (Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
       Info.Idx == Idx || getFeatureGroup(Info.Idx) == getFeatureGroup(Idx)) &&
      "When setting FeatureToInfo map the key and value pair should both refer "
      "to the same Feature or the FeatureInfo.Idx should be NumOfFeatures.");
  FeatureToInfo[Idx] = Info;
}

void ACPOCollectFeatures::setFeatureValueAndInfo(
    ACPOCollectFeatures::FeatureIndex Idx,
    ACPOCollectFeatures::FeatureInfo Info, std::string Val) {
  setFeatureValue(Idx, Val);
  setFeatureInfo(Idx, Info);
}

void ACPOCollectFeatures::setGlobalFeatureInfo(
    ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == FeatureIndex::NumOfFeatures &&
         "When setting glboal FeatureInfo the Idx should always be "
         "NumOfFeatures");
  GlobalFeatureInfo = Info;
}

std::string
ACPOCollectFeatures::getFeature(ACPOCollectFeatures::FeatureIndex Idx) const {
  assert(registeredFeature(Idx) && "Feature not registered");
  return FeatureToValue.find(Idx)->second;
}

std::string
ACPOCollectFeatures::getFeatureName(ACPOCollectFeatures::FeatureIndex Idx) {
  return FeatureIndexToName.find(Idx)->second;
}

ACPOCollectFeatures::GroupID
ACPOCollectFeatures::getFeatureGroup(ACPOCollectFeatures::FeatureIndex Idx) {
  return FeatureIndexToGroup.find(Idx)->second;
}

ACPOCollectFeatures::Scope
ACPOCollectFeatures::getFeatureScope(ACPOCollectFeatures::FeatureIndex Idx) {
  return FeatureIndexToScope.find(Idx)->second;
}

std::set<ACPOCollectFeatures::FeatureIndex>
ACPOCollectFeatures::getGroupFeatures(ACPOCollectFeatures::GroupID Group) {
  std::set<ACPOCollectFeatures::FeatureIndex> FeatureIndices;
  auto Range = GroupToFeatureIndices.equal_range(Group);
  for (auto It = Range.first; It != Range.second; ++It) {
    FeatureIndices.insert(It->second);
  }
  return FeatureIndices;
}

std::set<ACPOCollectFeatures::FeatureIndex>
ACPOCollectFeatures::getScopeFeatures(ACPOCollectFeatures::Scope S) {
  std::set<ACPOCollectFeatures::FeatureIndex> FeatureIndices;
  auto Range = ScopeToFeatureIndices.equal_range(S);
  for (auto It = Range.first; It != Range.second; ++It) {
    FeatureIndices.insert(It->second);
  }
  return FeatureIndices;
}

bool ACPOCollectFeatures::containsFeature(
    ACPOCollectFeatures::FeatureIndex Idx) {
  return FeatureToValue.count(Idx) > 0;
}

bool ACPOCollectFeatures::containsFeature(
    ACPOCollectFeatures::GroupID GroupID) {
  for (auto FeatureIdx : getGroupFeatures(GroupID)) {
    if (!containsFeature(FeatureIdx))
      return false;
  }
  return true;
}

void ACPOCollectFeatures::clearFeatureValueMap() { FeatureToValue.clear(); }

bool ACPOCollectFeatures::registeredFeature(
    ACPOCollectFeatures::FeatureIndex Idx) const {
  return FeatureToValue.find(Idx) != FeatureToValue.end();
}

void calculateFPIRelated(ACPOCollectFeatures &ACF,
                         const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ACPOCollectFeatures::FeatureIndex::BasicBlockCount);

  auto *FAM = Info.Managers.FAM;
  auto *F = Info.SI.F;

  assert(F && FAM && "Function or FAM is nullptr");

  auto &FPI = FAM->getResult<FunctionPropertiesAnalysis>(*F);

  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::BasicBlockCount,
                             Info, std::to_string(FPI.BasicBlockCount));
  ACF.setFeatureValueAndInfo(
      ACPOCollectFeatures::FeatureIndex::
          BlocksReachedFromConditionalInstruction,
      Info, std::to_string(FPI.BlocksReachedFromConditionalInstruction));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::Uses, Info,
                             std::to_string(FPI.Uses));
}

void calculateCallerBlockFreq(ACPOCollectFeatures &ACF,
                              const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ACPOCollectFeatures::FeatureIndex::CallerBlockFreq);

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

  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::CallerBlockFreq,
                             Info, std::to_string(CallerBlockFreq));
}

void calculateCallSiteHeight(ACPOCollectFeatures &ACF,
                             const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ACPOCollectFeatures::FeatureIndex::CallSiteHeight);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ACPOCollectFeatures::FeatureIndex::CallSiteHeight))
    return;

  auto *CB = Info.SI.CB;
  auto *IA = Info.OI.IA;

  assert(CB && IA && "CallSite or IA is nullptr");

  if (IA) {
    ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::CallSiteHeight,
                              Info, std::to_string(IA->getCallSiteHeight(CB)));
    return;
  }
  LLVM_DEBUG(dbgs() << "IA was nullptr & callsite height is not set!" << "\n");
}

void calculateConstantParam(ACPOCollectFeatures &ACF,
                            const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ACPOCollectFeatures::FeatureIndex::ConstantParam);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ACPOCollectFeatures::FeatureIndex::ConstantParam))
    return;

  auto *CB = Info.SI.CB;
  assert(CB && "CallSite is nullptr");

  size_t NrCtantParams = 0;
  for (auto I = CB->arg_begin(), E = CB->arg_end(); I != E; ++I) {
    NrCtantParams += (isa<Constant>(*I));
  }

  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::ConstantParam,
                             Info, std::to_string(NrCtantParams));
}

void calculateCostEstimate(ACPOCollectFeatures &ACF,
                           const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ACPOCollectFeatures::FeatureIndex::CostEstimate);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ACPOCollectFeatures::FeatureIndex::CostEstimate))
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

  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::CostEstimate,
                             Info, std::to_string(CostEstimate));
}

int64_t getLocalCalls(Function &F, FunctionAnalysisManager &FAM) {
  return FAM.getResult<FunctionPropertiesAnalysis>(F)
      .DirectCallsToDefinedFunctions;
}

void calculateEdgeNodeCount(ACPOCollectFeatures &ACF,
                            const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         ACPOCollectFeatures::getFeatureGroup(Info.Idx) ==
             ACPOCollectFeatures::GroupID::EdgeNodeCount);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ACPOCollectFeatures::GroupID::EdgeNodeCount))
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
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::EdgeCount, Info,
                             EdgeCountStr);
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::NodeCount, Info,
                             NodeCountStr);
}

void calculateHotColdCallSite(ACPOCollectFeatures &ACF,
                              const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         ACPOCollectFeatures::getFeatureGroup(Info.Idx) ==
             ACPOCollectFeatures::GroupID::HotColdCallSite);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ACPOCollectFeatures::GroupID::HotColdCallSite))
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
  auto CallerEntryFreqHot = CallerBFI.getEntryFreq();
  bool HotCallSite = (CallSiteFreq.getFrequency() >= CallerEntryFreqHot * 60);

  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::ColdCallSite,
                             Info, std::to_string(ColdCallSite));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::HotCallSite,
                             Info, std::to_string(HotCallSite));
}

void calculateLoopLevel(ACPOCollectFeatures &ACF,
                        const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ACPOCollectFeatures::FeatureIndex::LoopLevel);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ACPOCollectFeatures::FeatureIndex::LoopLevel))
    return;

  auto *CB = Info.SI.CB;
  auto *FAM = Info.Managers.FAM;

  assert(CB && FAM && "CallBase or FAM is nullptr");

  Function *F = CB->getCaller();
  BasicBlock *BB = CB->getParent();
  LoopInfo &LI = FAM->getResult<LoopAnalysis>(*F);

  std::string OptCode = std::to_string(CB->getOpcode());
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::LoopLevel, Info,
                             std::to_string(LI.getLoopDepth(BB)));
}

InlineAdvisor::MandatoryInliningKind
ACPOCollectFeatures::getMandatoryKind(CallBase &CB,
                                      FunctionAnalysisManager &FAM,
                                      OptimizationRemarkEmitter &ORE) {
  return InlineAdvisor::getMandatoryKind(CB, FAM, ORE);
}

void calculateMandatoryKind(ACPOCollectFeatures &ACF,
                            const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ACPOCollectFeatures::FeatureIndex::MandatoryKind);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ACPOCollectFeatures::FeatureIndex::MandatoryKind))
    return;

  auto *CB = Info.SI.CB;
  auto *FAM = Info.Managers.FAM;

  assert(CB && FAM && "CallBase or FAM is nullptr");

  auto &Caller = *CB->getCaller();
  auto &ORE = FAM->getResult<OptimizationRemarkEmitterAnalysis>(Caller);
  auto MandatoryKind = ACPOCollectFeatures::getMandatoryKind(*CB, *FAM, ORE);

  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::MandatoryKind,
                             Info, std::to_string((int)MandatoryKind));
}

void calculateMandatoryOnly(ACPOCollectFeatures &ACF,
                            const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ACPOCollectFeatures::FeatureIndex::MandatoryOnly);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ACPOCollectFeatures::FeatureIndex::MandatoryOnly))
    return;

  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::MandatoryOnly,
                             Info, std::to_string((int)Info.OI.MandatoryOnly));
}

void calculateOptCode(ACPOCollectFeatures &ACF,
                      const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ACPOCollectFeatures::FeatureIndex::OptCode);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ACPOCollectFeatures::FeatureIndex::OptCode))
    return;

  auto *CB = Info.SI.CB;

  assert(CB && "CallBase is nullptr");

  std::string OptCode = std::to_string(CB->getOpcode());
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::OptCode, Info,
                             OptCode);
}

void calculateInlineCostFeatures(ACPOCollectFeatures &ACF,
                                 const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         (ACPOCollectFeatures::getFeatureGroup(Info.Idx) ==
          ACPOCollectFeatures::GroupID::InlineCostFeatureGroup));

  // Check if we already calculated the values.
  if (ACF.containsFeature(ACPOCollectFeatures::GroupID::InlineCostFeatureGroup))
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

  for (auto Idx =
           ACPOCollectFeatures::FeatureIndex::InlineCostFeatureGroupBegin + 1;
       Idx != ACPOCollectFeatures::FeatureIndex::InlineCostFeatureGroupEnd;
       ++Idx) {
    size_t TmpIdx =
        static_cast<size_t>(Idx) -
        static_cast<size_t>(
            ACPOCollectFeatures::FeatureIndex::InlineCostFeatureGroupBegin) -
        1;
    ACF.setFeatureValueAndInfo(
        Idx, Info,
        std::to_string(CostFeaturesOpt ? CostFeaturesOpt.value()[TmpIdx] : 0));
  }
}

static void
checkValidFFCache(Function &F,
                  struct ACPOFIExtendedFeatures::FunctionFeatures &FF,
                  DominatorTree &Tree, TargetTransformInfo &TTI, LoopInfo &LI,
                  bool &ValidSize, bool &ValidLoop, bool &ValidTree) {
  std::optional<size_t> SizeCache = ACPOFIModel::getCachedSize(
      &F, ACPOFIExtendedFeatures::NamedFeatureIndex::InitialSize);
  auto TTIAnalysisCache = ACPOFIModel::getTTICachedAnalysis(&F);
  if (SizeCache && TTIAnalysisCache == &TTI) {
    ValidSize = true;
  }

  std::optional<size_t> MaxDomTreeLevelCache = ACPOFIModel::getCachedSize(
      &F, ACPOFIExtendedFeatures::NamedFeatureIndex::MaxDomTreeLevel);
  auto DomCache = ACPOFIModel::getDomCachedAnalysis(&F);
  if (MaxDomTreeLevelCache && DomCache == &Tree) {
    ValidTree = true;
  }

  std::optional<size_t> LoopNumCache = ACPOFIModel::getCachedSize(
      &F, ACPOFIExtendedFeatures::NamedFeatureIndex::Loops);
  auto LIAnalysisCache = ACPOFIModel::getLICachedAnalysis(&F);
  if (LoopNumCache && LIAnalysisCache == &LI) {
    ValidLoop = true;
  }
}

static void getCachedFF(Function &F,
                        struct ACPOFIExtendedFeatures::FunctionFeatures &FF,
                        DominatorTree &Tree, TargetTransformInfo &TTI,
                        LoopInfo &LI) {
  std::optional<size_t> SizeCache = ACPOFIModel::getCachedSize(
      &F, ACPOFIExtendedFeatures::NamedFeatureIndex::InitialSize);
  auto TTIAnalysisCache = ACPOFIModel::getTTICachedAnalysis(&F);
  if (SizeCache && TTIAnalysisCache == &TTI) {
    FF[ACPOFIExtendedFeatures::NamedFeatureIndex::InitialSize] =
        SizeCache.value();
  }

  std::optional<size_t> MaxDomTreeLevelCache = ACPOFIModel::getCachedSize(
      &F, ACPOFIExtendedFeatures::NamedFeatureIndex::MaxDomTreeLevel);
  auto DomCache = ACPOFIModel::getDomCachedAnalysis(&F);
  if (MaxDomTreeLevelCache && DomCache == &Tree) {
    FF[ACPOFIExtendedFeatures::NamedFeatureIndex::MaxDomTreeLevel] =
        MaxDomTreeLevelCache.value();
  }

  std::optional<size_t> LoopNumCache = ACPOFIModel::getCachedSize(
      &F, ACPOFIExtendedFeatures::NamedFeatureIndex::Loops);
  auto LIAnalysisCache = ACPOFIModel::getLICachedAnalysis(&F);
  if (LoopNumCache && LIAnalysisCache == &LI) {
    FF[ACPOFIExtendedFeatures::NamedFeatureIndex::Loops] = LoopNumCache.value();
    FF[ACPOFIExtendedFeatures::NamedFeatureIndex::MaxLoopDepth] =
        ACPOFIModel::getCachedSize(
            &F, ACPOFIExtendedFeatures::NamedFeatureIndex::MaxLoopDepth)
            .value();
    if (LoopNumCache.value() != 0) {
      FF[ACPOFIExtendedFeatures::NamedFloatFeatureIndex::InstrPerLoop] =
          ACPOFIModel::getCachedFloat(
              &F, ACPOFIExtendedFeatures::NamedFloatFeatureIndex::InstrPerLoop)
              .value();
      FF[ACPOFIExtendedFeatures::NamedFloatFeatureIndex::
             BlockWithMultipleSuccecorsPerLoop] =
          ACPOFIModel::getCachedFloat(
              &F, ACPOFIExtendedFeatures::NamedFloatFeatureIndex::
                      BlockWithMultipleSuccecorsPerLoop)
              .value();
      FF[ACPOFIExtendedFeatures::NamedFloatFeatureIndex::AvgNestedLoopLevel] =
          ACPOFIModel::getCachedFloat(
              &F, ACPOFIExtendedFeatures::NamedFloatFeatureIndex::
                      AvgNestedLoopLevel)
              .value();
    }
  }
}

static void updateCachedFF(Function &F,
                           struct ACPOFIExtendedFeatures::FunctionFeatures &FF,
                           DominatorTree &Tree, TargetTransformInfo &TTI,
                           LoopInfo &LI) {
  ACPOFIModel::insertSizeCache(
      &F, ACPOFIExtendedFeatures::NamedFeatureIndex::InitialSize,
      FF[ACPOFIExtendedFeatures::NamedFeatureIndex::InitialSize]);
  ACPOFIModel::insertAnalysisCache(&F, &TTI);
  ACPOFIModel::insertSizeCache(
      &F, ACPOFIExtendedFeatures::NamedFeatureIndex::MaxDomTreeLevel,
      FF[ACPOFIExtendedFeatures::NamedFeatureIndex::MaxDomTreeLevel]);
  ACPOFIModel::insertAnalysisCache(&F, &Tree);
  ACPOFIModel::insertSizeCache(
      &F, ACPOFIExtendedFeatures::NamedFeatureIndex::Loops,
      FF[ACPOFIExtendedFeatures::NamedFeatureIndex::Loops]);
  ACPOFIModel::insertSizeCache(
      &F, ACPOFIExtendedFeatures::NamedFeatureIndex::MaxLoopDepth,
      FF[ACPOFIExtendedFeatures::NamedFeatureIndex::MaxLoopDepth]);
  ACPOFIModel::insertFloatCache(
      &F, ACPOFIExtendedFeatures::NamedFloatFeatureIndex::InstrPerLoop,
      FF[ACPOFIExtendedFeatures::NamedFloatFeatureIndex::InstrPerLoop]);
  ACPOFIModel::insertFloatCache(
      &F,
      ACPOFIExtendedFeatures::NamedFloatFeatureIndex::
          BlockWithMultipleSuccecorsPerLoop,
      FF[ACPOFIExtendedFeatures::NamedFloatFeatureIndex::
             BlockWithMultipleSuccecorsPerLoop]);
  ACPOFIModel::insertFloatCache(
      &F, ACPOFIExtendedFeatures::NamedFloatFeatureIndex::AvgNestedLoopLevel,
      FF[ACPOFIExtendedFeatures::NamedFloatFeatureIndex::AvgNestedLoopLevel]);
  ACPOFIModel::insertAnalysisCache(&F, &LI);
}

void calculateACPOFIExtendedFeaturesFeatures(
    ACPOCollectFeatures &ACF, const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         ACPOCollectFeatures::getFeatureGroup(Info.Idx) ==
             ACPOCollectFeatures::GroupID::ACPOFIExtendedFeatures);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ACPOCollectFeatures::GroupID::ACPOFIExtendedFeatures))
    return;

  auto F = Info.SI.F;
  auto *FAM = Info.Managers.FAM;

  assert(F && FAM && "F or FAM is nullptr");

  struct ACPOFIExtendedFeatures::FunctionFeatures FF;
  auto &DomTree = FAM->getResult<DominatorTreeAnalysis>(*F);
  auto &TTI = FAM->getResult<TargetIRAnalysis>(*F);
  auto &LI = FAM->getResult<LoopAnalysis>(*F);
  bool ValidSize = false;
  bool ValidLoop = false;
  bool ValidTree = false;
  checkValidFFCache(*F, FF, DomTree, TTI, LI, ValidSize, ValidLoop, ValidTree);
  FF = ACPOFIExtendedFeatures::getFunctionFeatures(
      *F, DomTree, TTI, LI, FAM, ValidSize, ValidLoop, ValidTree);
  getCachedFF(*F, FF, DomTree, TTI, LI);
  updateCachedFF(*F, FF, DomTree, TTI, LI);

  for (auto Idx = ACPOCollectFeatures::FeatureIndex::
                      ACPOFIExtendedFeaturesNamedFeatureBegin +
                  1;
       Idx !=
       ACPOCollectFeatures::FeatureIndex::ACPOFIExtendedFeaturesNamedFeatureEnd;
       ++Idx) {
    size_t TmpIdx =
        static_cast<size_t>(Idx) -
        static_cast<size_t>(ACPOCollectFeatures::FeatureIndex::
                                ACPOFIExtendedFeaturesNamedFeatureBegin) -
        1;
    ACF.setFeatureValueAndInfo(Idx, Info,
                               std::to_string(FF.NamedFeatures[TmpIdx]));
  }
  for (auto Idx = ACPOCollectFeatures::FeatureIndex::
                      ACPOFIExtendedFeaturesFloatFeatureBegin +
                  1;
       Idx !=
       ACPOCollectFeatures::FeatureIndex::ACPOFIExtendedFeaturesFloatFeatureEnd;
       ++Idx) {
    size_t TmpIdx =
        static_cast<size_t>(Idx) -
        static_cast<size_t>(ACPOCollectFeatures::FeatureIndex::
                                ACPOFIExtendedFeaturesFloatFeatureBegin) -
        1;
    ACF.setFeatureValueAndInfo(Idx, Info,
                               std::to_string(FF.NamedFloatFeatures[TmpIdx]));
  }
}

void calculateIsIndirectCall(ACPOCollectFeatures &ACF,
                             const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ACPOCollectFeatures::FeatureIndex::IsIndirectCall);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ACPOCollectFeatures::FeatureIndex::IsIndirectCall))
    return;

  auto *CB = Info.SI.CB;

  assert(CB && "CallBase is nullptr");

  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsIndirectCall,
                             Info, std::to_string(CB->isIndirectCall()));
}

void calculateIsInInnerLoop(ACPOCollectFeatures &ACF,
                            const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ACPOCollectFeatures::FeatureIndex::IsInInnerLoop);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ACPOCollectFeatures::FeatureIndex::IsInInnerLoop))
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

  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsInInnerLoop,
                             Info, std::to_string(CallSiteInInnerLoop));
}

void calculateIsMustTailCall(ACPOCollectFeatures &ACF,
                             const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ACPOCollectFeatures::FeatureIndex::IsMustTailCall);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ACPOCollectFeatures::FeatureIndex::IsMustTailCall))
    return;

  auto *CB = Info.SI.CB;

  assert(CB && "CallBase is nullptr");

  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsMustTailCall,
                             Info, std::to_string(CB->isMustTailCall()));
}

void calculateIsTailCall(ACPOCollectFeatures &ACF,
                         const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ACPOCollectFeatures::FeatureIndex::IsTailCall);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ACPOCollectFeatures::FeatureIndex::IsTailCall))
    return;

  auto *CB = Info.SI.CB;

  assert(CB && "CallBase is nullptr");

  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsTailCall,
                             Info, std::to_string(CB->isTailCall()));
}

ACPOCollectFeatures::FeatureValueMap ACPOCollectFeatures::getFeaturesPair(
    ACPOCollectFeatures::FeaturesInfo FeatureInfoVec) {
  clearFeatureValueMap();
  for (auto &FeatureInfo : FeatureInfoVec) {
    auto It = CalculateFeatureMap.find(FeatureInfo.Idx);
    if (It == CalculateFeatureMap.end()) {
      assert("Could not find the corresponding function to calculate feature");
    }
    auto CalculateFunction = It->second;
    CalculateFunction(*this, FeatureInfo);
    LLVM_DEBUG(dbgs() << "ACPO Feature " << getFeatureName(FeatureInfo.Idx)
                                         << ": " << FeatureToValue[FeatureInfo.Idx] << "\n");
  }

  return FeatureToValue;
}

ACPOCollectFeatures::FeatureValueMap
ACPOCollectFeatures::getFeaturesPair(ACPOCollectFeatures::Scopes ScopeVec) {
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
      LLVM_DEBUG(dbgs() << "ACPO Feature " << getFeatureName(FeatureIdx)
                                           << ": " << FeatureToValue[FeatureIdx] << "\n");
    }
  }

  return FeatureToValue;
}

ACPOCollectFeatures::FeatureValueMap
ACPOCollectFeatures::getFeaturesPair(ACPOCollectFeatures::GroupIDs GroupIDVec) {
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
      LLVM_DEBUG(dbgs() << "ACPO Feature " << getFeatureName(FeatureIdx)
                                           << ": " << FeatureToValue[FeatureIdx] << "\n");
    }
  }

  return FeatureToValue;
}

ACPOCollectFeatures::FeatureValueMap
ACPOCollectFeatures::getFeaturesPair(ACPOCollectFeatures::FeatureIndex Beg,
                                     ACPOCollectFeatures::FeatureIndex End) {
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

void ACPOCollectFeatures::clearFunctionLevel() { FunctionLevels.clear(); }

void ACPOCollectFeatures::insertFunctionLevel(const Function *F, unsigned FL) {
  FunctionLevels[F] = FL;
}

std::optional<unsigned>
ACPOCollectFeatures::getFunctionLevel(const Function *F) {
  auto It = FunctionLevels.find(F);
  if (It == FunctionLevels.end()) {
    return std::nullopt;
  } else {
    return It->second;
  }
}

ACPOCollectFeatures::FeatureIndex operator+(ACPOCollectFeatures::FeatureIndex N,
                                            int Counter) {
  return static_cast<ACPOCollectFeatures::FeatureIndex>((int)N + Counter);
}

ACPOCollectFeatures::FeatureIndex operator-(ACPOCollectFeatures::FeatureIndex N,
                                            int Counter) {
  return static_cast<ACPOCollectFeatures::FeatureIndex>((int)N - Counter);
}

ACPOCollectFeatures::FeatureIndex &
operator++(ACPOCollectFeatures::FeatureIndex &N) {
  return N = static_cast<ACPOCollectFeatures::FeatureIndex>((int)N + 1);
}

ACPOCollectFeatures::FeatureIndex
operator++(ACPOCollectFeatures::FeatureIndex &N, int) {
  ACPOCollectFeatures::FeatureIndex Res = N;
  ++N;
  return Res;
}

} // namespace llvm
#endif // ENABLE_ACPO
