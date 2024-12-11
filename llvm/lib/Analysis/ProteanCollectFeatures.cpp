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
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/FunctionPropertiesAnalysis.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/InlineOrder.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/LoopReuseAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/ModelDataCollector.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/ReplayInlineAdvisor.h"
#include "llvm/Analysis/ScalarEvolution.h"
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
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/LoopTools.h"
#include <llvm/ADT/APInt.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Type.h>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#define DEBUG_TYPE "proteanFC"

// In "llvm/lib/Analysis/ModelDataCollector.cpp"
extern llvm::cl::opt<std::string> ProteanModelFile;
extern llvm::cl::opt<std::string> ProteanLoopModelFile;
using namespace LoopTools;
namespace llvm {
static cl::opt<bool> FeatureDump("enable-protean-feature-dump",
                                 cl::init(false));
static cl::opt<bool> EnableLoopCollectFeature("enable-loop-collect-feature",
                                              cl::init(false));

class ModelDataProteanCollector : public ModelDataCollector {
public:
  ModelDataProteanCollector(formatted_raw_ostream &OS, bool OnlyMandatory,
                            std::string OutputFileName, std::string TempOutput)
      : ModelDataCollector(OS, OutputFileName), OnlyMandatory(OnlyMandatory),
        TempOutput(TempOutput) {}

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
    ModelDataCollector::proteanCollectFeatures();
  }

  void collectLoopFeatures(Loop *L, LoopStandardAnalysisResults *AR,
                           LoopAnalysisManager *LAM) {
    bool MandatoryOnly = getOnlyMandatory();
    resetRegisteredFeatures();

    BasicBlock *GlobalBB = L->getHeader();
    Function *GlobalF = GlobalBB->getParent();
    Module *GlobalM = GlobalF->getParent();
    ProteanCollectFeatures::FeatureInfo LoopFeatureInfo{
        ProteanCollectFeatures::FeatureIndex::NumOfFeatures,
        {nullptr, nullptr, AR},
        {GlobalF, nullptr, GlobalBB, GlobalM, L},
        {MandatoryOnly, nullptr}};

    registerFeature({ProteanCollectFeatures::Scope::Loop}, LoopFeatureInfo);
    ModelDataCollector::proteanCollectFeatures();
  }

  void collectFeatures(Module *M, InlineAdvisor *IA,
                       FunctionAnalysisManager *FAM,
                       ModuleAnalysisManager *MAM) {
    bool MandatoryOnly = getOnlyMandatory();
    resetRegisteredFeatures();
    Module *GlobalM = M;
    ProteanCollectFeatures::FeatureInfo GlobalFeatureInfo{
        ProteanCollectFeatures::FeatureIndex::NumOfFeatures,
        {FAM, MAM},
        {nullptr, nullptr, nullptr, GlobalM, nullptr},
        {MandatoryOnly, IA}};

    registerFeature({ProteanCollectFeatures::Scope::Module}, GlobalFeatureInfo);
    ModelDataCollector::proteanCollectFeatures();
  }

  std::string printHeader(Module *M, Function *F, CallBase *CB, Loop *L) {
    std::string Out = "";
    Out += "-Module,";
    if (F)
      Out += "Function,";
    if (CB)
      Out += "Callee,Caller,";
    if (L)
      Out += "Loop,";

    for (const auto &P : getIRFileNameMap()) {
      Out += P.getKey();
      Out += ",";
    }

    for (unsigned I = 0, E = Features.size(); I != E; ++I) {
      // First value does not get a comma
      if (I)
        Out += ",";
      Out += Features.at(I).first;
    }

    Out += "\n";
    return Out;
  }

  void updateOutput(bool PrintHeader, Module *M, Function *F, CallBase *CB,
                    Loop *L) {
    if (PrintHeader) {
      TempOutput += printHeader(M, F, CB, L);
      return;
    }
    std::string Out = "";
    Out += M->getName().str() + ",";
    if (F)
      Out += F->getName().str() + ",";
    if (CB)
      Out += CB->getCalledFunction()->getName().str() + "," +
             CB->getCaller()->getName().str() + ",";
    if (L)
      Out += L->getName().str() + ",";

    for (const auto &P : getIRFileNameMap()) {
      Out += P.getValue();
      Out += ",";
    }

    for (unsigned I = 0, E = Features.size(); I != E; ++I) {
      // First value does not get a comma
      if (I)
        Out += ",";
      Out += Features.at(I).second;
    }

    Out += "\n";
    TempOutput += Out;
    return;
  }

  std::vector<std::string> splitByChar(const std::string &Input,
                                       char SplitChar) {
    std::vector<std::string> Res;
    std::stringstream StrStream = std::stringstream(Input);
    std::string Line;
    while (std::getline(StrStream, Line, SplitChar)) {
      Res.push_back(Line);
    }
    return Res;
  }

  std::string getFileName(const std::string &Input) {
    std::vector<std::string> PathToFile = splitByChar(Input, '/');
    if (PathToFile.empty()) {
      return Input;
    }
    return PathToFile[(int)PathToFile.size() - 1];
  }

  void formatOutput() {
    std::vector<std::string> Lines = splitByChar(TempOutput, '\n');

    std::vector<std::string> AllFeatures =
        ProteanCollectFeatures::getAllFeatures();
    std::vector<std::unordered_map<std::string, std::string>> RowsFeatureValues;
    std::vector<std::string> IndexToFeature;
    std::unordered_map<std::string, std::string> ModuleFeaturesValues;
    bool ModuleLevel = true;

    for (std::string Line : Lines) {
      if (Line[0] == '-') {
        Line = Line.substr(1);
        std::vector<std::string> Features = splitByChar(Line, ',');
        IndexToFeature.clear();
        for (const std::string &Feature : Features) {
          IndexToFeature.push_back(Feature);
        }
      } else {
        std::vector<std::string> Values = splitByChar(Line, ',');
        int Idx = 0;
        std::unordered_map<std::string, std::string> FeatureValues;
        for (const std::string &Value : Values) {
          const std::string &Feature = IndexToFeature[Idx];
          FeatureValues[Feature] = Value;
          if (ModuleLevel) {
            ModuleFeaturesValues[Feature] = Value;
          }
          Idx++;
        }
        RowsFeatureValues.push_back(FeatureValues);

        if (ModuleLevel) {
          ModuleLevel = false;
        }
      }
    }
    std::string Out = "";
    if (isEmptyOutputFile()) {
      Out += "Module|Function|Callee|Caller|Loop,";
      for (const std::string &Feature : AllFeatures) {
        Out += Feature + ',';
      }
      Out.pop_back();
      Out += '\n';
    }

    for (auto &FeatureValues : RowsFeatureValues) {
      Out += getFileName(FeatureValues["Module"]) + '|' +
             FeatureValues["Function"] + '|' + FeatureValues["Callee"] + '|' +
             FeatureValues["Caller"] + '|' + FeatureValues["Loop"] + ',';
      for (const std::string &Feature : AllFeatures) {
        if (ModuleFeaturesValues.count(Feature)) {
          Out += ModuleFeaturesValues[Feature];
        } else if (FeatureValues.count(Feature)) {
          Out += FeatureValues[Feature];
        } else {
          Out += "0";
        }
        Out += ',';
      }
      Out.pop_back();
      Out += '\n';
    }

    TempOutput = Out;
  }

