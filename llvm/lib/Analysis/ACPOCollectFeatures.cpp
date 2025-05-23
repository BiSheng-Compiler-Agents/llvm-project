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
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/DumpFeature.h"
#include "llvm/Analysis/FunctionPropertiesAnalysis.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ScalarEvolution.h"
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
static void calculateBasicBlockFeatures(
    ACPOCollectFeatures &ACF, const ACPOCollectFeatures::FeatureInfo &info);
static void calculateEdgeFeatures(
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

static void
calculateMemOptFeatures(ACPOCollectFeatures &ACF,
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
        REGISTER_NAME(NumSuccessors, "num_successors"),
        REGISTER_NAME(NumInstrs, "num_instrs"),
        REGISTER_NAME(NumCriticalEdges, "num_critical_deges"),
        REGISTER_NAME(HighestNumInstrsInSucc, "highest_num_instrs_in_succ"),
        REGISTER_NAME(SuccNumWithHighestNumInstrs,
                      "succ_num_with_highest_num_instrs"),
        REGISTER_NAME(IsBranchInst, "is_branch_inst"),
        REGISTER_NAME(IsSwitchInst, "is_switch_inst"),
        REGISTER_NAME(IsIndirectBrInst, "is_indirect_br_inst"),
        REGISTER_NAME(IsInvokeInst, "is_invoke_inst"),
        REGISTER_NAME(IsCallBrInst, "iscall_br_inst"),
        REGISTER_NAME(IsFirstOpPtr, "is_first_op_ptr"),
        REGISTER_NAME(IsSecondOpNull, "is_second_op_null"),
        REGISTER_NAME(IsSecondOpConstant, "is_second_op_constant"),
        REGISTER_NAME(IsEqCmp, "is_eq_cmp"),
        REGISTER_NAME(IsNeCmp, "is_ne_cmp"),
        REGISTER_NAME(IsGtCmp, "is_gt_cmp"),
        REGISTER_NAME(IsLtCmp, "is_lt_cmp"),
        REGISTER_NAME(IsGeCmp, "is_ge_cmp"),
        REGISTER_NAME(IsLeCmp, "is_le_cmp"),
        REGISTER_NAME(IsIVCmp, "is_iv_cmp"),
        REGISTER_NAME(IsBBInLoop, "is_bb_in_loop"),
        REGISTER_NAME(IsFirstSuccInLoop, "is_first_succ_in_loop"),
        REGISTER_NAME(IsSecondSuccInLoop, "is_second_succ_in_loop"),
        REGISTER_NAME(DestNumSuccessors, "dest_num_successors"),
        REGISTER_NAME(DestNumInstrs, "dest_num_instrs"),
        REGISTER_NAME(DestNumCriticalEdges, "dest_num_critical_edges"),
        REGISTER_NAME(DestIsBranchInst, "dest_is_branch_inst"),
        REGISTER_NAME(DestIsSwitchInst, "dest_is_switch_inst"),
        REGISTER_NAME(DestIsIndirectBrInst, "dest_is_indirect_br_inst"),
        REGISTER_NAME(DestIsInvokeInst, "dest_is_invoke_inst"),
        REGISTER_NAME(DestIsCallBrInst, "dest_is_call_br_inst"),
        REGISTER_NAME(DestSuccNumber, "dest_succ_number"),
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
        REGISTER_NAME(NumInst, "num_inst"),
        REGISTER_NAME(NumPhis, "num_phis"),
        REGISTER_NAME(NumCalls, "num_calls"),
        REGISTER_NAME(NumLoads, "num_loads"),
        REGISTER_NAME(NumStores, "num_stores"),
        REGISTER_NAME(NumPreds, "num_preds"),
        REGISTER_NAME(NumSuccs, "num_succs"),
        REGISTER_NAME(EndsWithUnreachable, "ends_with_unreachable"),
        REGISTER_NAME(EndsWithReturn, "ends_with_return"),
        REGISTER_NAME(EndsWithCondBranch, "ends_with_cond_branch"),
        REGISTER_NAME(EndsWithBranch, "ends_with_branch"),
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
        REGISTER_SCOPE(NumSuccessors, BasicBlock),
        REGISTER_SCOPE(NumInstrs, BasicBlock),
        REGISTER_SCOPE(NumCriticalEdges, BasicBlock),
        REGISTER_SCOPE(HighestNumInstrsInSucc, BasicBlock),
        REGISTER_SCOPE(SuccNumWithHighestNumInstrs, BasicBlock),
        REGISTER_SCOPE(IsBranchInst, BasicBlock),
        REGISTER_SCOPE(IsSwitchInst, BasicBlock),
        REGISTER_SCOPE(IsIndirectBrInst, BasicBlock),
        REGISTER_SCOPE(IsInvokeInst, BasicBlock),
        REGISTER_SCOPE(IsCallBrInst, BasicBlock),
        REGISTER_SCOPE(IsFirstOpPtr, BasicBlock),
        REGISTER_SCOPE(IsSecondOpNull, BasicBlock),
        REGISTER_SCOPE(IsSecondOpConstant, BasicBlock),
        REGISTER_SCOPE(IsEqCmp, BasicBlock),
        REGISTER_SCOPE(IsNeCmp, BasicBlock),
        REGISTER_SCOPE(IsGtCmp, BasicBlock),
        REGISTER_SCOPE(IsLtCmp, BasicBlock),
        REGISTER_SCOPE(IsGeCmp, BasicBlock),
        REGISTER_SCOPE(IsLeCmp, BasicBlock),
        REGISTER_SCOPE(IsIVCmp, BasicBlock),
        REGISTER_SCOPE(IsBBInLoop, BasicBlock),
        REGISTER_SCOPE(IsFirstSuccInLoop, BasicBlock),
        REGISTER_SCOPE(IsSecondSuccInLoop, BasicBlock),
        REGISTER_SCOPE(DestNumSuccessors, Edge),
        REGISTER_SCOPE(DestNumInstrs, Edge),
        REGISTER_SCOPE(DestNumCriticalEdges, Edge),
        REGISTER_SCOPE(DestIsBranchInst, Edge),
        REGISTER_SCOPE(DestIsSwitchInst, Edge),
        REGISTER_SCOPE(DestIsIndirectBrInst, Edge),
        REGISTER_SCOPE(DestIsInvokeInst, Edge),
        REGISTER_SCOPE(DestIsCallBrInst, Edge),
        REGISTER_SCOPE(DestSuccNumber, Edge),
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
        REGISTER_SCOPE(NumInst, MemOpt),
        REGISTER_SCOPE(NumPhis, MemOpt),
        REGISTER_SCOPE(NumCalls, MemOpt),
        REGISTER_SCOPE(NumLoads, MemOpt),
        REGISTER_SCOPE(NumStores, MemOpt),
        REGISTER_SCOPE(NumPreds, MemOpt),
        REGISTER_SCOPE(NumSuccs, MemOpt),
        REGISTER_SCOPE(EndsWithUnreachable, MemOpt),
        REGISTER_SCOPE(EndsWithReturn, MemOpt),
        REGISTER_SCOPE(EndsWithCondBranch, MemOpt),
        REGISTER_SCOPE(EndsWithBranch, MemOpt),
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
        REGISTER_GROUP(NumSuccessors, BasicBlockFeatures),
        REGISTER_GROUP(NumInstrs, BasicBlockFeatures),
        REGISTER_GROUP(NumCriticalEdges, BasicBlockFeatures),
        REGISTER_GROUP(HighestNumInstrsInSucc, BasicBlockFeatures),
        REGISTER_GROUP(SuccNumWithHighestNumInstrs, BasicBlockFeatures),
        REGISTER_GROUP(IsBranchInst, BasicBlockFeatures),
        REGISTER_GROUP(IsSwitchInst, BasicBlockFeatures),
        REGISTER_GROUP(IsIndirectBrInst, BasicBlockFeatures),
        REGISTER_GROUP(IsInvokeInst, BasicBlockFeatures),
        REGISTER_GROUP(IsCallBrInst, BasicBlockFeatures),
        REGISTER_GROUP(IsFirstOpPtr, BasicBlockFeatures),
        REGISTER_GROUP(IsSecondOpNull, BasicBlockFeatures),
        REGISTER_GROUP(IsSecondOpConstant, BasicBlockFeatures),
        REGISTER_GROUP(IsEqCmp, BasicBlockFeatures),
        REGISTER_GROUP(IsNeCmp, BasicBlockFeatures),
        REGISTER_GROUP(IsGtCmp, BasicBlockFeatures),
        REGISTER_GROUP(IsLtCmp, BasicBlockFeatures),
        REGISTER_GROUP(IsGeCmp, BasicBlockFeatures),
        REGISTER_GROUP(IsLeCmp, BasicBlockFeatures),
        REGISTER_GROUP(IsIVCmp, BasicBlockFeatures),
        REGISTER_GROUP(IsBBInLoop, BasicBlockFeatures),
        REGISTER_GROUP(IsFirstSuccInLoop, BasicBlockFeatures),
        REGISTER_GROUP(IsSecondSuccInLoop, BasicBlockFeatures),
        REGISTER_GROUP(DestNumSuccessors, EdgeFeatures),
        REGISTER_GROUP(DestNumInstrs, EdgeFeatures),
        REGISTER_GROUP(DestNumCriticalEdges, EdgeFeatures),
        REGISTER_GROUP(DestIsBranchInst, EdgeFeatures),
        REGISTER_GROUP(DestIsSwitchInst, EdgeFeatures),
        REGISTER_GROUP(DestIsIndirectBrInst, EdgeFeatures),
        REGISTER_GROUP(DestIsInvokeInst, EdgeFeatures),
        REGISTER_GROUP(DestIsCallBrInst, EdgeFeatures),
        REGISTER_GROUP(DestSuccNumber, EdgeFeatures),
        REGISTER_GROUP(NumInst, MemOptFeatures),
        REGISTER_GROUP(NumPhis, MemOptFeatures),
        REGISTER_GROUP(NumCalls, MemOptFeatures),
        REGISTER_GROUP(NumLoads, MemOptFeatures),
        REGISTER_GROUP(NumStores, MemOptFeatures),
        REGISTER_GROUP(NumPreds, MemOptFeatures),
        REGISTER_GROUP(NumSuccs, MemOptFeatures),
        REGISTER_GROUP(EndsWithUnreachable, MemOptFeatures),
        REGISTER_GROUP(EndsWithReturn, MemOptFeatures),
        REGISTER_GROUP(EndsWithCondBranch, MemOptFeatures),
        REGISTER_GROUP(EndsWithBranch, MemOptFeatures),
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
        REGISTER_FUNCTION(NumSuccessors, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(NumInstrs, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(NumCriticalEdges, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(HighestNumInstrsInSucc, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(SuccNumWithHighestNumInstrs,
                          calculateBasicBlockFeatures),
        REGISTER_FUNCTION(IsBranchInst, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(IsSwitchInst, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(IsIndirectBrInst, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(IsInvokeInst, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(IsCallBrInst, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(IsFirstOpPtr, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(IsSecondOpNull, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(IsSecondOpConstant, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(IsEqCmp, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(IsNeCmp, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(IsGtCmp, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(IsLtCmp, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(IsGeCmp, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(IsLeCmp, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(IsIVCmp, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(IsBBInLoop, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(IsFirstSuccInLoop, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(IsSecondSuccInLoop, calculateBasicBlockFeatures),
        REGISTER_FUNCTION(DestNumSuccessors, calculateEdgeFeatures),
        REGISTER_FUNCTION(DestNumInstrs, calculateEdgeFeatures),
        REGISTER_FUNCTION(DestNumCriticalEdges, calculateEdgeFeatures),
        REGISTER_FUNCTION(DestIsBranchInst, calculateEdgeFeatures),
        REGISTER_FUNCTION(DestIsSwitchInst, calculateEdgeFeatures),
        REGISTER_FUNCTION(DestIsIndirectBrInst, calculateEdgeFeatures),
        REGISTER_FUNCTION(DestIsInvokeInst, calculateEdgeFeatures),
        REGISTER_FUNCTION(DestIsCallBrInst, calculateEdgeFeatures),
        REGISTER_FUNCTION(DestSuccNumber, calculateEdgeFeatures),
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
        REGISTER_FUNCTION(NumInst, calculateMemOptFeatures),
        REGISTER_FUNCTION(NumPhis, calculateMemOptFeatures),
        REGISTER_FUNCTION(NumCalls, calculateMemOptFeatures),
        REGISTER_FUNCTION(NumLoads, calculateMemOptFeatures),
        REGISTER_FUNCTION(NumStores, calculateMemOptFeatures),
        REGISTER_FUNCTION(NumPreds, calculateMemOptFeatures),
        REGISTER_FUNCTION(NumSuccs, calculateMemOptFeatures),
        REGISTER_FUNCTION(EndsWithUnreachable, calculateMemOptFeatures),
        REGISTER_FUNCTION(EndsWithReturn, calculateMemOptFeatures),
        REGISTER_FUNCTION(EndsWithCondBranch, calculateMemOptFeatures),
        REGISTER_FUNCTION(EndsWithBranch, calculateMemOptFeatures),
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

static void
calculateMemOptFeatures(ACPOCollectFeatures &ACF,
                        const ACPOCollectFeatures::FeatureInfo &Info) {
  auto *BB = Info.SI.BB;
  auto *FAM = Info.Managers.FAM;
  auto *T = BB->getTerminator();

  int num_insts = 0;
  int num_phis = 0;
  int num_calls = 0;
  int num_loads = 0;
  int num_stores = 0;
  bool end_with_cond_branch = 0;
  bool end_with_branch = 0;

  for (auto &inst : *BB) {
    num_insts++;
    if (isa<PHINode>(inst))
      num_phis++;
    if (isa<CallInst>(inst))
      num_calls++;
    if (isa<LoadInst>(inst))
      num_loads++;
    if (isa<StoreInst>(inst))
      num_stores++;
  }

  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::NumInst, Info,
                             std::to_string(num_insts));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::NumPhis, Info,
                             std::to_string(num_phis));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::NumCalls, Info,
                             std::to_string(num_calls));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::NumLoads, Info,
                             std::to_string(num_loads));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::NumStores, Info,
                             std::to_string(num_stores));
  ACF.setFeatureValueAndInfo(
      ACPOCollectFeatures::FeatureIndex::NumPreds, Info,
      std::to_string(std::distance(pred_begin(BB), pred_end(BB))));
  ACF.setFeatureValueAndInfo(
      ACPOCollectFeatures::FeatureIndex::NumSuccs, Info,
      std::to_string(std::distance(succ_begin(BB), succ_end(BB))));
  ACF.setFeatureValueAndInfo(
      ACPOCollectFeatures::FeatureIndex::EndsWithUnreachable, Info,
      std::to_string(isa<UnreachableInst>(*T)));  
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::EndsWithReturn,
                             Info, std::to_string(isa<ReturnInst>(*T)));
  if (auto *BR = dyn_cast<BranchInst>(T)) {
    if (BR->isConditional())
      end_with_cond_branch = true;
    else if (BR->isUnconditional())
      end_with_branch = true;
  }

  ACF.setFeatureValueAndInfo(
      ACPOCollectFeatures::FeatureIndex::EndsWithCondBranch, Info,
      std::to_string(end_with_cond_branch));  
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::EndsWithBranch,
                             Info, std::to_string(end_with_branch));
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

void calculateBasicBlockFeatures(
    ACPOCollectFeatures &ACF, const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         ACPOCollectFeatures::getFeatureGroup(Info.Idx) == 
             ACPOCollectFeatures::GroupID::BasicBlockFeatures);

  // check if we already calculated the values.
  if (ACF.containsFeature(ACPOCollectFeatures::GroupID::BasicBlockFeatures))
    return;

  auto *BB = Info.SI.BB;
  auto *F = Info.SI.F;
  auto *FAM = Info.Managers.FAM;

  assert(BB && F && FAM && "One of BB, F or FAM is nullptr");

  unsigned NumInstrs = std::distance(BB->instructionsWithoutDebug().begin(), 
                                     BB->instructionsWithoutDebug().end());
  
  unsigned NumCriticalEdges = 0;
  for (auto &BBI : *F) {
    const Instruction *TI = BBI.getTerminator();
    for (unsigned I = 0, E = TI->getNumSuccessors(); I != E; ++I) {
      if(isCriticalEdge(TI, I))
        NumCriticalEdges++;
    }
  }

  Instruction *TI = BB->getTerminator();
  unsigned HighestNumInstrsInSucc = 0;
  unsigned SuccNumWithHighestNumInstrs = 0;
  
  for (unsigned I = 0, E = TI->getNumSuccessors(); I != E; ++I) {
    BasicBlock *Succ = TI->getSuccessor(I);
    unsigned CurrNumInstrs = std::distance(Succ->instructionsWithoutDebug().begin(),
            Succ->instructionsWithoutDebug().end());
    if (CurrNumInstrs > HighestNumInstrsInSucc) {
      HighestNumInstrsInSucc = CurrNumInstrs;
      SuccNumWithHighestNumInstrs = GetSuccessorNumber(BB, Succ);
    }
  }

  bool IsFirstOpPtr = false;
  bool IsSecondOpNull = false;
  bool IsSecondOpConstant = false;
  bool IsEqCmp = false;
  bool IsNeCmp = false;
  bool IsGtCmp = false;
  bool IsLtCmp = false;
  bool IsGeCmp = false;
  bool IsLeCmp = false;
  bool IsIndVarCmp = false;
  bool IsBBInLoop = false;
  bool IsFirstSuccInLoop = false;
  bool IsSecondSuccInLoop = false;
  if (BranchInst *BI = dyn_cast<BranchInst>(TI)) {
    if(BI->isConditional()) {
      Value *Cond = BI->getCondition();
      if (ICmpInst *CI = dyn_cast<ICmpInst>(Cond)) {
        Value *LHS = CI->getOperand(0);
        IsFirstOpPtr = LHS->getType()->isPointerTy();
        Value *RHS = CI->getOperand(1);
        IsSecondOpNull = isa<ConstantPointerNull>(RHS);
        IsSecondOpConstant = isa<Constant>(RHS);
        CmpInst::Predicate Pred = CI->getPredicate();
        IsEqCmp = Pred == CmpInst::ICMP_EQ;
        IsNeCmp = Pred == CmpInst::ICMP_NE;
        IsGtCmp = ICmpInst::isGT(Pred);
        IsLtCmp = ICmpInst::isLT(Pred);
        IsGeCmp = ICmpInst::isGE(Pred);
        IsLeCmp = ICmpInst::isLE(Pred);
      }

      LoopInfo &LI = FAM->getResult<LoopAnalysis>(*F);
      ScalarEvolution &SE = FAM->getResult<ScalarEvolutionAnalysis>(*F);
      for (auto &L : LI) {
        IsBBInLoop = (IsBBInLoop || L->contains(BB));
        IsFirstSuccInLoop = (IsFirstSuccInLoop || L->contains(TI->getSuccessor(0)));
        IsSecondSuccInLoop = (IsSecondSuccInLoop || L->contains(TI->getSuccessor(1)));
        if (PHINode *IndVar = L->getInductionVariable(SE))
          if (IndVar->getParent() == BB)
            IsIndVarCmp = true;
      }
    }
  }

  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsSecondSuccInLoop,
                             Info, std::to_string(IsSecondSuccInLoop));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsFirstSuccInLoop,
                             Info, std::to_string(IsFirstSuccInLoop));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsBBInLoop,
                             Info, std::to_string(IsBBInLoop));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsIVCmp,
                             Info, std::to_string(IsIndVarCmp));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsLeCmp,
                             Info, std::to_string(IsLeCmp));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsGeCmp,
                             Info, std::to_string(IsGeCmp));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsLtCmp,
                             Info, std::to_string(IsLtCmp));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsGtCmp,
                             Info, std::to_string(IsGtCmp));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsEqCmp,
                             Info, std::to_string(IsEqCmp));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsNeCmp,
                             Info, std::to_string(IsNeCmp));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsSecondOpConstant,
                             Info, std::to_string(IsSecondOpConstant));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsSecondOpNull,
                             Info, std::to_string(IsSecondOpNull));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsFirstOpPtr,
                             Info, std::to_string(IsFirstOpPtr));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsCallBrInst,
                             Info, std::to_string(isa<CallBrInst>(TI)));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsInvokeInst,
                             Info, std::to_string(isa<InvokeInst>(TI)));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsIndirectBrInst,
                             Info, std::to_string(isa<IndirectBrInst>(TI)));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsSwitchInst,
                             Info, std::to_string(isa<SwitchInst>(TI)));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::IsBranchInst,
                             Info, std::to_string(isa<BranchInst>(TI)));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::SuccNumWithHighestNumInstrs,
                             Info, std::to_string(SuccNumWithHighestNumInstrs));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::HighestNumInstrsInSucc,
                             Info, std::to_string(HighestNumInstrsInSucc));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::NumCriticalEdges,
                             Info, std::to_string(NumCriticalEdges));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::NumInstrs,
                             Info, std::to_string(NumInstrs));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::NumSuccessors,
                             Info, std::to_string(TI->getNumSuccessors()));
}

