//===- ACPOBranchWeightModel.h - ACPO Branch weight moel ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Expectations.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2025. Huawei Technologies Co., Ltd. All rights reserved.
//
//===----------------------------------------------------------------------===//
//
// This pass adds the branch weight metadata.
//
//===----------------------------------------------------------------------===//
#if defined(ENABLE_ACPO)

#include "llvm/Transforms/Utils/ACPOBranchWeightModel.h"
#include "llvm/Analysis/ACPOBWModel.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Analysis/ModelDataCollector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"
#include <unistd.h>

using namespace llvm;

#define DEBUG_TYPE "acpo-branch-weight-model"

namespace llvm {

cl::opt<bool> EnableACPOBWModel("use-acpo-bw-model-new-pass", cl::Hidden,cl::init(false), cl::desc("Enable ACPO branch weight model."));

static cl::list<std::string> ExcludedModuleList("exclude-bw-modules", cl::Hidden, cl::CommaSeparated, cl::desc("Comma separated list of functions that will be excluded."));

} // namespace llvm

static cl::opt<std::string>
    BWACPODumpFile("branch-weight-acpo-dump-file-new-pass",
                   cl::init("bw-acpo-data.csv"), cl::Hidden,
                   cl::desc("Name of a file to dump branch weight data."));

enum BWModelType { acpo };

static cl::opt<BWModelType> ACPOBWModelType(
    "acpo-bw-model-type-new-pass", cl::desc("Choose acpo bw model type:"),
    cl::init(acpo),
    cl::values(clEnumVal(acpo, "Use ACPO branch weight ML model")));

namespace {
// Class for collecting ACPO features
class ModelDataAI4CBWCollector : public ModelDataCollector {
public:
  ModelDataAI4CBWCollector(formatted_raw_ostream &OS, std::string OutputFileName) : ModelDataCollector(OS, OutputFileName) {}
  
  bool collectFeatures(BasicBlock &BB, BasicBlock *Succ, FunctionAnalysisManager *FAM) {
    Function *F = BB.getParent();
    Module *M = F->getParent();
    if (!FAM || !M || !F) {
      errs() << "One of Module, Function or FAM is nullptr\n";
      return false;
    }

    resetRegisteredFeatures();
    ACPOCollectFeatures::FeatureInfo GlobalFeatureInfo {
      ACPOCollectFeatures::FeatureIndex::NumOfFeatures,
      {FAM, nullptr},
      {F, nullptr, &BB, M, nullptr, Succ}};

    registerFeature({ACPOCollectFeatures::Scope::Function}, GlobalFeatureInfo);
    registerFeature({ACPOCollectFeatures::Scope::BasicBlock}, GlobalFeatureInfo);
    registerFeature({ACPOCollectFeatures::Scope::Edge}, GlobalFeatureInfo);
    ModelDataCollector::collectFeatures();
    return true;
  }

  void printBranchWeights(Instruction *TI, unsigned Weight) {
    BasicBlock *BB = TI->getParent();
    Function *F = BB->getParent();
    Module *M = F->getParent();
    std::string Out = "";

    for(unsigned I = 0, E = Features.size(); I != E; ++I) {
        if (I)
          Out += ",";
        Out += Features.at(I).second;
    }

    Out += "," + M->getName().str() + ","+ F->getName().str() + "," + BB->getName().str();
    Out += "," + std::to_string(Weight);
    Out += "\n";
    ModelDataCollector::setOutput(Out);
    return;
  }
};
} // end anonymous namespace

// Only enable the model for 920B
static bool isCPUTarget920B(Function &F) {
  const AttributeList &Attrs = F.getAttributes();
  if (!Attrs.hasFnAttrs()) return false;

  AttributeSet AS = Attrs.getFnAttrs();
  for (const Attribute &Attr : AS) {
    if (Attr.isStringAttribute()) {
      StringRef AttrStr = Attr.getValueAsString();
      if (AttrStr.contains("hip09")) {
        return true;
      }
    }
  }

  return false;
}