  void printOutput() { ModelDataCollector::setOutput(TempOutput); }

  bool getOnlyMandatory() { return OnlyMandatory; }

private:
  bool OnlyMandatory = false;
  std::string TempOutput = "";
};

static void
calculateFPIRelated(ProteanCollectFeatures &ACF,
                    const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateCallerBlockFreq(ProteanCollectFeatures &ACF,
                         const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateCallSiteHeight(ProteanCollectFeatures &ACF,
                        const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateConstantParam(ProteanCollectFeatures &ACF,
                       const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateCostEstimate(ProteanCollectFeatures &ACF,
                      const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateEdgeNodeCount(ProteanCollectFeatures &ACF,
                       const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateHotColdCallSite(ProteanCollectFeatures &ACF,
                         const ProteanCollectFeatures::FeatureInfo &Info);
static void calculateLoopLevel(ProteanCollectFeatures &ACF,
                               const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateMandatoryKind(ProteanCollectFeatures &ACF,
                       const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateMandatoryOnly(ProteanCollectFeatures &ACF,
                       const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateInlineCostFeatures(ProteanCollectFeatures &ACF,
                            const ProteanCollectFeatures::FeatureInfo &Info);
static void calculateProteanFIExtendedFeaturesFeatures(
    ProteanCollectFeatures &ACF,
    const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateIVValueFeatures(ProteanCollectFeatures &ACF,
                         const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateInnerOuterMostLoop(ProteanCollectFeatures &ACF,
                            const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateLoopInstFeatures(ProteanCollectFeatures &ACF,
                          const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateLoopHeight(ProteanCollectFeatures &ACF,
                    const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateLoopSetSize(ProteanCollectFeatures &ACF,
                     const ProteanCollectFeatures::FeatureInfo &Info);
static void calculateTripCount(ProteanCollectFeatures &ACF,
                               const ProteanCollectFeatures::FeatureInfo &Info);
void calculateLoopSize(ProteanCollectFeatures &ACF,
                       const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateIVValueFeatures(ProteanCollectFeatures &ACF,
                         const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateInnerOuterMostLoop(ProteanCollectFeatures &ACF,
                            const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateLoopInstFeatures(ProteanCollectFeatures &ACF,
                          const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateLoopHeight(ProteanCollectFeatures &ACF,
                    const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateLoopSetSize(ProteanCollectFeatures &ACF,
                     const ProteanCollectFeatures::FeatureInfo &Info);
static void calculateTripCount(ProteanCollectFeatures &ACF,
                               const ProteanCollectFeatures::FeatureInfo &Info);
void calculateLoopSize(ProteanCollectFeatures &ACF,
                       const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateIsIndirectCall(ProteanCollectFeatures &ACF,
                        const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateIsInInnerLoop(ProteanCollectFeatures &ACF,
                       const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateIsMustTailCall(ProteanCollectFeatures &ACF,
                        const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateIsTailCall(ProteanCollectFeatures &ACF,
                    const ProteanCollectFeatures::FeatureInfo &Info);
static void calculateOptCode(ProteanCollectFeatures &ACF,
                             const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateFunctionInfo(ProteanCollectFeatures &ACF,
                      const ProteanCollectFeatures::FeatureInfo &Info);
static void
calculateModuleInfoCount(ProteanCollectFeatures &ACF,
                         const ProteanCollectFeatures::FeatureInfo &Info);
// Register FeatureIdx -> Feature name
//          FeatureIdx -> Scope, Scope -> FeatureIdx
//          FeatureIdx -> Group, Group -> FeatureIdx
//          FeatureIdx -> Calculating function
#define REGISTER_NAME(INDEX_NAME, NAME)                                        \
  {ProteanCollectFeatures::FeatureIndex::INDEX_NAME, NAME}
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
        REGISTER_NAME(FunctionCount, "function_count"),
        REGISTER_NAME(TotalBBCount, "total_bb_count"),
        REGISTER_NAME(AverageBBPerFunction, "average_bb_per_function"),
        REGISTER_NAME(TotalInstructionCount, "total_instruction_count"),
        REGISTER_NAME(TotalFunctionCalls, "total_function_calls"),
        REGISTER_NAME(AverageCallsPerFunction, "average_calls_per_function"),
        REGISTER_NAME(MedianCallsPerFunction, "median_calls_per_function"),
        REGISTER_NAME(LoopCount, "loop_count"),
        REGISTER_NAME(TotalEdgeCount, "total_edge_count"),
        REGISTER_NAME(CriticalEdgeCount, "critical_edge_count"),
        REGISTER_NAME(GlobalVariableCount, "global_variable_count"),
        REGISTER_NAME(AverageInstructionsPerFunction,
                      "average_instructions_per_function"),
        REGISTER_NAME(AverageLoadInstructionsPerFunction,
                      "average_load_instructions_per_function"),
        REGISTER_NAME(AverageStoreInstructionsPerFunction,
                      "average_store_instructions_per_function"),
        REGISTER_NAME(SCCSize, "scc_size"),
        REGISTER_NAME(AverageComponentSize, "average_component_size"),
        REGISTER_NAME(NumOfFeatures, "num_features"),
        REGISTER_NAME(TripCount, "TripCount"),
        REGISTER_NAME(MaxTripCount, "MaxTripCount"),
        REGISTER_NAME(LoopSize, "Size"),
        REGISTER_NAME(InitialIVValueInt, "InitialIVValueInt"),
        REGISTER_NAME(FinalIVValueInt, "FinalIVValueInt"),
        REGISTER_NAME(StepValueInt, "StepValueInt"),
        REGISTER_NAME(NumPartitions, "NumPartitions"),
        REGISTER_NAME(IndVarSetSize, "IndVarSetSize"),
        REGISTER_NAME(AvgStoreSetSize, "AvgStoreSetSize"),
        REGISTER_NAME(AvgNumInsts, "AvgNumInsts"),
        REGISTER_NAME(NumLoadInstPerLoopNest, "NumLoadInstPerLoopNest"),
        REGISTER_NAME(NumStoreInstPerLoopNest, "NumStoreInstPerLoopNest"),
        REGISTER_NAME(TotLoopNestInstCount, "TotLoopNestInstCount"),
        REGISTER_NAME(AvgNumLoadInstPerLoopNest, "AvgNumLoadInstPerLoopNest"),
        REGISTER_NAME(NumLoadInstPerLoop, "NumLoadInstPerLoop"),
        REGISTER_NAME(NumStoreInstPerLoop, "NumStoreInstPerLoop"),
        REGISTER_NAME(TotLoopInstCount, "TotLoopInstCount"),
        REGISTER_NAME(AvgNumLoadInstPerLoop, "AvgNumLoadInstPerLoop"),
        REGISTER_NAME(TotBlocksPerLoop, "TotBlocksPerLoop"),
        REGISTER_NAME(IsInnerMostLoop, "IsInnerMostLoop"),
        REGISTER_NAME(IsOuterMostLoop, "IsOuterMostLoop"),
        REGISTER_NAME(MaxLoopHeight, "MaxLoopHeight"),
        REGISTER_NAME(IsFixedTripCount, "IsFixedTripCount"),
    };
#undef REGISTER_NAME

#define REGISTER_SCOPE(INDEX_NAME, NAME)                                       \
  {ProteanCollectFeatures::FeatureIndex::INDEX_NAME,                           \
   ProteanCollectFeatures::Scope::NAME}
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
        REGISTER_SCOPE(TripCount, Loop),
        REGISTER_SCOPE(MaxTripCount, Loop),
        REGISTER_SCOPE(IsFixedTripCount, Loop),
        REGISTER_SCOPE(LoopSize, Loop),
        REGISTER_SCOPE(InitialIVValueInt, Loop),
        REGISTER_SCOPE(FinalIVValueInt, Loop),
        REGISTER_SCOPE(StepValueInt, Loop),
        REGISTER_SCOPE(NumPartitions, Loop),
        REGISTER_SCOPE(IndVarSetSize, Loop),
        REGISTER_SCOPE(AvgStoreSetSize, Loop),
        REGISTER_SCOPE(AvgNumInsts, Loop),
        REGISTER_SCOPE(NumLoadInstPerLoopNest, Loop),
        REGISTER_SCOPE(NumStoreInstPerLoopNest, Loop),
        REGISTER_SCOPE(TotLoopNestInstCount, Loop),
        REGISTER_SCOPE(AvgNumLoadInstPerLoopNest, Loop),
        REGISTER_SCOPE(NumLoadInstPerLoop, Loop),
        REGISTER_SCOPE(NumStoreInstPerLoop, Loop),
        REGISTER_SCOPE(TotLoopInstCount, Loop),
        REGISTER_SCOPE(AvgNumLoadInstPerLoop, Loop),
        REGISTER_SCOPE(TotBlocksPerLoop, Loop),
        REGISTER_SCOPE(IsInnerMostLoop, Loop),
        REGISTER_SCOPE(IsOuterMostLoop, Loop),
        REGISTER_SCOPE(MaxLoopHeight, Loop),
        REGISTER_SCOPE(FunctionCount, Module),
        REGISTER_SCOPE(AverageBBPerFunction, Module),
        REGISTER_SCOPE(TotalBBCount, Module),
        REGISTER_SCOPE(TotalInstructionCount, Module),
        REGISTER_SCOPE(TotalFunctionCalls, Module),
        REGISTER_SCOPE(AverageCallsPerFunction, Module),
        REGISTER_SCOPE(MedianCallsPerFunction, Module),
        REGISTER_SCOPE(TotalEdgeCount, Module),
        REGISTER_SCOPE(CriticalEdgeCount, Module),
        REGISTER_SCOPE(GlobalVariableCount, Module),
        REGISTER_SCOPE(AverageInstructionsPerFunction, Module),
        REGISTER_SCOPE(AverageLoadInstructionsPerFunction, Module),
        REGISTER_SCOPE(AverageStoreInstructionsPerFunction, Module),
        REGISTER_SCOPE(SCCSize, Function),
        REGISTER_SCOPE(AverageComponentSize, Function),
        REGISTER_SCOPE(LoopCount, Module),
    };
#undef REGISTER_SCOPE

#define REGISTER_GROUP(INDEX_NAME, NAME)                                       \
  {ProteanCollectFeatures::FeatureIndex::INDEX_NAME,                           \
   ProteanCollectFeatures::GroupID::NAME}
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
        REGISTER_GROUP(TripCount, TripCountFeatures),
        REGISTER_GROUP(MaxTripCount, TripCountFeatures),
        REGISTER_GROUP(IsFixedTripCount, TripCountFeatures),
        REGISTER_GROUP(InitialIVValueInt, IVRelatedFeatures),
        REGISTER_GROUP(FinalIVValueInt, IVRelatedFeatures),
        REGISTER_GROUP(StepValueInt, IVRelatedFeatures),
        REGISTER_GROUP(NumPartitions, LoopSetSizeFeatures),
        REGISTER_GROUP(IndVarSetSize, LoopSetSizeFeatures),
        REGISTER_GROUP(AvgStoreSetSize, LoopSetSizeFeatures),
        REGISTER_GROUP(AvgNumInsts, LoopSetSizeFeatures),
        REGISTER_GROUP(NumLoadInstPerLoopNest, LoopInstFeatures),
        REGISTER_GROUP(NumStoreInstPerLoopNest, LoopInstFeatures),
        REGISTER_GROUP(TotLoopNestInstCount, LoopInstFeatures),
        REGISTER_GROUP(AvgNumLoadInstPerLoopNest, LoopInstFeatures),
        REGISTER_GROUP(NumLoadInstPerLoop, LoopInstFeatures),
        REGISTER_GROUP(NumStoreInstPerLoop, LoopInstFeatures),
        REGISTER_GROUP(TotLoopInstCount, LoopInstFeatures),
        REGISTER_GROUP(AvgNumLoadInstPerLoop, LoopInstFeatures),
        REGISTER_GROUP(TotBlocksPerLoop, LoopInstFeatures),
        REGISTER_GROUP(IsInnerMostLoop, InnerOuterFeatures),
        REGISTER_GROUP(IsOuterMostLoop, InnerOuterFeatures),
        REGISTER_GROUP(FunctionCount, ModuleInfoCount),
        REGISTER_GROUP(TotalBBCount, ModuleInfoCount),
        REGISTER_GROUP(AverageBBPerFunction, ModuleInfoCount),
        REGISTER_GROUP(TotalInstructionCount, ModuleInfoCount),
        REGISTER_GROUP(TotalFunctionCalls, ModuleInfoCount),
        REGISTER_GROUP(AverageCallsPerFunction, ModuleInfoCount),
        REGISTER_GROUP(MedianCallsPerFunction, ModuleInfoCount),
        REGISTER_GROUP(LoopCount, ModuleInfoCount),
        REGISTER_GROUP(TotalEdgeCount, ModuleInfoCount),
        REGISTER_GROUP(CriticalEdgeCount, ModuleInfoCount),
        REGISTER_GROUP(GlobalVariableCount, ModuleInfoCount),
        REGISTER_GROUP(AverageInstructionsPerFunction, ModuleInfoCount),
        REGISTER_GROUP(AverageLoadInstructionsPerFunction, ModuleInfoCount),
        REGISTER_GROUP(AverageStoreInstructionsPerFunction, ModuleInfoCount),
        REGISTER_GROUP(SCCSize, FunctionInfo),
        REGISTER_GROUP(AverageComponentSize, FunctionInfo),
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
  {ProteanCollectFeatures::FeatureIndex::INDEX_NAME, NAME}
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
        REGISTER_FUNCTION(TripCount, calculateTripCount),
        REGISTER_FUNCTION(MaxTripCount, calculateTripCount),
        REGISTER_FUNCTION(LoopSize, calculateLoopSize),
        REGISTER_FUNCTION(InitialIVValueInt, calculateIVValueFeatures),
        REGISTER_FUNCTION(FinalIVValueInt, calculateIVValueFeatures),
        REGISTER_FUNCTION(StepValueInt, calculateIVValueFeatures),
        REGISTER_FUNCTION(NumPartitions, calculateLoopSetSize),
        REGISTER_FUNCTION(IndVarSetSize, calculateLoopSetSize),
        REGISTER_FUNCTION(AvgStoreSetSize, calculateLoopSetSize),
        REGISTER_FUNCTION(AvgNumInsts, calculateLoopSetSize),
        REGISTER_FUNCTION(NumLoadInstPerLoopNest, calculateLoopInstFeatures),
        REGISTER_FUNCTION(NumStoreInstPerLoopNest, calculateLoopInstFeatures),
        REGISTER_FUNCTION(TotLoopNestInstCount, calculateLoopInstFeatures),
        REGISTER_FUNCTION(AvgNumLoadInstPerLoopNest, calculateLoopInstFeatures),
        REGISTER_FUNCTION(NumLoadInstPerLoop, calculateLoopInstFeatures),
        REGISTER_FUNCTION(NumStoreInstPerLoop, calculateLoopInstFeatures),
        REGISTER_FUNCTION(TotLoopInstCount, calculateLoopInstFeatures),
        REGISTER_FUNCTION(AvgNumLoadInstPerLoop, calculateLoopInstFeatures),
        REGISTER_FUNCTION(TotBlocksPerLoop, calculateLoopInstFeatures),
        REGISTER_FUNCTION(IsInnerMostLoop, calculateInnerOuterMostLoop),
        REGISTER_FUNCTION(IsOuterMostLoop, calculateInnerOuterMostLoop),
        REGISTER_FUNCTION(MaxLoopHeight, calculateLoopHeight),
        REGISTER_FUNCTION(IsFixedTripCount, calculateTripCount),
        REGISTER_FUNCTION(FunctionCount, calculateModuleInfoCount),
        REGISTER_FUNCTION(TotalBBCount, calculateModuleInfoCount),
        REGISTER_FUNCTION(AverageBBPerFunction, calculateModuleInfoCount),
        REGISTER_FUNCTION(TotalInstructionCount, calculateModuleInfoCount),
        REGISTER_FUNCTION(TotalFunctionCalls, calculateModuleInfoCount),
        REGISTER_FUNCTION(AverageCallsPerFunction, calculateModuleInfoCount),
        REGISTER_FUNCTION(MedianCallsPerFunction, calculateModuleInfoCount),
        REGISTER_FUNCTION(LoopCount, calculateModuleInfoCount),
        REGISTER_FUNCTION(TotalEdgeCount, calculateModuleInfoCount),
        REGISTER_FUNCTION(CriticalEdgeCount, calculateModuleInfoCount),
        REGISTER_FUNCTION(GlobalVariableCount, calculateModuleInfoCount),
        REGISTER_FUNCTION(AverageInstructionsPerFunction,
                          calculateModuleInfoCount),
        REGISTER_FUNCTION(AverageLoadInstructionsPerFunction,
                          calculateModuleInfoCount),
        REGISTER_FUNCTION(AverageStoreInstructionsPerFunction,
                          calculateModuleInfoCount),
        REGISTER_FUNCTION(SCCSize, calculateFunctionInfo),
        REGISTER_FUNCTION(AverageComponentSize, calculateFunctionInfo),
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

std::vector<std::string> ProteanCollectFeatures::getAllFeatures() {
  std::vector<std::string> Res;
  std::vector<std::string> ModuleLevel;
  std::vector<std::string> FunctionLevel;
  std::vector<std::string> LoopLevel;

  for (const auto &Info : ProteanCollectFeatures::FeatureIndexToName) {
    auto ScopeIter = FeatureIndexToScope.find(Info.first);
    if (ScopeIter == FeatureIndexToScope.end()) {
      continue;
    }
    if (ScopeIter->second == ProteanCollectFeatures::Scope::Module) {
      ModuleLevel.push_back(Info.second);
    } else if (ScopeIter->second == ProteanCollectFeatures::Scope::Function) {
      FunctionLevel.push_back("callee_" + Info.second);
      FunctionLevel.push_back("caller_" + Info.second);
    } else if (ScopeIter->second == ProteanCollectFeatures::Scope::CallSite) {
      FunctionLevel.push_back(Info.second);
    } else if (ScopeIter->second == ProteanCollectFeatures::Scope::Loop) {
      LoopLevel.push_back(Info.second);
    }
  }
  Res.insert(Res.end(), ModuleLevel.begin(), ModuleLevel.end());
  Res.insert(Res.end(), FunctionLevel.begin(), FunctionLevel.end());
  Res.insert(Res.end(), LoopLevel.begin(), LoopLevel.end());
  return Res;
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
  auto *IA = Info.II.IA;

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

void calculateFunctionInfo(ProteanCollectFeatures &ACF,
                           const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         ProteanCollectFeatures::getFeatureGroup(Info.Idx) ==
             ProteanCollectFeatures::GroupID::FunctionInfo);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::GroupID::FunctionInfo))
    return;

  auto *F = Info.SI.F;

  int SCCSize = 0;
  double AverageComponentSize = 0;
  for (scc_iterator<Function *> I = scc_begin(F); I != scc_end(F); ++I) {
    SCCSize += 1;
    AverageComponentSize += std::distance(I->begin(), I->end());
  }
  AverageComponentSize /= SCCSize;

  std::string SCCSizeStr = std::to_string(SCCSize);
  std::string AverageComponentSizeStr = std::to_string(AverageComponentSize);

  ACF.setFeatureValueAndInfo(ProteanCollectFeatures::FeatureIndex::SCCSize,
                             Info, SCCSizeStr);
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::AverageComponentSize, Info,
      AverageComponentSizeStr);
}

void calculateModuleInfoCount(ProteanCollectFeatures &ACF,
                              const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         ProteanCollectFeatures::getFeatureGroup(Info.Idx) ==
             ProteanCollectFeatures::GroupID::ModuleInfoCount);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::GroupID::ModuleInfoCount))
    return;

  auto *M = Info.SI.M;
  auto *FAM = Info.Managers.FAM;

  assert(M && FAM && "Module or FAM are nullptr");

  int FunctionCount = 0;
  int BBCount = 0;
  int InstructionCount = 0;
  int LoopCount = 0;
  int LoadInstructionCount = 0;
  int StoreInstructionCount = 0;
  int CriticalEdgeCount = 0;
  int TotalEdgeCount = 0;

  std::vector<int> CallsPerFunction;
  for (Function &F : *M) {
    if (!F.isDeclaration()) {
      // Function Info
      FunctionCount++;
      int FunctionCalls = 0;
      // Loop Count
      LoopInfo &LI = FAM->getResult<LoopAnalysis>(F);
      LoopCount += std::distance(LI.begin(), LI.end());
      for (BasicBlock &BB : F) {
        InstructionCount += std::distance(BB.begin(), BB.end());
        TotalEdgeCount +=
            std::distance(successors(&BB).begin(), successors(&BB).end());
        for (Instruction &I : BB) {
          // Load and Store Count
          if (isa<LoadInst>(I)) {
            LoadInstructionCount += 1;
          }
          if (isa<StoreInst>(I)) {
            StoreInstructionCount += 1;
          }
          // Function Calls Count
          if (auto *CB = dyn_cast<CallBase>(&I)) {
            if (Function *Callee = CB->getCalledFunction()) {
              if (!Callee->isDeclaration()) {
                FunctionCalls += 1;
              }
            }
          }
        }

        int PredCount = std::distance(pred_begin(&BB), pred_end(&BB));
        // BB must have >1 predecessor
        if (PredCount > 1) {
          for (BasicBlock *Pred : predecessors(&BB)) {
            // Pred must have >1 successor
            if (Pred->getSingleSuccessor() == nullptr) {
              CriticalEdgeCount += 1;
            }
          }
        }
        BBCount += std::distance(F.begin(), F.end());
        CallsPerFunction.push_back(FunctionCalls);
      }
    }
  }
  // Function Calls Analysis
  std::sort(CallsPerFunction.begin(), CallsPerFunction.end());
  int MedianCallsPerFunction = 0;
  if (!CallsPerFunction.empty()) {
    MedianCallsPerFunction = CallsPerFunction[FunctionCount / 2];
  }
  int TotalFunctionCalls = 0;
  for (int Calls : CallsPerFunction) {
    TotalFunctionCalls += Calls;
  }
  // Average per Function Analysis
  double AverageCallsPerFunction = 1.0 * TotalFunctionCalls / FunctionCount;
  double AverageBBPerFunction = 1.0 * BBCount / FunctionCount;
  double AverageInstructionCount = 1.0 * InstructionCount / FunctionCount;
  double AverageLoadInstructionCount =
      1.0 * LoadInstructionCount / FunctionCount;
  double AverageStoreInstructionCount =
      1.0 * StoreInstructionCount / FunctionCount;
  int GlobalVariableCount =
      std::distance(M->globals().begin(), M->globals().end());

  std::string FunctionCountStr = std::to_string(FunctionCount);
  std::string BBCountStr = std::to_string(BBCount);
  std::string AverageBBPerFunctionStr = std::to_string(AverageBBPerFunction);
  std::string InstructionCountStr = std::to_string(InstructionCount);
  std::string TotalFunctionCallsStr = std::to_string(TotalFunctionCalls);
  std::string AverageCallsPerFunctionStr =
      std::to_string(AverageCallsPerFunction);
  std::string MedianCallsPerFunctionStr =
      std::to_string(MedianCallsPerFunction);
  std::string LoopCountStr = std::to_string(LoopCount);
  std::string TotalEdgeCountStr = std::to_string(TotalEdgeCount);
  std::string CriticalEdgeCountStr = std::to_string(CriticalEdgeCount);
  std::string GlobalVariableCountStr = std::to_string(GlobalVariableCount);
  std::string AverageInstructionCountStr =
      std::to_string(AverageInstructionCount);
  std::string LoadInstructionCountStr =
      std::to_string(AverageLoadInstructionCount);
  std::string StoreInstructionCountStr =
      std::to_string(AverageStoreInstructionCount);

  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::FunctionCount, Info,
      FunctionCountStr);
  ACF.setFeatureValueAndInfo(ProteanCollectFeatures::FeatureIndex::TotalBBCount,
                             Info, BBCountStr);
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::AverageBBPerFunction, Info,
      AverageBBPerFunctionStr);
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::TotalInstructionCount, Info,
      InstructionCountStr);
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::TotalFunctionCalls, Info,
      TotalFunctionCallsStr);
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::AverageCallsPerFunction, Info,
      AverageCallsPerFunctionStr);
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::MedianCallsPerFunction, Info,
      MedianCallsPerFunctionStr);
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::AverageInstructionsPerFunction,
      Info, AverageInstructionCountStr);
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::AverageLoadInstructionsPerFunction,
      Info, LoadInstructionCountStr);
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::AverageStoreInstructionsPerFunction,
      Info, StoreInstructionCountStr);
  ACF.setFeatureValueAndInfo(ProteanCollectFeatures::FeatureIndex::LoopCount,
                             Info, LoopCountStr);
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::TotalEdgeCount, Info,
      TotalEdgeCountStr);
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::CriticalEdgeCount, Info,
      CriticalEdgeCountStr);
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::GlobalVariableCount, Info,
      GlobalVariableCountStr);
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
      std::to_string((int)Info.II.MandatoryOnly));
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

void calculateIVValueFeatures(ProteanCollectFeatures &ACF,
                              const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         ProteanCollectFeatures::getFeatureGroup(Info.Idx) ==
             ProteanCollectFeatures::GroupID::IVRelatedFeatures);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::GroupID::IVRelatedFeatures))
    return;

  auto *AR = Info.Managers.AR;
  auto *L = Info.SI.L;

  assert(AR && L && "AR or L is nullptr");

  auto &SE = AR->SE;
  auto *IndVar = L->getInductionVariable(SE);

  int InitialIVValueInt = 0, FinalIVValueInt = 0, StepValueInt = 0;
  if (auto LoopBound = L->getBounds(SE)) {
    if (auto Bound = LoopBound->getBounds(*L, *IndVar, SE)) {
      Value &InitialIVValue = Bound->getInitialIVValue();
      if (ConstantInt *CI = dyn_cast_or_null<ConstantInt>(&InitialIVValue))
        InitialIVValueInt = CI->getSExtValue();
      auto &FinalIVValue = Bound->getFinalIVValue();
      if (auto CI = dyn_cast_or_null<ConstantInt>(&FinalIVValue))
        FinalIVValueInt = CI->getSExtValue();
      auto *StepValue = Bound->getStepValue();
      if (auto CI = dyn_cast_or_null<ConstantInt>(StepValue))
        StepValueInt = CI->getSExtValue();
    }
  }

  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::InitialIVValueInt, Info,
      std::to_string(InitialIVValueInt));
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::FinalIVValueInt, Info,
      std::to_string(FinalIVValueInt));
  ACF.setFeatureValueAndInfo(ProteanCollectFeatures::FeatureIndex::StepValueInt,
                             Info, std::to_string(StepValueInt));
}