void calculateEdgeFeatures(
  ACPOCollectFeatures &ACF, const ACPOCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ACPOCollectFeatures::FeatureIndex::NumOfFeatures ||
         ACPOCollectFeatures::getFeatureGroup(Info.Idx) ==
            ACPOCollectFeatures::GroupID::EdgeFeatures);
  
  // Check if we already calculated the values.
  if (ACF.containsFeature(ACPOCollectFeatures::GroupID::EdgeFeatures))
    return;

  auto *BB = Info.SI.BB;
  auto *DestBB = Info.SI.DestBB;
  auto *F = Info.SI.F;
  auto *FAM = Info.Managers.FAM;

  assert(BB && DestBB && F && FAM && "One of BB, DestBB, F or FAM is nullptr");

  unsigned DestNumInstrs = std::distance(DestBB->instructionsWithoutDebug().begin(),
                                         DestBB->instructionsWithoutDebug().end());

  unsigned DestNumCriticalEdges = 0;
  for(auto &BBI : *F) {
    const Instruction *TI = BBI.getTerminator();
    for (unsigned I = 0, E = TI->getNumSuccessors(); I != E; ++I) {
      if (isCriticalEdge(TI, I))
        DestNumCriticalEdges++;
    }
  }

  const Instruction *TI = DestBB->getTerminator();
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::DestSuccNumber,
                             Info, std::to_string(GetSuccessorNumber(BB, DestBB)));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::DestIsCallBrInst,
                             Info, std::to_string(isa<CallBrInst>(TI)));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::DestIsInvokeInst,
                             Info, std::to_string(isa<InvokeInst>(TI)));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::DestIsIndirectBrInst,
                             Info, std::to_string(isa<IndirectBrInst>(TI)));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::DestIsSwitchInst,
                             Info, std::to_string(isa<SwitchInst>(TI)));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::DestIsBranchInst,
                             Info, std::to_string(isa<BranchInst>(TI)));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::DestNumCriticalEdges,
                             Info, std::to_string(DestNumCriticalEdges));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::DestNumInstrs,
                             Info, std::to_string(DestNumInstrs));
  ACF.setFeatureValueAndInfo(ACPOCollectFeatures::FeatureIndex::DestNumSuccessors,
                             Info, std::to_string(TI->getNumSuccessors()));
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