static bool checkMismatchOperandNum(Instruction *I, MDNode *MD) {
  if (isa<InvokeInst>(I)) {
    return (MD->getNumOperands() == 2 || MD->getNumOperands() == 3);
  }

unsigned ExpectedNumOperands = 0;
  if (BranchInst *BI = dyn_cast<BranchInst>(I))
    ExpectedNumOperands = BI->getNumSuccessors();
  else if (SwitchInst *SI = dyn_cast<SwitchInst>(I))
    ExpectedNumOperands = SI->getNumSuccessors();
  else if (isa<CallInst>(I))
    ExpectedNumOperands = 1;
  else if (IndirectBrInst *IBI = dyn_cast<IndirectBrInst>(I))
    ExpectedNumOperands = IBI->getNumDestinations();
  else if (isa<SelectInst>(I))
    ExpectedNumOperands = 2;
  else if (CallBrInst *CI = dyn_cast<CallBrInst>(I))
    ExpectedNumOperands = CI->getNumSuccessors();

  return (MD->getNumOperands() == 1 + ExpectedNumOperands);
}

bool ACPOBranchWeightModelPass::applyBranchWeightUsingACPOModel(Module &M, ModuleAnalysisManager &MAM) {
  std::error_code EC;
  raw_fd_ostream RawOS(BWACPODumpFile, EC, sys::fs::CD_OpenAlways, sys::fs::FA_Write, sys::fs::OF_Append);

  if (EC) {
    errs() << "Could not create/open feature dump file: " << EC.message() << '\n';
    return false;
  }
  formatted_raw_ostream OS(RawOS);
  ModelDataAI4CBWCollector MDC(OS, BWACPODumpFile);
  FunctionAnalysisManager &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  SmallVector<unsigned, 4> Weights;
  bool Changed = false;
  for (Function &F : M) {
    auto &ORE = FAM.getResult<OptimizationRemarkEmitterAnalysis>(F);
    std::unique_ptr<ACPOBWModel> BW = std::make_unique<ACPOBWModel>(&(F.getContext()), &ORE);

    for (BasicBlock &BB : F) {
      Weights.clear();
      Instruction *TI = BB.getTerminator();
      for (unsigned I = 0, E = TI->getNumSuccessors(); I != E; ++I) {
        bool printData = MDC.collectFeatures(BB, TI->getSuccessor(I), &FAM);
        std::vector<std::pair<std::string, std::string>> Features = MDC.getFeatures();
        BW->setMLCustomFeatures(Features);
        if(MDC.isEmptyOutputFile()) {
          MDC.printRow(true);
        }
        MDC.printRow();
        std::unique_ptr<ACPOAdvice> Advice = BW->getAdvice();
        Constant *Val = Advice->getField("BW-BranchWright");
        assert(Val != nullptr);
        assert(isa<ConstantInt>(Val));
        ConstantInt *ACPOBW = dyn_cast<ConstantInt>(Val);
        int64_t BranchWeight = ACPOBW->getSExtValue();
        if (BranchWeight != 100) Weights.push_back(BranchWeight);
      }

      // Create and add meta data
      if (Weights.empty()) {
        LLVM_DEBUG(dbgs() << "No weight data. Skipping.");
      } else {
        MDBuilder MDB(F.getContext());
        MDNode *MDWeight = MDB.createBranchWeights(Weights);
        LLVM_DEBUG(dbgs() << "Instruction before adding metadata" << *TI << "\n");
        LLVM_DEBUG(dbgs() << "Metadata node " << *MDWeight << "\n");
        if (!checkMismatchOperandNum(TI, MDWeight)) {
          LLVM_DEBUG(dbgs() << "Mismatch operand number. Skipping.\n");
          continue;
        }
        TI->setMetadata(LLVMContext::MD_prof, MDWeight);
        LLVM_DEBUG(dbgs() << "Instruction after adding metadata" << *TI < "\n");
        Changed = true;
      }
    }
  }
  return Changed;
}

PreservedAnalyses ACPOBranchWeightModelPass::run(Module &M, ModuleAnalysisManager &MAM) {
  if (!EnableACPOBWModel) return PreservedAnalyses::all();

  std::unordered_set<std::string> ExcludedModules(ExcludedModuleList.begin(), ExcludedModuleList.end());

  if (ExcludedModules.count(M.getName().str())) return PreservedAnalyses::all();

  LLVM_DEBUG(dbgs() << "Using ACPO Branch Weight Model - " << M.getName() << '\n');

  bool Changed = false;

  switch (ACPOBWModelType) {
    case acpo:
      Changed = applyBranchWeightUsingACPOModel(M, MAM);
      break;
    default:
      LLVM_DEBUG(errs() << "Invalid branch weight model type\n");
      break;
  }

  if (!Changed) {
    return PreservedAnalyses::all();
  } else {
    return PreservedAnalyses::none();
  }
}
#endif // ENABLE_ACPO