void calculateInnerOuterMostLoop(
    ProteanCollectFeatures &ACF,
    const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         ProteanCollectFeatures::getFeatureGroup(Info.Idx) ==
             ProteanCollectFeatures::GroupID::InnerOuterFeatures);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::GroupID::InnerOuterFeatures))
    return;

  auto *L = Info.SI.L;

  assert(L && "L is nullptr");

  bool IsInnerMostLoop = L->isInnermost();
  bool IsOuterMostLoop = L->isOutermost();
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::IsInnerMostLoop, Info,
      std::to_string(IsInnerMostLoop));
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::IsOuterMostLoop, Info,
      std::to_string(IsOuterMostLoop));
}

void calculateLoopInstFeatures(
    ProteanCollectFeatures &ACF,
    const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         ProteanCollectFeatures::getFeatureGroup(Info.Idx) ==
             ProteanCollectFeatures::GroupID::LoopInstFeatures);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::GroupID::LoopInstFeatures))
    return;

  auto *L = Info.SI.L;

  assert(L && "L is nullptr");

  int TotLoopNestInstCount = 0;
  int NumLoadInstPerLoopNest = 0, NumStoreInstPerLoopNest = 0;
  float AvgNumLoadInstPerLoopNest = 0;
  int TotBlocksPerLoopNest = 0;
  for (auto *LB : L->getBlocks()) {
    TotBlocksPerLoopNest++;
    for (Instruction &Inst : *LB) {
      TotLoopNestInstCount++;
      if (isa<LoadInst>(Inst))
        NumLoadInstPerLoopNest++;
      if (isa<StoreInst>(Inst))
        NumStoreInstPerLoopNest++;
    }
  }

  AvgNumLoadInstPerLoopNest =
      static_cast<float>(NumLoadInstPerLoopNest) / TotLoopNestInstCount;
  // Local loop info helps with characterizing only the current loop behaviour
  int TotLoopInstCount = 0;
  int NumLoadInstPerLoop = 0, NumStoreInstPerLoop = 0;
  float AvgNumLoadInstPerLoop = 0;
  int TotBlocksPerLoop = 0;
  // If block(s) of the current loop wasn't inside the vector of all subloops
  // of the loop, we count the stats (identifying only the current loop)
  std::vector<Loop *> SL = L->getSubLoops();
  if (!SL.empty()) {
    for (auto *LB : L->getBlocks()) {
      for (auto *BBSL : SL) {
        if (!BBSL->contains(LB)) {
          TotBlocksPerLoop++;
          for (Instruction &Inst : *LB) {
            TotLoopInstCount++;
            if (isa<LoadInst>(Inst))
              NumLoadInstPerLoop++;
            if (isa<StoreInst>(Inst))
              NumStoreInstPerLoop++;
          }
        }
      }
    }

    AvgNumLoadInstPerLoop =
        static_cast<float>(NumLoadInstPerLoop) / TotLoopInstCount;
  } else {
    // If a loop doesn't have nested loops
    TotLoopInstCount = TotLoopNestInstCount;
    NumLoadInstPerLoop = NumLoadInstPerLoopNest;
    NumStoreInstPerLoop = NumStoreInstPerLoopNest;
    AvgNumLoadInstPerLoop = AvgNumLoadInstPerLoopNest;
    TotBlocksPerLoop = TotBlocksPerLoopNest;
  }

  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::TotLoopInstCount, Info,
      std::to_string(TotLoopInstCount));
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::NumLoadInstPerLoop, Info,
      std::to_string(NumLoadInstPerLoop));
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::NumStoreInstPerLoop, Info,
      std::to_string(NumStoreInstPerLoop));
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::AvgNumLoadInstPerLoop, Info,
      std::to_string(AvgNumLoadInstPerLoop));
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::TotBlocksPerLoop, Info,
      std::to_string(TotBlocksPerLoop));
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::TotLoopNestInstCount, Info,
      std::to_string(TotLoopNestInstCount));
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::NumLoadInstPerLoopNest, Info,
      std::to_string(NumLoadInstPerLoopNest));
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::NumStoreInstPerLoopNest, Info,
      std::to_string(NumStoreInstPerLoopNest));
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::AvgNumLoadInstPerLoopNest, Info,
      std::to_string(AvgNumLoadInstPerLoopNest));
}

