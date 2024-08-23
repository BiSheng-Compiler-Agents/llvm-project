//===- DumpFeature.cpp - DumpFeature implementation -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements dumping features for functions in an scc.
//
//===----------------------------------------------------------------------===//

#if defined(ENABLE_ACPO)
#include "llvm/Analysis/DumpFeature.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/CallHeight.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCAsmLayout.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <deque>
#include <vector>

using namespace llvm;

bool EnableFeatureDump;
static cl::opt<bool, true> EnableFeatureDumpFlag(
    "enable-feature-dump", cl::location(EnableFeatureDump), cl::init(false),
    cl::Hidden, cl::ZeroOrMore, cl::desc("Enable Feature Dump"));

static cl::opt<bool>
    CheckPairHisto("check-pair-histo", cl::Hidden,
                   cl::desc("Dump instruction pairs in the histogram"));

static cl::opt<bool> Verbose("dump-verbose", cl::Hidden,
                             cl::desc("Dump as human readable format"));

static llvm::cl::opt<std::string>
    OutFile("feature-output", llvm::cl::desc("File for outputting features"),
            llvm::cl::init("features.csv"));

namespace {
unsigned getMaxInstructionID() {
#define LAST_OTHER_INST(NR) return NR;
#include "llvm/IR/Instruction.def"
}

// This is a point in time - we determined including these pairs of
// consecutive instructions (in the IR layout available at inline time) as
// features improves the model performance. We want to move away from manual
// feature selection.
// The array is given in opcode pairs rather than labels because 1) labels
// weren't readily available, and 2) the successions were hand - extracted.
//
// This array must be sorted.
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

unsigned getMaxDominatorTreeDepth(const Function &F,
                                  const DominatorTree &Tree) {
  unsigned MaxBBDepth = 0;
  for (const auto &BB : F)
    if (const auto *TN = Tree.getNode(&BB))
      MaxBBDepth = std::max(MaxBBDepth, TN->getLevel());

  return MaxBBDepth;
}

// get valid call uses and valid call uses in loop counts.
std::pair<int, int>
getValidCallUsesAndInLoopCounts(Function &F,
                                FunctionAnalysisManager *FAM = nullptr) {
  unsigned CallUses = 0;
  unsigned CallUsesInLoop = 0;

  for (User *U : F.users()) {
    if (CallBase *CB = dyn_cast<CallBase>(U)) {
      ++CallUses;
      BasicBlock *BB = CB->getParent();
      Function *FUser = CB->getCaller();
      auto &LI =
          FAM->getResult<LoopAnalysis>(*FUser);
      if (LI.getLoopFor(BB) != nullptr) {
        ++CallUsesInLoop;
      }
    }
  }
  return std::make_pair(CallUses, CallUsesInLoop);
}
} // namespace

// We have: 9 calculated features (the features here); 1 feature for each
// instruction opcode; and 1 feature for each manually-identified sequence.
// For the latter 2, we build a histogram: we count the number of
// occurrences of each instruction opcode or succession of instructions,
// respectively.
// Note that instruction opcodes start from 1. For convenience, we also have
// an always 0 feature for the '0' opcode, hence the extra 1.
const size_t ACPOFIExtendedFeatures::FunctionFeatures::FeatureCount =
    ImportantInstructionSuccessions.size() + getMaxInstructionID() + 1 +
    static_cast<size_t>(
        ACPOFIExtendedFeatures::NamedFloatFeatureIndex::NumNamedFloatFeatures) +
    static_cast<size_t>(
        ACPOFIExtendedFeatures::NamedFeatureIndex::NumNamedFeatures);

void ACPOFIExtendedFeatures::updateLoopRelatedFeatures(Function &F,
                                                       LoopInfo &LI,
                                                       FunctionFeatures &FF) {
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
    FF[NamedFloatFeatureIndex::BlockWithMultipleSuccecorsPerLoop] =
        q + ((float)(BlockWithMulSuccNum - q * LoopNum)) / LoopNum;
    q = LoopLevelSum / LoopNum;
    FF[NamedFloatFeatureIndex::AvgNestedLoopLevel] =
        q + ((float)(LoopLevelSum - q * LoopNum)) / LoopNum;
  }
}