void calculateLoopHeight(ProteanCollectFeatures &ACF,
                         const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ProteanCollectFeatures::FeatureIndex::MaxLoopHeight);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::FeatureIndex::MaxLoopHeight))
    return;

  auto *L = Info.SI.L;

  assert(L && "L is nullptr");

  int MaxLoopHeight = -1;
  std::queue<Loop *> SLoops;
  SLoops.push(L);
  // InnerMostLoops have MaxLoopHeight of 0
  while (!SLoops.empty()) {
    unsigned Size = SLoops.size();
    for (unsigned I = 0; I < Size; ++I) {
      Loop *VisitingLoop = SLoops.back();
      SLoops.pop();
      for (auto *ChildLoop : VisitingLoop->getSubLoops())
        SLoops.push(ChildLoop);
    }
    ++MaxLoopHeight;
  }

  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::MaxLoopHeight, Info,
      std::to_string(MaxLoopHeight));
}

void calculateLoopSetSize(ProteanCollectFeatures &ACF,
                          const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         ProteanCollectFeatures::getFeatureGroup(Info.Idx) ==
             ProteanCollectFeatures::GroupID::LoopSetSizeFeatures);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::GroupID::LoopSetSizeFeatures))
    return;

  auto *AR = Info.Managers.AR;
  Loop *L = Info.SI.L;

  assert(AR && L && "AR or L is nullptr");

  ScalarEvolution *SE = &AR->SE;
  LoopInfo *LI = &AR->LI;
  auto *AA = &AR->AA;
  DominatorTree *DT = &AR->DT;
  auto *TLI = &AR->TLI;
  auto *TTI = &AR->TTI;
  LoopAccessInfoManager LAIs(*SE, *AA, *DT, *LI, TTI, TLI);
  const LoopAccessInfo *NewLAI = &LAIs.getInfo(*L);
  LoopPartitionGraph LPG(L, SE, DT, LI, NewLAI);

  int NumPartitions = 0;
  float AvgStoreSetSize = 0, AvgNumInsts = 0;
  size_t IndVarSetSize = 0;
  if (!LPG.createLoopPartitions()) {
    size_t TotalStoreSetSize = 0, TotalNumInsts = 0;
    NumPartitions = LPG.getNumNodes();
    IndVarSetSize = LPG.getIndVarSet().size();
    for (auto &N : LPG.getNodes()) {
      LoopTools::LoopPartition *LP = N.get();
      TotalNumInsts += LP->getInputInstrSet().size();
      TotalStoreSetSize += LP->getStoreSet().size();
    }
    TotalNumInsts += TotalStoreSetSize;

    if (NumPartitions) {
      AvgStoreSetSize = static_cast<float>(TotalStoreSetSize) / NumPartitions;
      AvgNumInsts = static_cast<float>(TotalNumInsts) / NumPartitions;
    }
  }

  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::AvgStoreSetSize, Info,
      std::to_string(AvgStoreSetSize));
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::IndVarSetSize, Info,
      std::to_string(IndVarSetSize));
  ACF.setFeatureValueAndInfo(ProteanCollectFeatures::FeatureIndex::AvgNumInsts,
                             Info, std::to_string(AvgNumInsts));
  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::NumPartitions, Info,
      std::to_string(NumPartitions));
}