void ACPOFIExtendedFeatures::updateBBLoopCallsiteBFFeatures(
    Function &F, FunctionFeatures &FF, LoopInfo &LI,
    FunctionAnalysisManager *FAM) {
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
            uint64_t EntryFreq =
                FAM->getResult<BlockFrequencyAnalysis>(*Callee)
                          .getEntryFreq();
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

ACPOFIExtendedFeatures::FunctionFeatures
ACPOFIExtendedFeatures::getFunctionFeatures(
    Function &F, DominatorTree &DomTree, TargetTransformInfo &TTI, LoopInfo &LI,
    FunctionAnalysisManager *FAM,
    bool ValidSize, bool ValidLoop, bool ValidTree) {
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
      FAM->getResult<BlockFrequencyAnalysis>(F)
                .getEntryFreq();
  ACPOFIExtendedFeatures::updateBBLoopCallsiteBFFeatures(F, FF, LI, FAM);
  if (!ValidLoop)
    ACPOFIExtendedFeatures::updateLoopRelatedFeatures(F, LI, FF);
  return FF;
}

static int getCallHeight(Module &M, CallHeight *CH, Function *F) {
  if (CH == nullptr) {
    // If we don't have cached result (for ex, running with opt)
    // We re-calculate the function level
    // Or using the old pass manager
    CallHeight CH = CallHeight(M);
    return CH.getLevel(*F);
  }
  return CH->getLevel(*F);
}

void dumpInstructionPairs(raw_fd_ostream &OS) {
  for (size_t i = 0; i < ImportantInstructionSuccessions.size(); i++) {
    std::pair<uint64_t, uint64_t> pair = ImportantInstructionSuccessions[i];
    OS << "{" << Instruction::getOpcodeName(pair.first) << ", "
       << Instruction::getOpcodeName(pair.second) << "} ";
  }
  OS << "\n";
}

void dumpFunctionFeatures(raw_fd_ostream &OS,
                          ACPOFIExtendedFeatures::FunctionFeatures &FF,
                          Function &F, bool Verbose) {
  if (Verbose) {
    OS << "Function Name: " << F.getName() << "\n";
    OS << "FeatureCount: " << FF.FeatureCount << "\n";
    OS << "\nAverage instructions per basic block: "
       << FF[ACPOFIExtendedFeatures::NamedFloatFeatureIndex::
                 InstructionPerBlock]
       << "\nAverage number of successors per block: "
       << FF[ACPOFIExtendedFeatures::NamedFloatFeatureIndex::SuccessorPerBlock]
       << "\nAverage number of vector instructions per instruction: "
       << FF[ACPOFIExtendedFeatures::NamedFloatFeatureIndex::AvgVecInstr]
       << "\nAverage nest level per loop: "
       << FF[ACPOFIExtendedFeatures::NamedFloatFeatureIndex::AvgNestedLoopLevel]
       << "\nAverage instructions per loop: "
       << FF[ACPOFIExtendedFeatures::NamedFloatFeatureIndex::InstrPerLoop]
       << "\nAverage blocks with multiple succssors per loop: "
       << FF[ACPOFIExtendedFeatures::NamedFloatFeatureIndex::
                 BlockWithMultipleSuccecorsPerLoop]
       << "\nInitial Size: "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::InitialSize] << "\n"
       << "Blocks: " << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::Blocks]
       << "\n"
       << "Calls (Number of callsites): "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::Calls] << "\n"
       << "IsLocal: " << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::IsLocal]
       << "\n"
       << "IsLinkOnceODR: "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::IsLinkOnceODR] << "\n"
       << "IsLinkOnce: "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::IsLinkOnce] << "\n"
       << "Loops: " << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::Loops]
       << "\n"
       << "MaxLoopDepth: "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::MaxLoopDepth] << "\n"
       << "MaxDomTreeLevel: "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::MaxDomTreeLevel]
       << "\nPointer arguments of this caller: "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::PtrArgs]
       << "\nCallees with pointer arguments: "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::PtrCallee]
       << "\nCallees that return a pointer: "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::CallReturnPtr]
       << "\nConditional Branches: "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::ConditionalBranch]
       << "\nConditional Branches that depends on an argument: "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::CBwithArg]
       << "\nCaller Height of the current function: "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::CallerHeight]
       << "\nNumber of explict calls to this function: "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::CallUsage]
       << "\nIs recursive: "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::IsRecursive]
       << "\nNumber of callsites that are inside loop in this function: "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::NumCallsiteInLoop]
       << "\nNumber of explict calls to this function that are in loop: "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::NumOfCallUsesInLoop]
       << "\nBlock Frequency for the first block of this function: "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::EntryBlockFreq]
       << "\nMaximum of all callsites' entry Block Frequency: "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::MaxCallsiteBlockFreq]
       << "\n";
    OS << "InstructionHistogram: ";
    OS << "Size: " << FF.InstructionHistogram.size() << "\n";
    for (size_t i = 0; i < FF.InstructionHistogram.size(); i++) {
      OS << FF.InstructionHistogram[i] << " ";
    }
    OS << "\n";
    OS << "InstructionPairHistogram: ";
    OS << "Size: " << FF.InstructionPairHistogram.size() << "\n";
    for (size_t i = 0; i < FF.InstructionPairHistogram.size(); i++) {
      OS << FF.InstructionPairHistogram[i] << " ";
    }
    OS << "\n\n";
  } else {
    OS << F.getName() << " "
       << FF[ACPOFIExtendedFeatures::NamedFloatFeatureIndex::
                 InstructionPerBlock]
       << " "
       << FF[ACPOFIExtendedFeatures::NamedFloatFeatureIndex::SuccessorPerBlock]
       << " " << FF[ACPOFIExtendedFeatures::NamedFloatFeatureIndex::AvgVecInstr]
       << " "
       << FF[ACPOFIExtendedFeatures::NamedFloatFeatureIndex::AvgNestedLoopLevel]
       << " "
       << FF[ACPOFIExtendedFeatures::NamedFloatFeatureIndex::InstrPerLoop]
       << " "
       << FF[ACPOFIExtendedFeatures::NamedFloatFeatureIndex::
                 BlockWithMultipleSuccecorsPerLoop]
       << " " << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::InitialSize]
       << " " << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::Blocks] << " "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::Calls] << " "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::IsLocal] << " "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::IsLinkOnceODR] << " "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::IsLinkOnce] << " "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::Loops] << " "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::MaxLoopDepth] << " "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::MaxDomTreeLevel] << " "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::PtrArgs] << " "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::PtrCallee] << " "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::CallReturnPtr] << " "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::ConditionalBranch]
       << " " << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::CBwithArg] << " "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::CallerHeight] << " "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::CallUsage] << " "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::IsRecursive] << " "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::NumCallsiteInLoop]
       << " "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::NumOfCallUsesInLoop]
       << " " << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::EntryBlockFreq]
       << " "
       << FF[ACPOFIExtendedFeatures::NamedFeatureIndex::MaxCallsiteBlockFreq]
       << " ";

    for (size_t i = 0; i < FF.InstructionHistogram.size(); i++) {
      OS << FF.InstructionHistogram[i] << " ";
    }
    for (size_t i = 0; i < FF.InstructionPairHistogram.size(); i++) {
      OS << FF.InstructionPairHistogram[i] << " ";
    }
    OS << "\n";
  }
}