void calculateTripCount(ProteanCollectFeatures &ACF,
                        const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         ProteanCollectFeatures::getFeatureGroup(Info.Idx) ==
             ProteanCollectFeatures::GroupID::TripCountFeatures);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::GroupID::TripCountFeatures))
    return;

  auto *L = Info.SI.L;
  auto *AR = Info.Managers.AR;

  assert(AR && L && "AR or L is nullptr");

  auto &SE = AR->SE;
  unsigned TripCount = 0;
  SmallVector<BasicBlock *, 8> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);
  for (BasicBlock *ExitingBlock : ExitingBlocks)
    if (unsigned TC = SE.getSmallConstantTripCount(L, ExitingBlock))
      if (!TripCount || TC < TripCount)
        TripCount = TC;

  unsigned MaxTripCount = 0;
  if (!TripCount) {
    MaxTripCount = SE.getSmallConstantMaxTripCount(L);
  }
  bool IsFixedTripCount = (TripCount != 0) ? true : false;

  ACF.setFeatureValueAndInfo(
      ProteanCollectFeatures::FeatureIndex::IsFixedTripCount, Info,
      std::to_string(IsFixedTripCount));
  ACF.setFeatureValueAndInfo(ProteanCollectFeatures::FeatureIndex::TripCount,
                             Info, std::to_string(TripCount));
  ACF.setFeatureValueAndInfo(ProteanCollectFeatures::FeatureIndex::MaxTripCount,
                             Info, std::to_string(MaxTripCount));
}