void runAndDump(raw_fd_ostream &OS, Function *F, DominatorTree &DomTree,
                TargetTransformInfo &TTI, LoopInfo &LI, Module &M,
                CallHeight *CH, FunctionAnalysisManager *FAM = nullptr) {
  struct ACPOFIExtendedFeatures::FunctionFeatures FF =
      ACPOFIExtendedFeatures::getFunctionFeatures(*F, DomTree, TTI, LI, FAM);
  // Get the call height feature
  FF[ACPOFIExtendedFeatures::NamedFeatureIndex::CallerHeight] =
      getCallHeight(M, CH, F);
  dumpFunctionFeatures(OS, FF, *F, Verbose);
}

std::unique_ptr<raw_fd_ostream> setUpOS() {
  // Check ACPO/llvm-project issue #112
  std::error_code FileErr;
  std::unique_ptr<raw_fd_ostream> OS(
      new raw_fd_ostream(OutFile.c_str(), FileErr, llvm::sys::fs::OF_Append));

  if (FileErr) {
    llvm::errs() << "Error opening info file " << OutFile.c_str() << ": "
                 << FileErr.message() << "\n";
    return nullptr;
  }

  if (CheckPairHisto) {
    dumpInstructionPairs(*OS);
    return nullptr;
  }

  return OS;
}

PreservedAnalyses DumpFeaturePass::run(LazyCallGraph::SCC &C,
                                       CGSCCAnalysisManager &AM,
                                       LazyCallGraph &CG,
                                       CGSCCUpdateResult &UR) {
  std::unique_ptr<raw_fd_ostream> OS = setUpOS();
  if (!OS)
    return PreservedAnalyses::all();

  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerCGSCCProxy>(C, CG).getManager();

  const auto &MAMProxy = AM.getResult<ModuleAnalysisManagerCGSCCProxy>(C, CG);
  Module &M = *C.begin()->getFunction().getParent();
  CallHeight *CH = MAMProxy.getCachedResult<CallHeightAnalysis>(M);
  for (LazyCallGraph::Node &N : C) {
    Function *F = &N.getFunction();
    if (F->empty()) {
      continue;
    }

    auto &DomTree = FAM.getResult<DominatorTreeAnalysis>(*F);
    auto &TTI = FAM.getResult<TargetIRAnalysis>(*F);
    auto &LI = FAM.getResult<LoopAnalysis>(*F);

    runAndDump(*OS, F, DomTree, TTI, LI, M, CH, &FAM);
  }
  return PreservedAnalyses::all();
}

ACPOFIExtendedFeatures::NamedFeatureIndex &
llvm::operator++(ACPOFIExtendedFeatures::NamedFeatureIndex &n) {
  return n = static_cast<ACPOFIExtendedFeatures::NamedFeatureIndex>((int)n + 1);
}

ACPOFIExtendedFeatures::NamedFeatureIndex
operator++(ACPOFIExtendedFeatures::NamedFeatureIndex &n, int) {
  ACPOFIExtendedFeatures::NamedFeatureIndex res = n;
  ++n;
  return res;
}

ACPOFIExtendedFeatures::NamedFloatFeatureIndex &
llvm::operator++(ACPOFIExtendedFeatures::NamedFloatFeatureIndex &n) {
  return n = static_cast<ACPOFIExtendedFeatures::NamedFloatFeatureIndex>((int)n + 1);
}

ACPOFIExtendedFeatures::NamedFloatFeatureIndex
operator++(ACPOFIExtendedFeatures::NamedFloatFeatureIndex &n, int) {
  ACPOFIExtendedFeatures::NamedFloatFeatureIndex res = n;
  ++n;
  return res;
}
#endif