void calculateLoopSize(ProteanCollectFeatures &ACF,
                       const ProteanCollectFeatures::FeatureInfo &Info) {
  assert(Info.Idx == ProteanCollectFeatures::FeatureIndex::NumOfFeatures ||
         Info.Idx == ProteanCollectFeatures::FeatureIndex::LoopSize);

  // Check if we already calculated the values.
  if (ACF.containsFeature(ProteanCollectFeatures::FeatureIndex::LoopSize))
    return;

  auto *L = Info.SI.L;
  auto *AR = Info.Managers.AR;

  assert(AR && L && "AR or L is nullptr");

  auto &TTI = AR->TTI;
  AssumptionCache &AC = AR->AC;
  SmallPtrSet<const Value *, 32> EphValues;
  CodeMetrics::collectEphemeralValues(L, &AC, EphValues);
  unsigned BEInsns = 2;
  CodeMetrics Metrics;
  for (BasicBlock *BB : L->blocks())
    Metrics.analyzeBasicBlock(BB, TTI, EphValues);
  InstructionCost LoopSize = Metrics.NumInsts;

  // Don't allow an estimate of size zero. This would allows unrolling of loops
  // with huge iteration counts, which is a compile time problem even if it's
  // not a problem for code quality. Also, the code using this size may assume
  // that each loop has at least three instructions (likely a conditional
  // branch, a comparison feeding that branch, and some kind of loop increment
  // feeding that comparison instruction).
  if (LoopSize.isValid() && *LoopSize.getValue() < BEInsns + 1) {
    // This is an open coded max() on InstructionCost
    LoopSize = BEInsns + 1;
  }

  ACF.setFeatureValueAndInfo(ProteanCollectFeatures::FeatureIndex::LoopSize,
                             Info, std::to_string(*LoopSize.getValue()));
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
    if (L->isInnermost() && L->contains(CB)) {
      CallSiteInInnerLoop = true;
      break;
    }
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
    LLVM_DEBUG(dbgs() << "Protean Feature " << getFeatureName(Idx) << ": "
                      << FeatureToValue[Idx] << "\n");
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
  }
  return It->second;
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
  LLVM_DEBUG(dbgs() << "Collecting Inlining Features\n");
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
  ModelDataProteanCollector MDC(OS, /*OnlyMandatory*/ false, ProteanModelFile,
                                /*TempOutput*/ "");

  // Module level collection
  MDC.collectFeatures(&M, IA, &FAM, &AM);
  if (FeatureDump) {
    MDC.updateOutput(/*PrintHeader*/ true, &M, nullptr, /*CB*/ nullptr,
                     /*L*/ nullptr);
    MDC.updateOutput(/*PrintHeader*/ false, &M, nullptr, /*CB*/ nullptr,
                     /*L*/ nullptr);
  }
  // Call base collection
  bool CallBaseIsCollected = false;
  for (Function &F : M) {
    if (F.isDeclaration()) {
      continue;
    }
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *CB = dyn_cast<CallBase>(&I)) {
          if (Function *Callee = CB->getCalledFunction()) {
            if (!Callee->isDeclaration()) {
              MDC.collectFeatures(CB, IA, &FAM, &AM);
              if (FeatureDump) {
                if (!CallBaseIsCollected) {
                  CallBaseIsCollected = true;
                  MDC.updateOutput(/*PrintHeader*/ true, &M, &F, CB,
                                   /*L*/ nullptr);
                }
                MDC.updateOutput(/*PrintHeader*/ false, &M, &F, CB,
                                 /*L*/ nullptr);
              }
            }
          }
        }
      }
    }
  }
  // Loop Collection
  bool LoopIsCollected = false;
  for (Function &F : M) {
    if (F.isDeclaration()) {
      continue;
    }
    auto &LI = FAM.getResult<LoopAnalysis>(F);
    auto &LAM = FAM.getResult<LoopAnalysisManagerFunctionProxy>(F).getManager();
    auto &DT = FAM.getResult<DominatorTreeAnalysis>(F);
    auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
    auto &LR = FAM.getResult<LoopReuseAnalysisWrapper>(F);
    auto &AA = FAM.getResult<AAManager>(F);
    auto &AC = FAM.getResult<AssumptionAnalysis>(F);
    auto &TTI = FAM.getResult<TargetIRAnalysis>(F);
    auto &TLI = FAM.getResult<TargetLibraryAnalysis>(F);

    LoopStandardAnalysisResults LSAR = {AA,  AC,  DT,      LI,      LR,     SE,
                                        TLI, TTI, nullptr, nullptr, nullptr};
    for (Loop *L : LI) {
      MDC.collectLoopFeatures(L, &LSAR, &LAM);
      if (FeatureDump) {
        if (!LoopIsCollected) {
          LoopIsCollected = true;
          MDC.updateOutput(/*PrintHeader*/ true, &M, &F, /*CB*/ nullptr, L);
        }
        MDC.updateOutput(/*PrintHeader*/ false, &M, &F, /*CB*/ nullptr, L);
      }
    }
  }
  MDC.formatOutput();
  MDC.printOutput();
  return PreservedAnalyses::all();
}

PreservedAnalyses LoopCollectFeaturesPass::run(Loop &L, LoopAnalysisManager &AM,
                                               LoopStandardAnalysisResults &AR,
                                               LPMUpdater &U) {
  LLVM_DEBUG(dbgs() << "Collecting Loop Features");
  std::error_code EC;
  raw_fd_ostream RawOS(ProteanLoopModelFile.getValue(), EC,
                       sys::fs::CD_OpenAlways, sys::fs::FA_Write,
                       sys::fs::OF_Append);
  formatted_raw_ostream OS(RawOS);
  ModelDataProteanCollector MDC(OS, /*OnlyMandatory*/ false,
                                ProteanLoopModelFile, /*TempOutput*/ "");
  Function *F = L.getHeader()->getParent();
  Module *M = F->getParent();
  MDC.collectLoopFeatures(&L, &AR, &AM);
  if (MDC.isEmptyOutputFile())
    MDC.updateOutput(/*PrintHeader*/ true, M, F, /*CB*/ nullptr, &L);
  if (FeatureDump)
    MDC.updateOutput(/*PrintHeader*/ false, M, F, /*CB*/ nullptr, &L);
  MDC.printOutput();
  return PreservedAnalyses::all();
}

ProteanCollectFeatures::FunctionFeaturesCache
    ProteanCollectFeatures::FeatureCache;
ProteanCollectFeatures::FunctionAnalysisMap
    ProteanCollectFeatures::FunctionAnalysisCache;
} // namespace llvm
