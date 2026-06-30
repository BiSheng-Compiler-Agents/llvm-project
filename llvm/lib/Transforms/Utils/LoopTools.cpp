//===- LoopTools.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2022-2024. Huawei Technologies Co., Ltd. All rights reserved.
//
//===----------------------------------------------------------------------===//
//
// This file provides implementation for partition-based loop utilities. This
// loop representation is favourable to complex transforms such as loop tiling
// as it decomposes loops into identifiable chunks of code responsible for key
// computation and the iteration space associate with each chunk.
//
// The key idea behind this infrastructure is that of Loop Partition. A loop
// partition is a sequence of instructions that perform computation within a
// loop nest at a specific loop nest level common to all these instructions.
// Each partition is defined by having a store operation (some place to store
// final or temporary result), which is either implemented as an actual
// StoreInst or as a variable used by outside loop or other partitions in the
// loop nest.
//
// Each partition is associated with an iteration space. An iteration space is
// a set of induction variables defining their respective loops, increments for
// each variable (currently onle increment of 1 is supported) and loop bounds.
//
// To represent a complete loop nest we for a Loop Partition Graph to collect
// a set of partitions into a single data structure, with edges to help specify
// relationships between them and highlight dependencies.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/LoopTools.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/IVDescriptors.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <functional>
#include <list>
#include <queue>
#include <stack>
#include <tuple>
#include <utility>

using namespace llvm;

#define LOOPTOOLS_NAME "loop-tools"
#define DEBUG_TYPE LOOPTOOLS_NAME

static cl::opt<bool> EnableLoopTilingHack(
    "enable-loop-tiling-hack", cl::init(true), cl::Hidden,
    cl::desc("Enable a hack to avoid complicated loop tiling cases."));
STATISTIC(NumLoopsStripMined,
          "Number of large memory access loops strip mined.");

namespace LoopTools {

#define MAX_PARTITION_BLOCKS 3

static Value *stripZSExt(Value *V) {
  if (isa<SExtInst>(V) || isa<ZExtInst>(V)) {
    Instruction *I = dyn_cast<Instruction>(V);
    V = I->getOperand(0);
  }
  return V;
}

static std::string getEdgeTypeString(DependencyType ET) {
  if (ET == DependencyType::UseDef)
    return std::string("UseDef");
  if (ET == DependencyType::ReadWrite)
    return std::string("Read/Write");
  if (ET == DependencyType::ITSNestChange)
    return std::string("ITSNestChange");
  return std::string("None");
}

////////////////////////////////////////////////////////////////////////////
// LoopPartition Class implementation
////////////////////////////////////////////////////////////////////////////

LoopPartition::LoopPartition(
    std::vector<struct LoopPartitionBounds> &IndVarOrder, Instruction *Store) {
  cloneIterationSpace(IndVarOrder);
  if (Store != nullptr)
    StoreSet.insert(Store);
  ItSpaceInstructions.clear();
}

// Create a new partition with matching iteration space, but for a different
// store instruction.
LoopPartition::LoopPartition(LoopPartition &Other, Instruction *Store) {
  cloneIterationSpace(Other.getLoopOrder(), Other.getLoopBounds());
  if (Store != nullptr)
    StoreSet.insert(Store);
  copyItSpaceInstructions(Other);
  HasProblematicPHIs = Other.hasProblemPHI();
}

// Create loop partition duplicate.
LoopPartition::LoopPartition(const LoopPartition &Other) {
  const auto &LoopOrder = Other.getLoopOrder();
  const auto &LoopBounds = Other.getLoopBounds();
  cloneIterationSpace(LoopOrder, LoopBounds);
  const auto &Insts = Other.getInputInstrSet();
  const auto &StoreInsts = Other.getStoreSet();
  for (auto *I : Insts)
    addInstruction(I);
  for (auto *S : StoreInsts)
    addStore(S);
  copyItSpaceInstructions(Other);
  HasProblematicPHIs = Other.hasProblemPHI();
}

// Clean constructor.
LoopPartition::LoopPartition() {
  StoreSet.clear();
  InputInstrSet.clear();
  LoopOrder.clear();
  LBMap.clear();
  ItSpaceInstructions.clear();
  HasProblematicPHIs = false;
}

// Destructor.
LoopPartition::~LoopPartition() {
  StoreSet.clear();
  InputInstrSet.clear();
  LoopOrder.clear();
  LBMap.clear();
  ItSpaceInstructions.clear();
}

void LoopPartition::addIterationDimension(const struct LoopPartitionBounds &LPB,
                                          int Pos) {
  PHINode *IndVar = LPB.IndVar;
  LBMap[IndVar].IndVar = IndVar;
  LBMap[IndVar].Start = LPB.Start;
  LBMap[IndVar].End = LPB.End;
  LBMap[IndVar].InclusiveStart = LPB.InclusiveStart;
  LBMap[IndVar].InclusiveEnd = LPB.InclusiveEnd;
  LBMap[IndVar].Step = LPB.Step;
  LBMap[IndVar].IsUnsigned = LPB.IsUnsigned;
  LBMap[IndVar].StepInstr = LPB.StepInstr;
  if (Pos == -1)
    LoopOrder.push_back(IndVar);
  else
    LoopOrder.insert(LoopOrder.begin() + Pos, IndVar);
}

bool LoopPartition::setLoopOrder(std::vector<PHINode *> &NewLoopOrder) {
  if (LoopOrder.size() != NewLoopOrder.size())
    return false;
  LoopOrder.clear();
  for (PHINode *IV : NewLoopOrder)
    LoopOrder.push_back(IV);
  return true;
}

void LoopPartition::updateIterationDimension(
    const struct LoopPartitionBounds &LPB) {
  PHINode *IndVar = LPB.IndVar;

  if (LBMap.find(IndVar) == LBMap.end())
    return;

  if (LPB.Start != nullptr) {
    LBMap[IndVar].Start = LPB.Start;
    LBMap[IndVar].InclusiveStart = LPB.InclusiveStart;
  }
  if (LPB.End != nullptr) {
    LBMap[IndVar].End = LPB.End;
    LBMap[IndVar].InclusiveEnd = LPB.InclusiveEnd;
  }
  if (LPB.Step != nullptr)
    LBMap[IndVar].Step = LPB.Step;
  LBMap[IndVar].IsUnsigned = LPB.IsUnsigned;
  if (LPB.StepInstr != nullptr)
    LBMap[IndVar].StepInstr = LPB.StepInstr;
}

bool LoopPartition::isPartitionIndVar(Instruction *Instr) const {
  PHINode *P = dyn_cast_or_null<PHINode>(Instr);
  if (P == nullptr)
    return false;
  return (LBMap.find(P) != LBMap.end());
}

bool LoopPartition::isPartitionIndVarIncrement(Instruction *Instr) const {
  if (Instr == nullptr)
    return false;
  for (auto &Search : LBMap) {
    if (Search.second.StepInstr == Instr)
      return true;
  }
  return false;
}

void LoopPartition::cloneIterationSpace(
    const std::vector<struct LoopPartitionBounds> &LoopIndVarOrder) {
  LBMap.clear();
  LoopOrder.clear();
  for (auto &Part : LoopIndVarOrder)
    addIterationDimension(Part);
}

void LoopPartition::cloneIterationSpace(
    const std::vector<PHINode *> &VarOrder,
    const std::unordered_map<PHINode *, struct LoopPartitionBounds> &ISM) {
  LBMap.clear();
  LoopOrder.clear();
  for (auto &Var : VarOrder) {
    LBMap[Var].IndVar = Var;
    auto Search = ISM.find(Var);
    assert(Search != ISM.end());

    LBMap[Var].Start = Search->second.Start;
    LBMap[Var].End = Search->second.End;
    LBMap[Var].InclusiveStart = Search->second.InclusiveStart;
    LBMap[Var].InclusiveEnd = Search->second.InclusiveEnd;
    LBMap[Var].Step = Search->second.Step;
    LBMap[Var].IsUnsigned = Search->second.IsUnsigned;
    LBMap[Var].StepInstr = Search->second.StepInstr;
    LoopOrder.push_back(Var);
  }
}

PHINode *LoopPartition::getInnerLoopIndVar() const {
  if (LoopOrder.empty())
    return nullptr;
  return LoopOrder.back();
}

Instruction *LoopPartition::getStepInstrForIndVar(PHINode *IndVar) const {
  auto Search = LBMap.find(IndVar);
  if (Search == LBMap.end())
    return nullptr;
  return Search->second.StepInstr;
}

void LoopPartition::getInnerLoopBoundsFromCmpInst(ICmpInst *CmpCond,
                                                  Value *&Start, Value *&End,
                                                  bool &StartInc, bool &EndInc,
                                                  bool PathTrue) const {
  assert(CmpCond != nullptr);

  PHINode *IndVar = getInnerLoopIndVar();
  bool IsIncr = isIncreasing(IndVar);

  // Handle only inreasing partitions for now.
  if (!IsIncr)
    return;

  Value *Op0 = CmpCond->getOperand(0);
  Value *Op1 = CmpCond->getOperand(1);
  auto Pred = CmpCond->getPredicate();
  auto Search = LBMap.find(IndVar);
  assert(Search != LBMap.end());

  Start = Search->second.Start;
  End = Search->second.End;
  StartInc = Search->second.InclusiveStart;
  EndInc = Search->second.InclusiveEnd;

  if ((Op0 != IndVar) && (Op1 != IndVar))
    return;

  // Now make sure Op0 is the induction variable;
  if (Op1 == IndVar) {
    Op1 = Op0;
    Op0 = IndVar;
    switch (Pred) {
    case CmpInst::ICMP_EQ:
      // No change needed here.
      break;
    case CmpInst::ICMP_SLT:
      Pred = CmpInst::ICMP_SGT;
      break;
    case CmpInst::ICMP_SGT:
      Pred = CmpInst::ICMP_SLT;
      break;
    default:
      return;
    }
  }

  // Now adjust bounds based on given information.
  switch (Pred) {
  case CmpInst::ICMP_EQ:
    // Check upper and lower bounds. For EQ checks,
    // just verify the bounds tested match the current
    // partition start/end. Otherwise, this is a hole
    // in the iteration space and we cannot handle that.
    if (Search->second.Start == Op1) {
      Start = Op1;
      End = PathTrue ? Op1 : Search->second.End;
      StartInc = PathTrue;
      EndInc = PathTrue | Search->second.InclusiveEnd;
    } else if (Search->second.End == Op1) {
      Start = PathTrue ? Op1 : Search->second.Start;
      End = Op1;
      StartInc = PathTrue | Search->second.InclusiveStart;
      EndInc = PathTrue;
    }
    break;
  case CmpInst::ICMP_SLT:
    if (PathTrue) {
      Start = Search->second.Start;
      StartInc = Search->second.InclusiveStart;
      End = Op1;
      EndInc = false;
    } else {
      Start = Op1;
      StartInc = true;
      End = Search->second.End;
      EndInc = Search->second.InclusiveEnd;
    }
    break;
  case CmpInst::ICMP_SGT:
    break;
  default:
    break;
  }
}

void LoopPartition::addInstruction(Instruction *I) {
  if (I != nullptr && !hasInputInstr(I))
    InputInstrSet.insert(I);
}

void LoopPartition::addStore(Instruction *I) {
  if (I != nullptr && !hasStore(I))
    StoreSet.insert(I);
}

Instruction *LoopPartition::getUniqueStore() const {
  if (StoreSet.size() == 1) {
    SetVector<Instruction *>::const_iterator It = StoreSet.begin();
    return *It;
  }
  return nullptr;
}

PHINode *LoopPartition::getIndVarAtIndex(int Index) const {
  assert(Index >= 0 && Index < (int)LoopOrder.size());
  return LoopOrder[Index];
}

bool LoopPartition::getBoundsForIndVar(PHINode *IV,
                                       struct LoopPartitionBounds &LPB) const {
  auto Search = LBMap.find(IV);
  if (Search == LBMap.end())
    return false;

  LPB.IndVar = IV;
  LPB.Start = Search->second.Start;
  LPB.End = Search->second.End;
  LPB.InclusiveStart = Search->second.InclusiveStart;
  LPB.InclusiveEnd = Search->second.InclusiveEnd;
  LPB.Step = Search->second.Step;
  LPB.IsUnsigned = Search->second.IsUnsigned;
  LPB.StepInstr = Search->second.StepInstr;
  return true;
}

// Add instructions into the instruction order, at the end.
void LoopPartition::appendInstructionOrder(std::vector<Instruction *> &Insts) {
  PartInstOrder.insert(Insts.begin(), Insts.end());
}

// Add instructions into the instruction order, at the beginning just after
// all the PHI instructions.
void LoopPartition::prependInstructionOrderAfterPHIs(
    std::vector<Instruction *> &Insts) {
  if (Insts.empty())
    return;
  SetVector<Instruction *> OldOrder(PartInstOrder.begin(), PartInstOrder.end());
  PartInstOrder.clear();
  unsigned InputIdx = 0;
  while (isa<PHINode>(Insts[InputIdx])) {
    PartInstOrder.insert(Insts[InputIdx]);
    InputIdx++;
  }

  for (Instruction *I : OldOrder) {
    if (isa<PHINode>(I))
      PartInstOrder.insert(I);
    else
      break;
  }

  // Now add remaining instructions of the Insts vector to the instruction
  // order and then the old instructions in the partition.
  for (Instruction *I : Insts) {
    if (isa<PHINode>(I))
      continue;
    PartInstOrder.insert(I);
  }
  for (Instruction *I : OldOrder) {
    if (isa<PHINode>(I))
      continue;
    PartInstOrder.insert(I);
  }
}

// Check if the provided partition has a matching iteration space.
bool LoopPartition::hasMatchingIterationSpace(LoopPartition &Partition) const {
  unsigned Idx = 0;
  if (Partition.getLoopOrder().size() != getLoopOrder().size())
    return false;

  for (const PHINode *LIV : Partition.getLoopOrder()) {
    if (LoopOrder[Idx] != LIV)
      return false;
    Idx++;
  }

  for (const auto &LB : Partition.getLoopBounds()) {
    if (!hasMatchingLoopBounds(LB.second))
      return false;
  }

  return true;
}

// Check if the provided iteration space matches this partition.
bool LoopPartition::hasMatchingIterationSpace(
    const std::vector<PHINode *> &IndVarOrder,
    const std::vector<struct LoopPartitionBounds> &Bounds) const {
  unsigned Idx = 0;
  if (IndVarOrder.size() != getLoopOrder().size())
    return false;

  for (const PHINode *LIV : IndVarOrder) {
    if (LoopOrder[Idx] != LIV)
      return false;
    Idx++;
  }

  for (const auto &LB : Bounds) {
    if (!hasMatchingLoopBounds(LB))
      return false;
  }

  return true;
}

bool LoopPartition::differsOnlyInInnerMostBound(
    const std::vector<PHINode *> &IndVarOrder,
    const std::vector<struct LoopPartitionBounds> &Bounds) const {
  unsigned Idx = 0;
  if (IndVarOrder.size() != getLoopOrder().size())
    return false;

  for (const PHINode *LIV : IndVarOrder) {
    if (LoopOrder[Idx] != LIV)
      return false;
    Idx++;
  }

  for (Idx = 0; Idx < Bounds.size(); Idx++) {
    if (!hasMatchingLoopBounds(Bounds[Idx]) && (Idx != Bounds.size() - 1))
      return false;
  }

  return true;
}

int LoopPartition::findDeepestCommonLoopIdx(
    const std::vector<PHINode *> &IndVarOrder,
    const std::vector<struct LoopPartitionBounds> &Bounds,
    int ITSNestChange) const {
  int IVOSize = (int)IndVarOrder.size();
  if (LoopOrder.size() < IndVarOrder.size())
    IVOSize = (int)LoopOrder.size();

  for (int Idx = 0; Idx < IVOSize; Idx++) {
    if ((IndVarOrder[Idx] != LoopOrder[Idx]) ||
        (!hasMatchingLoopBounds(Bounds[Idx])))
      return (Idx - 1);
  }
  // If the boundaries match then check if there is a request for a nest
  // depth change, and if so offset the result by the ITSNestChange.
  assert(IVOSize >= ITSNestChange);
  return IVOSize - 1 - ITSNestChange;
}

bool LoopPartition::hasMatchingLoopBounds(
    const struct LoopPartitionBounds &Bounds) const {
  auto Search = LBMap.find(Bounds.IndVar);
  if (Search == LBMap.end()) {
    return false;
  }

  return (Search->second.IndVar == Bounds.IndVar) &&
         (Search->second.Start == Bounds.Start) &&
         (Search->second.End == Bounds.End) &&
         (Search->second.InclusiveStart == Bounds.InclusiveStart) &&
         (Search->second.InclusiveEnd == Bounds.InclusiveEnd) &&
         (Search->second.Step == Bounds.Step) &&
         (Search->second.IsUnsigned == Bounds.IsUnsigned) &&
         (Search->second.StepInstr == Bounds.StepInstr);
}

// Merge provided partition into this one.
void LoopPartition::mergePartitions(const LoopPartition &Partition) {
  for (Instruction *Store : Partition.getStoreSet())
    addStore(Store);

  for (Instruction *Input : Partition.getInputInstrSet())
    addInstruction(Input);
  HasProblematicPHIs |= Partition.hasProblemPHI();
}

bool LoopPartition::isIncreasing(PHINode *IndVar) const {
  auto Search = LBMap.find(IndVar);
  assert(Search != LBMap.end());
  if (!isa<ConstantInt>(Search->second.Step))
    return false;

  ConstantInt *Step = Search->second.Step;
  return (!Step->isZero() && !Step->isNegative());
}

void LoopPartition::printPartition() const {
  errs() << "Partition [" << getName() << "]\n";
  printIterationSpace();
  errs() << "Store(s):\n";
  for (auto *I : getStoreSet())
    errs() << " - " << *I << "\n";
  errs() << "Instruction cone(s):\n";
  for (auto *I : getInputInstrSet())
    errs() << " - " << *I << "\n";
  errs() << "Basic Blocks:\n";
  for (auto *BB : PartitionBlocks)
    errs() << " - " << BB->getName() << "\n";
}

void LoopPartition::printIterationSpace() const {
  int Indent = 0;
  if (LoopOrder.empty())
    return;
  for (PHINode *IndVar : LoopOrder) {
    auto Search = LBMap.find(IndVar);
    assert(Search != LBMap.end());

    printIndented(Indent, " * ");
    errs() << *IndVar << "\n";
    printIndented(Indent + 1, " * ");
    std::string StartInc = Search->second.InclusiveStart ? "[" : "(";
    std::string EndInc = Search->second.InclusiveEnd ? "]" : ")";
    Value *Start = Search->second.Start;
    Value *End = Search->second.End;
    errs() << "Bounds = " << StartInc << *Start << ", " << *End << EndInc
           << "\n";
    printIndented(Indent + 1, " * ");

    ConstantInt *Step = Search->second.Step;
    errs() << "Step = [" << *Step << "]\n";
    Indent++;
  }
}

bool LoopPartition::hasConstantIntLoopBounds() {
  if (LBMap.empty())
    return false;
  for (auto &LBM : LBMap) {
    Value *Start = LBM.second.Start;
    Value *End = LBM.second.End;
    if (!isa<ConstantInt>(Start) || !isa<ConstantInt>(End))
      return false;
  }
  return true;
}

void LoopPartition::markItSpaceInstructions(
    std::vector<Instruction *> &ItSpaceInsts) {
  for (Instruction *I : ItSpaceInsts)
    ItSpaceInstructions.insert(I);
}

void LoopPartition::markItSpaceInstruction(Instruction *I) {
  ItSpaceInstructions.insert(I);
}

void LoopPartition::copyItSpaceInstructions(const LoopPartition &Other) {
  for (Instruction *I : Other.getItSpaceInstructions())
    ItSpaceInstructions.insert(I);
}

const std::unordered_set<Instruction *> &
LoopPartition::getItSpaceInstructions() const {
  return ItSpaceInstructions;
}

bool LoopPartition::isItSpaceInstruction(Instruction *I) const {
  return (ItSpaceInstructions.count(I) > 0);
}

void LoopPartition::printIndented(int Indent, std::string Text) const {
  for (int Idx = 0; Idx < Indent; Idx++)
    errs() << "  ";
  errs() << Text;
}

void LoopPartition::orderInstructions(std::vector<BasicBlock *> &BlockOrder) {
  // Copy over Store and Instruction sets.
  int TotalBBs = 0;
  int Duplicates = 0;
  PartInstOrder.clear();
  PartitionBlocks.clear();
  HasBranches = false;
  LLVM_DEBUG(dbgs() << "=================================================\n");
  LLVM_DEBUG(dbgs() << "Ordering instructions in partition\n");
  // In this loop go over all the basic blocks in order. For each BB, go through
  // each instruction in order and if it is present in either the instruction
  // or store sets, then add them to the new instruction order set.
  for (BasicBlock *BB : BlockOrder) {
    bool HasInst = false;
    for (Instruction &I : *BB) {
      Instruction *Instr = &I;
      bool InIIS = InputInstrSet.contains(Instr);
      bool InSS = StoreSet.contains(Instr);
      if (InIIS || InSS) {
        if (isa<BranchInst>(Instr))
          HasBranches = true;
        if (InIIS && InSS)
          Duplicates++;
        PartInstOrder.insert(Instr);
        if (!HasInst) {
          TotalBBs++;
          PartitionBlocks.insert(BB);
        }
        HasInst = true;
      }
    }
  }
  LLVM_DEBUG(printPartition());
  // In case of failure, clear instruction order set.
  int InitialInsts = StoreSet.size() + InputInstrSet.size() - Duplicates;
  if (InitialInsts != (int)PartInstOrder.size()) {
    LLVM_DEBUG(dbgs() << "CLEARED partition order!\n");
    LLVM_DEBUG(dbgs() << "StoreSet.size() = " << StoreSet.size() << "\n");
    LLVM_DEBUG(dbgs() << "InputInstrSet.size() = " << InputInstrSet.size()
                      << "\n");
    LLVM_DEBUG(dbgs() << "PartInstOrder.size() = " << PartInstOrder.size()
                      << "\n");
    TotalBasicBlocks = 0;
    PartInstOrder.clear();
    PartitionBlocks.clear();
  } else {
    // When successful, specify how many basic blocks are needed in this
    // partition. If the number is > 1 then it implies a complex control flow
    // that needs to be handled accordingly during code generation.
    TotalBasicBlocks = TotalBBs;
  }
  LLVM_DEBUG(dbgs() << "=================================================\n");
}

//////////////////////////////////////////////////////////////////////////
// LoopPartitionGraph Implementation
//////////////////////////////////////////////////////////////////////////

LoopPartitionGraph::LoopPartitionGraph(Loop *LoopNest, ScalarEvolution *SCEV,
                                       DominatorTree *DT, LoopInfo *LIStruct,
                                       const LoopAccessInfo *LAI_in) {
  assert(SCEV != nullptr);
  assert(LoopNest != nullptr);
  assert(LIStruct != nullptr);
  assert(LAI_in != nullptr);
  assert(DT != nullptr);
  SE = SCEV;
  L = LoopNest;
  LI = LIStruct;
  LAI = LAI_in;
  DomTree = DT;
}

LoopPartitionGraph::~LoopPartitionGraph() {
  deleteTemporaryBBs();
  Nodes.clear();
  InEdges.clear();
  OutEdges.clear();
  RestNodes.clear();
}

std::shared_ptr<LoopPartition> LoopPartitionGraph::createNewPartition(
    std::vector<struct LoopPartitionBounds> &IndVarOrder, Instruction *Store) {
  std::shared_ptr<LoopPartition> P =
      std::make_shared<LoopPartition>(IndVarOrder, Store);
  assert(P.get() != nullptr);
  Nodes.push_back(P);
  return P;
}

std::shared_ptr<LoopPartition>
LoopPartitionGraph::createNewPartition(LoopPartition &Other,
                                       Instruction *Store) {
  std::shared_ptr<LoopPartition> P =
      std::make_shared<LoopPartition>(Other, Store);
  assert(P.get() != nullptr);
  Nodes.push_back(P);
  return P;
}

std::shared_ptr<LoopPartition>
LoopPartitionGraph::createNewPartition(const LoopPartition &Other) {
  std::shared_ptr<LoopPartition> P = std::make_shared<LoopPartition>(Other);
  assert(P.get() != nullptr);
  Nodes.push_back(P);
  return P;
}

std::shared_ptr<LoopPartition> LoopPartitionGraph::createNewPartition() {
  std::shared_ptr<LoopPartition> P = std::make_shared<LoopPartition>();
  assert(P.get() != nullptr);
  Nodes.push_back(P);
  return P;
}

const std::vector<std::shared_ptr<LoopPartition>> &
LoopPartitionGraph::getNodes() const {
  return Nodes;
}

void LoopPartitionGraph::clearEmptyPartitions() {
  for (auto It = Nodes.begin(); It != Nodes.end();) {
    if ((*It)->isEmpty())
      Nodes.erase(It);
    else
      It++;
  }
}

bool LoopPartitionGraph::hasProblemPHIs() const {
  for (auto Part : Nodes)
    if (Part->hasProblemPHI())
      return true;
  return false;
}

bool LoopPartitionGraph::createEdge(Instruction *Src, Instruction *Dst,
                                    DependencyType EdgeType, int Dir) {
  // Find source partition
  std::shared_ptr<LoopPartition> SrcPart = nullptr;
  for (auto Part : Nodes) {
    if (Part->hasStore(Src)) {
      SrcPart = Part;
      break;
    }
  }

  if (SrcPart == nullptr)
    return false;

  // Now add edges.
  for (auto DstPart : Nodes) {
    if (DstPart->hasInputInstr(Dst) || DstPart->hasStore(Dst)) {
      struct LoopPartitionEdge Edge;
      Edge.Type = EdgeType;
      Edge.Direction = Dir;
      Edge.Source = SrcPart;
      Edge.Destination = DstPart;
      Edge.SourceInstr = Src;
      Edge.DestinationInstr = Dst;
      OutEdges[SrcPart.get()].push_back(Edge);
      InEdges[DstPart.get()].push_back(Edge);
    }
  }
  return true;
}

bool LoopPartitionGraph::createEdge(std::shared_ptr<LoopPartition> SrcPart,
                                    Instruction *Src,
                                    std::shared_ptr<LoopPartition> DstPart,
                                    Instruction *Dst, DependencyType EdgeType,
                                    int Dir) {
  if ((SrcPart == nullptr) || (DstPart == nullptr))
    return false;

  // Now add edges.
  if ((DstPart->hasInputInstr(Dst) || DstPart->hasStore(Dst)) &&
      SrcPart->hasStore(Src)) {
    struct LoopPartitionEdge Edge;
    Edge.Type = EdgeType;
    Edge.Direction = Dir;
    Edge.Source = SrcPart;
    Edge.Destination = DstPart;
    Edge.SourceInstr = Src;
    Edge.DestinationInstr = Dst;
    OutEdges[SrcPart.get()].push_back(Edge);
    InEdges[DstPart.get()].push_back(Edge);
    return true;
  }
  return false;
}

// Add an edge from a SrcPart partition to any partition with specified
// Dst instruction in its InputInstrSet.
bool LoopPartitionGraph::createEdge(std::shared_ptr<LoopPartition> SrcPart,
                                    Instruction *Src, Instruction *Dst,
                                    DependencyType EdgeType, int Dir) {
  if (SrcPart == nullptr)
    return false;

  // Now add edges.
  for (auto Part : Nodes) {
    if ((Part->hasInputInstr(Dst) && !Part->hasInputInstr(Src)) ||
        (Part->hasStore(Dst) && !Part->hasInputInstr(Src))) {
      createEdge(SrcPart, Src, Part, Dst, EdgeType, Dir);
    }
  }
  return true;
}

void LoopPartitionGraph::createEdge(std::shared_ptr<LoopPartition> Src,
                                    std::shared_ptr<LoopPartition> Dst,
                                    DependencyType EdgeType, int Dir) {
  // Do not create self-UseDef edges.
  if ((EdgeType == DependencyType::UseDef) && (Src == Dst))
    return;
  if ((EdgeType == DependencyType::ReadWrite) && (Src == Dst))
    return;

  struct LoopPartitionEdge Edge;
  Edge.Type = EdgeType;
  Edge.Direction = Dir;
  Edge.Source = Src;
  Edge.Destination = Dst;
  Edge.SourceInstr = nullptr;
  Edge.DestinationInstr = nullptr;
  OutEdges[Src.get()].push_back(Edge);
  InEdges[Dst.get()].push_back(Edge);
}

void LoopPartitionGraph::createEdge(std::shared_ptr<LoopPartition> Src,
                                    std::shared_ptr<LoopPartition> Dst,
                                    unsigned ITSNestChange, int Dir) {
  // Do not create self-UseDef edges.
  struct LoopPartitionEdge Edge;
  Edge.Type = DependencyType::ITSNestChange;
  Edge.Direction = Dir;
  Edge.Source = Src;
  Edge.Destination = Dst;
  Edge.SourceInstr = nullptr;
  Edge.DestinationInstr = nullptr;
  Edge.ITSChange = ITSNestChange;
  OutEdges[Src.get()].push_back(Edge);
  InEdges[Dst.get()].push_back(Edge);
}

// This method finds all def/use edges and adds them to the LoopPartitionGraph
void LoopPartitionGraph::createUseDefEdges() {
  for (auto Part : Nodes) {
    for (Instruction *Store : Part->getStoreSet()) {
      for (User *U : Store->users()) {
        Instruction *I = dyn_cast_or_null<Instruction>(U);
        if (I != nullptr)
          createEdge(Part, Store, I, DependencyType::UseDef);
      }

      // Another way to form use/def dependencies is through memory, and it
      // happens in O2. For example write a[i] then read a[i] and use it as
      // input to another computation. So check for such cases (misaligned cases
      // would show up as read/write dependencies so those are covered) and
      // create UseDef edges for them.
      if (isa<StoreInst>(Store)) {
        StoreInst *SI = dyn_cast<StoreInst>(Store);
        Value *BasePtrStore = SI->getPointerOperand();
        for (auto DstPart : Nodes) {
          if (Part == DstPart)
            continue;
          for (Instruction *I : DstPart->getInputInstrSet()) {
            if (!isa<LoadInst>(I))
              continue;
            LoadInst *Load = dyn_cast<LoadInst>(I);
            Value *BasePtrLoad = Load->getPointerOperand();

            if (BasePtrLoad == BasePtrStore) {
              createEdge(Part, Store, DstPart, Load, DependencyType::UseDef, 0);
            }
          }
        }
      }
    }
  }
}

void LoopPartitionGraph::createReadWriteEdges() {
  auto *Dependences = LAI->getDepChecker().getDependences();
  const auto &DepChecker = LAI->getDepChecker();
  if ((Dependences == nullptr) || Dependences->empty())
    return;
  for (auto &Dep : *Dependences) {
    // RAW -> isForward(), WAR -> isBackward()
    if (Dep.isForward() || Dep.isBackward()) {
      Instruction *srcInstr = Dep.getSource(DepChecker);
      Instruction *dstInstr = Dep.getDestination(DepChecker);
      int Dir = (Dep.isForward() ? 1 : 0) + (Dep.isBackward() ? (-1) : 0);
      createEdge(srcInstr, dstInstr, DependencyType::ReadWrite, Dir);
    }
  }
}

bool LoopPartitionGraph::hasReadWriteEdges() {
  int NumSelfEdges = 0;
  int NumZeroInEdge = 0;
  for (auto Part : Nodes) {
    for (auto InEdge : InEdges[Part.get()]) {
      if (InEdge.Type == DependencyType::ReadWrite)
        return true;
    }

    for (auto OutEdge : OutEdges[Part.get()]) {
      if (OutEdge.Type == DependencyType::ReadWrite)
        return true;
    }

    // HACK: Don't tile if no single node with out edge to itself is seen
    // Don't tile if number of inEdges in a node is large
    int NumInEdges = InEdges[Part.get()].size();
    int NumOutEdges = OutEdges[Part.get()].size();
    if (NumInEdges == 0 && NumOutEdges > 0)
      NumZeroInEdge++;
    if (hasSrcToDstEdge(Part, Part)) {
      NumSelfEdges++;
      NumInEdges--;
    }
    if (EnableLoopTilingHack && NumInEdges > 2)
      return true;
  }
  if (EnableLoopTilingHack && NumSelfEdges != 1)
    return true;
  if (EnableLoopTilingHack && NumZeroInEdge != 1)
    return true;
  if (EnableLoopTilingHack && Nodes.size() > 1 && Nodes.size() < 6)
    return true;
  return false;
}

std::shared_ptr<LoopPartition> LoopPartitionGraph::getNodeAt(int Index) const {
  return Nodes.at(Index);
}

void LoopPartitionGraph::removeNodeAt(int Idx) {
  assert((Idx >= 0) && (Idx < (int)Nodes.size()));
  Nodes.erase(Nodes.begin() + Idx);
}

int LoopPartitionGraph::getInEdgeCount(
    std::shared_ptr<LoopPartition> Partition) const {
  auto Search = InEdges.find(Partition.get());
  if (Search == InEdges.end())
    return 0;
  return Search->second.size();
}

int LoopPartitionGraph::getOutEdgeCount(
    std::shared_ptr<LoopPartition> Partition) const {
  auto Search = OutEdges.find(Partition.get());
  if (Search == OutEdges.end())
    return 0;
  return Search->second.size();
}

bool LoopPartitionGraph::hasSrcToDstEdge(
    std::shared_ptr<LoopPartition> Src,
    std::shared_ptr<LoopPartition> Dst) const {
  auto Search = OutEdges.find(Src.get());
  if (Search == OutEdges.end())
    return false;
  for (const auto &Edge : Search->second) {
    if (Edge.Destination.get() == Dst.get())
      return true;
  }
  return false;
}

void LoopPartitionGraph::moveEdges(std::shared_ptr<LoopPartition> From,
                                   std::shared_ptr<LoopPartition> To) {
  if (getInEdgeCount(From) > 0) {
    for (auto &Edge : InEdges[From.get()]) {
      Edge.Destination = To;
      if (Edge.Source == From) {
        Edge.Source = To;
        OutEdges[To.get()].push_back(Edge);
      }
      InEdges[To.get()].push_back(Edge);
      // Now go to source and find matching outedge and make sure it points to
      // the To node.
      for (auto &SE : OutEdges[Edge.Source.get()]) {
        if (SE.Destination == From)
          SE.Destination = To;
      }
    }
    InEdges[From.get()].clear();
    InEdges.erase(From.get());
  }
  if (getOutEdgeCount(From) > 0) {
    for (auto &Edge : OutEdges[From.get()]) {
      if (Edge.Destination == From) {
        // We already handled self-edges, so skip this one to prvent edge
        // duplication.
        continue;
      }
      Edge.Source = To;
      OutEdges[To.get()].push_back(Edge);
      // Now go to destination and find matching inedge and make sure it points
      // to the To node for its source.
      for (auto &SE : InEdges[Edge.Destination.get()]) {
        if (SE.Source == From)
          SE.Source = To;
      }
    }
    OutEdges[From.get()].clear();
    OutEdges.erase(From.get());
  }
}

std::unordered_map<BasicBlock *, Loop *>
LoopPartitionGraph::generateHeaderToLoopMap() const {
  std::unordered_map<BasicBlock *, Loop *> HeaderToLoopMap;
  std::queue<Loop *> Q;

  // Go through loops and record their header blocks.
  Q.push(L);
  while (!Q.empty()) {
    Loop *TopL = Q.front();
    Q.pop();
    BasicBlock *Header = TopL->getHeader();
    if (Header == nullptr) {
      HeaderToLoopMap.clear();
      return HeaderToLoopMap;
    }
    HeaderToLoopMap[Header] = TopL;
    for (auto *SL : TopL->getSubLoopsVector())
      Q.push(SL);
  }
  return HeaderToLoopMap;
}

std::vector<BasicBlock *> LoopPartitionGraph::generateBBOrder() {
  std::vector<BasicBlock *> BBOrder;
  std::unordered_map<BasicBlock *, Loop *> HeaderToLoopMap =
      generateHeaderToLoopMap();
  std::queue<BasicBlock *> BBs;
  std::unordered_set<BasicBlock *> Visited;

  // Verify there were no errors in HeaderToLoopMap creation.
  assert(HeaderToLoopMap.size() > 0);

  // Lambda function to check if all BB predecessors are in the visited
  // set.
  auto VisitedPreds = [](BasicBlock *BB, std::unordered_set<BasicBlock *> V,
                         Loop *LL) -> bool {
    bool AllVisited = true;
    for (auto It = pred_begin(BB), Et = pred_end(BB); It != Et; ++It) {
      BasicBlock *Predecessor = *It;
      if (!LL->contains(Predecessor))
        continue;
      if (V.count(Predecessor) == 0)
        AllVisited = false;
    }
    return AllVisited;
  };

  // Starting at the outermost loop header, create a breadth-first
  // order of BBs. In case of a loop inside, use the HeaderToLoopMap to make
  // sure to unblock the BB.
  BBs.push(L->getHeader());
  while (!BBs.empty()) {
    BasicBlock *CBB = BBs.front();
    BBs.pop();

    // Skip BBs potentially processed.
    if (Visited.count(CBB) > 0)
      continue;

    // Mark as visited and add to BBOrder.
    BBOrder.push_back(CBB);
    Visited.insert(CBB);

    // Add successors to the queue if they are part of the loop.
    for (auto It = succ_begin(CBB), Et = succ_end(CBB); It != Et; ++It) {
      BasicBlock *Successor = *It;
      if (!L->contains(Successor))
        continue;
      if (((HeaderToLoopMap.find(Successor) != HeaderToLoopMap.end()) ||
           (VisitedPreds(Successor, Visited, L))) &&
          (Visited.count(Successor) == 0))
        BBs.push(Successor);
    }
  }

  LLVM_DEBUG(dbgs() << "BBOrder size = " << BBOrder.size() << "\n");
  return BBOrder;
}

void LoopPartitionGraph::generateInstOrderForPartitions() {
  std::vector<BasicBlock *> BlockOrder = generateBBOrder();
  for (int Src = 0; Src < getNumNodes(); Src++) {
    std::shared_ptr<LoopPartition> SrcPart = getNodeAt(Src);
    SrcPart->orderInstructions(BlockOrder);
  }
}

void LoopPartitionGraph::mergeCompatiblePartitions() {
  // Examine partitions pair-wise to see if any pair can be safely merged
  // reduce their count. Safe merger is defined as two partitions having
  // the same iteration space and lack of dependencies between them, and if
  // any dependencies to/from these partitions exists, they are to/from the
  // same partitions.
  //
  // For debugging only:
  LLVM_DEBUG(printGraphConnectivity());

  for (int Src = 0; Src < getNumNodes(); Src++) {
    std::shared_ptr<LoopPartition> SrcPart = getNodeAt(Src);
    for (int Dst = Src + 1; Dst < getNumNodes(); Dst++) {
      std::shared_ptr<LoopPartition> DstPart = getNodeAt(Dst);

      // Check for matching iteration spaces.
      if (!SrcPart->hasMatchingIterationSpace(*(DstPart.get()))) {
        continue;
      }

      // For the moment, since edges don't carry values partition
      // similarity can be summarized by edge count. Once they do,
      // it is possible a partition pair may have two edges, once for each
      // value carried (in UseDef case) or dependency represented (in other
      // cases). Then we will have to be a bit more cautious about
      // fanout/fanin analysis.
      if ((getInEdgeCount(SrcPart) != getInEdgeCount(DstPart)) ||
          (getOutEdgeCount(SrcPart) != getOutEdgeCount(DstPart))) {
        continue;
      }

      if (getInEdgeCount(SrcPart) > 0) {
        bool MatchingEdges = true;
        for (const auto &Edge : InEdges[SrcPart.get()]) {
          if (!hasSrcToDstEdge(Edge.Source, DstPart)) {
            MatchingEdges = false;
            break;
          }
        }
        if (!MatchingEdges) {
          continue;
        }
      }

      if (getOutEdgeCount(SrcPart) > 0) {
        bool MatchingEdges = true;
        for (const auto &Edge : OutEdges[SrcPart.get()]) {
          if (!hasSrcToDstEdge(DstPart, Edge.Destination)) {
            MatchingEdges = false;
            break;
          }
        }
        if (!MatchingEdges) {
          continue;
        }
      }

      // Merge partitions. Always merge into the source partition and
      // then remove the DstPart from the list of graph nodes.
      SrcPart->mergePartitions(*(DstPart.get()));
      moveEdges(DstPart, SrcPart);

      // Now we must topologically instructions in merged partitions to ensure
      // order of operations is correct.
      SetVector<Instruction *> ISet;
      for (auto *I : SrcPart->getInputInstrSet())
        ISet.insert(I);

      SrcPart->clearInputInstrSet();
      addInputsToPartitionTopologically(ISet, SrcPart.get());

      // At the moment the edge do not carry values on them, but if they start
      // then we need to make sure we copy over edges that are unique from
      // DstPart to SrcPart as part of merging.
      removeNodeAt(Dst);
      Dst--;
    }
  }
  generateInstOrderForPartitions();
}

void LoopPartitionGraph::getKeyLoopBlocks(BasicBlock **PH,
                                          BasicBlock **LoopExit,
                                          BasicBlock **GuardBlock,
                                          Loop *SL) const {
  // Preheader will be the block we start from. Generally
  *PH = SL->getLoopPreheader();
  *LoopExit = SL->getExitBlock();
  BranchInst *GuardBranch = SL->getLoopGuardBranch();
  if ((*PH == nullptr) || (*LoopExit == nullptr)) {
    *PH = nullptr;
    *LoopExit = nullptr;
    *GuardBlock = nullptr;
    return;
  }
  // The guard block will be either the preheader we modify or the guard block
  // that already exists.
  if (GuardBranch != nullptr) {
    if (GuardBranch->getNumSuccessors() != 2)
      GuardBranch = nullptr;
    else {
      BasicBlock *Succ0 = GuardBranch->getSuccessor(0);
      BasicBlock *Succ1 = GuardBranch->getSuccessor(1);
      if (!(((Succ0 == *PH) && (Succ1 == *LoopExit)) ||
            ((Succ1 == *PH) && (Succ0 == *LoopExit))))
        GuardBranch = nullptr;
    }
  }
  *GuardBlock = (GuardBranch != nullptr) ? GuardBranch->getParent() : *PH;
}

bool LoopPartitionGraph::canGenerateCodeToReplaceLoop() const {
  BasicBlock *PH = nullptr;
  BasicBlock *LoopExit = nullptr;
  BasicBlock *GuardBlock = nullptr;
  getKeyLoopBlocks(&PH, &LoopExit, &GuardBlock, L);

  // Check that we can topologically sort partitions. In a scenario that we
  // cannot it means the partition is ill-formed and we need to abort.
  std::vector<std::shared_ptr<LoopPartition>> NodeOrder = getPartitionOrder();
  if (NodeOrder.size() != Nodes.size())
    return false;

  // Now check partition contents.
  bool InstOrderGenerated = true;
  for (auto &N : Nodes) {
    if (N->getPartInstOrder().size() == 0) {
      InstOrderGenerated = false;
    }
    // Don't let the partitions get too complex for now.
    if (N->getNumPartitionBlocks() > MAX_PARTITION_BLOCKS)
      return false;

    // Check loop increments are positive as we currently do not handle
    // generation of code for loops in negative direction.
    const auto LBounds = N->getLoopBounds();
    for (const auto Pair : LBounds) {
      const struct LoopPartitionBounds &LPB = Pair.second;
      if ((LPB.Step == nullptr) || (LPB.Step->isZero()) ||
          (LPB.Step->isNegative()))
        return false;
    }
  }
  return ((PH != nullptr) && (LoopExit != nullptr) && InstOrderGenerated);
}

void LoopPartitionGraph::printGraphConnectivity() const {
  errs() << "Loop Partition Graph:\n";
  errs() << " * size = " << Nodes.size() << "\n";
  for (auto &N : Nodes) {
    errs() << " - Node: " << N->getName() << "\n";
    errs() << "  * OutEdges:\n";
    auto SearchO = OutEdges.find(N.get());
    if (SearchO != OutEdges.end()) {
      for (auto &Edge : SearchO->second) {
        errs() << "   > " << Edge.Destination->getName() << "\n";
        errs() << "     [ type = " << getEdgeTypeString(Edge.Type) << "]\n";
      }
    }
    errs() << "  * InEdges:\n";
    auto SearchI = InEdges.find(N.get());
    if (SearchI != InEdges.end()) {
      for (auto &Edge : SearchI->second) {
        errs() << "   < " << Edge.Source->getName() << "\n";
        errs() << "     [ type = " << getEdgeTypeString(Edge.Type) << "]\n";
      }
    }
  }
}

// Sort partitions in topological order.
std::vector<std::shared_ptr<LoopPartition>>
LoopPartitionGraph::getPartitionOrder() const {
  std::vector<std::shared_ptr<LoopPartition>> Order;
  std::unordered_map<LoopPartition *, int> InCount;
  std::queue<std::shared_ptr<LoopPartition>> Queue;

  // Display for debugging only.
  LLVM_DEBUG(printGraphConnectivity());
  for (auto &N : Nodes) {
    int Count = getInEdgeCount(N);
    auto SearchI = InEdges.find(N.get());
    // Discount self-edges.
    if (SearchI != InEdges.end()) {
      for (auto &Edge : SearchI->second) {
        if (Edge.Source.get() == Edge.Destination.get())
          Count--;
      }
    }
    InCount[N.get()] = Count;
    if (Count == 0)
      Queue.push(N);
  }

  while (!Queue.empty()) {
    std::shared_ptr<LoopPartition> Partition = Queue.front();
    Queue.pop();
    Order.push_back(Partition);

    // Now we know there are outgoing edges for this graph node,
    // so go through them and decrement edge counts for destination nodes.
    auto Search = OutEdges.find(Partition.get());
    if (Search != OutEdges.end()) {
      for (auto &Edge : Search->second) {
        InCount[Edge.Destination.get()] -= 1;
        if (InCount[Edge.Destination.get()] == 0)
          Queue.push(Edge.Destination);
      }
    }
  }
  return Order;
}

void LoopPartitionGraph::computeDeepestLoop() {
  DeepestLoopLevel = 0;
  for (auto &N : getNodes()) {
    LoopPartition *P = N.get();
    if (P->getLoopNestDepth() > DeepestLoopLevel)
      DeepestLoopLevel = P->getLoopNestDepth();
  }
}

Value *LoopPartitionGraph::createOrGetReplacementLogic(
    Value *Original, BasicBlock *PreHeader,
    std::unordered_map<PHINode *, PHINode *> &IVMap) {
  // If the original instruction is outside of the loop, then do nothing.
  // If it is in a TemporaryBB, then copy over the entire BB code up to this
  // instruction so that the replacement IR is generated.

  // Constants and arguments need no handling.
  if (isa<Constant>(Original) || isa<Argument>(Original))
    return Original;

  assert(Original != nullptr);
  Instruction *OriginalI = dyn_cast<Instruction>(Original);
  assert(OriginalI != nullptr);
  BasicBlock *OrigParent = OriginalI->getParent();
  assert(OrigParent != nullptr);

  // Start with simple case of reference to transformed induction variable.
  if (isa<PHINode>(OriginalI)) {
    PHINode *OrigPHI = dyn_cast<PHINode>(OriginalI);
    if (IVMap.find(OrigPHI) != IVMap.end())
      return IVMap[OrigPHI];
    else {
      // This is the case of an out-of-loop value.
      assert(!L->contains(OrigParent));
      return Original;
    }
  }

  assert(isa<Instruction>(Original));
  // Now handle more complex cases. This must now be
  std::unordered_map<Instruction *, Instruction *> InstMap;
  assert(!L->contains(OrigParent));

  if (TemporaryBBs.count(OrigParent) == 0)
    return Original;

  // This must mean the logic needs to be created in the Preheader block,
  // before the terminal instruction.
  Instruction *Term = PreHeader->getTerminator();
  assert(Term != nullptr);

  IRBuilder<> Writer(PreHeader);
  Writer.SetInsertPoint(Term);
  for (auto &Inst : *OrigParent) {
    Instruction *I = &Inst;
    Instruction *NI = I->clone();
    Writer.Insert(NI, "");
    InstMap[I] = NI;
    assert(!isa<CallInst>(NI));

    // Now update all inputs.
    for (unsigned Idx = 0; Idx < NI->getNumOperands(); Idx++) {
      Value *Op = NI->getOperand(Idx);
      if (isa<PHINode>(Op)) {
        PHINode *PredPHI = dyn_cast<PHINode>(Op);
        if (IVMap.find(PredPHI) != IVMap.end()) {
          NI->setOperand(Idx, IVMap[PredPHI]);
        }
      } else if (isa<Instruction>(Op)) {
        Instruction *IOP = dyn_cast<Instruction>(Op);
        if (InstMap.find(IOP) != InstMap.end()) {
          NI->setOperand(Idx, InstMap[IOP]);
        }
      }
    }
  }

  assert(InstMap.find(OriginalI) != InstMap.end());
  return InstMap[OriginalI];
}

int LoopPartitionGraph::createLoopNest(
    BasicBlock *StartingBB, BasicBlock *EndingBB,
    std::vector<PHINode *> &IndVarOrder, std::vector<BasicBlock *> &LoopGuardBB,
    std::vector<BasicBlock *> &LoopHeaderBB,
    std::vector<BasicBlock *> &LoopBodyBB,
    std::vector<BasicBlock *> &LoopExitingBB,
    std::vector<BasicBlock *> &LoopExitBB,
    std::vector<struct LoopPartitionBounds> &Bounds,
    std::unordered_map<PHINode *, PHINode *> &IVMap,
    std::shared_ptr<LoopPartition> Partition) {
  // Starting Basic Block must have only an unconditional branch.
  assert(StartingBB != nullptr);
  Instruction *Terminator = StartingBB->getTerminator();
  BranchInst *Branch = dyn_cast_or_null<BranchInst>(Terminator);
  assert((Branch != nullptr) && Branch->isUnconditional());
  unsigned Index = LoopHeaderBB.size();
  PHINode *IV = Partition->getIndVarAtIndex(Index);
  struct LoopPartitionBounds LPB;
  if (!Partition->getBoundsForIndVar(IV, LPB))
    llvm_unreachable("Unexpected partition data, aborting.\n");

  Function *Func = StartingBB->getParent();
  assert(Func != nullptr);

  BasicBlock *Header = createBB(Partition, Index, Func, EndingBB, "_head");
  BasicBlock *Exiting = nullptr;

  // Check if this is the inner-most loop and if not create the exiting block.
  // Otherwise, the header and the exiting blocks are one block.
  bool IsInnermost = (IV == Partition->getInnerLoopIndVar());
  bool IsMaxDepth = (((int)IndVarOrder.size()) + 1 == DeepestLoopLevel);
  if (!IsInnermost || !IsMaxDepth || Partition->hasBranchInsts())
    Exiting = createBB(Partition, Index, Func, EndingBB, "_tail");
  else
    Exiting = Header;

  // Update vectors that keep track of the BBs.
  LoopGuardBB.push_back(StartingBB);
  LoopHeaderBB.push_back(Header);
  LoopBodyBB.push_back(Header);
  LoopExitingBB.push_back(Exiting);
  LoopExitBB.push_back(EndingBB);
  IndVarOrder.push_back(IV);
  Bounds.push_back(LPB);

  // Create instructions to control the loop within the specified bounds.
  // First create the PHI node to represent the induction variable.
  IRBuilder<> HeadB(Header);
  Twine IVName = IV->getName() + "." + Partition->getName();
  PHINode *NewLIV =
      dyn_cast<PHINode>(HeadB.CreatePHI(IV->getType(), 2, IVName));
  IVMap[IV] = NewLIV;

  IRBuilder<> TailB(Exiting);
  // For the moment, we only handle positive increments. For negative values
  // we need to account for different bound checks.
  assert(!LPB.Step->isNegative() && !LPB.Step->isZero());
  Instruction *Increment = dyn_cast<Instruction>(
      TailB.CreateAdd(NewLIV, LPB.Step, NewLIV->getName() + ".incr"));

  bool IsUnsigned = LPB.IsUnsigned;
  if (LPB.StepInstr->hasNoUnsignedWrap())
    Increment->setHasNoUnsignedWrap();
  if (LPB.StepInstr->hasNoSignedWrap())
    Increment->setHasNoSignedWrap();

  // Create link to new induction variable.
  Value *NewStart = createOrGetReplacementLogic(LPB.Start, StartingBB, IVMap);
  NewLIV->addIncoming(NewStart, StartingBB);
  NewLIV->addIncoming(Increment, Exiting);

  // Now create the branch to complete the loop.
  auto Pred = (LPB.InclusiveEnd)
                  ? ((IsUnsigned) ? CmpInst::ICMP_ULE : CmpInst::ICMP_SLE)
                  : ((IsUnsigned) ? CmpInst::ICMP_ULT : CmpInst::ICMP_SLT);

  Value *NewEnd = createOrGetReplacementLogic(LPB.End, StartingBB, IVMap);
  Value *Compare = TailB.CreateCmp(Pred, Increment, NewEnd);
  TailB.CreateCondBr(Compare, Header, EndingBB);

  // Now branch from header to exiting blocks if they are not the same block.
  if (Header != Exiting)
    HeadB.CreateBr(Exiting);

  // Now lets introduce the guard logic and point to appropriate blocks.
  BranchInst *Term = dyn_cast<BranchInst>(StartingBB->getTerminator());
  assert(Term->isUnconditional());
  Term->eraseFromParent();

  IRBuilder<> Guard(StartingBB);
  Value *Cmp2 = Guard.CreateCmp(Pred, NewStart, NewEnd);
  // For some comparisons the result is always true/false depending on the
  // values (ie. say when they are constants). In such cases, we can simplify
  // the branch to be unconditional, which makes the IR simpler.
  if (isa<Constant>(Cmp2)) {
    Constant *GuardC = dyn_cast<Constant>(Cmp2);
    Guard.CreateBr(GuardC->isOneValue() ? Header : EndingBB);
  } else {
    Guard.CreateCondBr(Cmp2, Header, EndingBB);
  }
  // Now create loop one level deeper, but if this is the inner most loop
  // then this is the end of loop nest frame creation and we will proceed to
  // filling out the body of the loop.
  if (!IsInnermost)
    return createLoopNest(Header, Exiting, IndVarOrder, LoopGuardBB,
                          LoopHeaderBB, LoopBodyBB, LoopExitingBB, LoopExitBB,
                          Bounds, IVMap, Partition);

  // Return the depth of the loop nest created.
  return Index + 1;
}

int LoopPartitionGraph::createLoopNestFrame(
    BasicBlock *StartingBB, BasicBlock *EndingBB,
    std::vector<PHINode *> &IndVarOrder, std::vector<BasicBlock *> &LoopGuardBB,
    std::vector<BasicBlock *> &LoopHeaderBB,
    std::vector<BasicBlock *> &LoopBodyBB,
    std::vector<BasicBlock *> &LoopExitingBB,
    std::vector<BasicBlock *> &LoopExitBB,
    std::vector<struct LoopPartitionBounds> &Bounds,
    std::unordered_map<PHINode *, PHINode *> &IVMap,
    std::shared_ptr<LoopPartition> Partition, int ITSNestChange) {
  // First check if previous partition has a matching iteration space.
  Function *Func = StartingBB->getParent();
  assert(Func != nullptr);
  if (IndVarOrder.size() == 0) {
    // Case 1: Starting new loop nest.
    return createLoopNest(StartingBB, EndingBB, IndVarOrder, LoopGuardBB,
                          LoopHeaderBB, LoopBodyBB, LoopExitingBB, LoopExitBB,
                          Bounds, IVMap, Partition);
  } else if ((ITSNestChange == 0) &&
             Partition->hasMatchingIterationSpace(IndVarOrder, Bounds)) {
    // Case 2: partitions have the same iteration space, so we can continue
    // writing code into the previous one's BB.
    return LoopBodyBB.size();
  } else if ((ITSNestChange == 0) &&
             Partition->differsOnlyInInnerMostBound(IndVarOrder, Bounds)) {
    // Case 3: the iterators are the same, just on different bounds, so
    // create an if statement to handle this case and then return to common
    // loop body.
    llvm_unreachable("ERROR: Unhandled control flow structure.\n");
  } else {
    // Case 4: Close loops until you get to common level. What that really
    //         means is to purge Loop* vectors to specified depth and then
    //         determine what the correct starting and ending basic blocks
    //         should be.
    int Idx =
        Partition->findDeepestCommonLoopIdx(IndVarOrder, Bounds, ITSNestChange);
    if (Idx >= 0) {
      if (Idx == ((int)IndVarOrder.size()) - 1) {
        // In this case we are creating an inner loop inside of an existing loop
        // nest. So in this case, just set the startingBB to be the header of
        // that loop and the endingBB to be the exitingBB of that loop. This
        // will create an inner loop.
        StartingBB = LoopHeaderBB.back();
        EndingBB = LoopExitingBB.back();
        if (Partition->getLoopNestDepth() > (Idx + 1)) {
          return createLoopNest(StartingBB, EndingBB, IndVarOrder, LoopGuardBB,
                                LoopHeaderBB, LoopBodyBB, LoopExitingBB,
                                LoopExitBB, Bounds, IVMap, Partition);
        } else {
          return Idx + 1;
        }
      } else {
        // In this case we are closing a loop nest up to a certain level. Then
        // we either write the body of the partition after the loop, or create
        // another subloop. In both cases we need to create a new BB to function
        // as a StartingBB. In the first case, it will be where the partition
        // body will go. In the latter case, the new StartingBB will be the
        // preheader of the loop nest. The EndingBB will be the exiting basic
        // block of the deepest common loop.
        EndingBB = LoopExitingBB[Idx];
        StartingBB = createBB(Partition, Idx, Func, EndingBB, "_ph");

        // Make sure that edges that were pointing to EndingBB from PriorExiting
        // now point to StartingBB and that StartingBB has an unconditional
        // branch to the EndingBB.
        std::vector<BasicBlock *> PredsToRemove;
        for (auto It = pred_begin(EndingBB); It != pred_end(EndingBB); ++It)
          if (*It != nullptr)
            PredsToRemove.push_back(*It);

        for (BasicBlock *PBB : PredsToRemove) {
          Instruction *Term = PBB->getTerminator();
          assert(Term != nullptr);
          assert(isa<BranchInst>(Term));
          BranchInst *LoopEndBranch = dyn_cast<BranchInst>(Term);

          for (int ExitID = 0; ExitID < (int)LoopEndBranch->getNumSuccessors();
               ExitID++) {
            if (LoopEndBranch->getSuccessor(ExitID) == EndingBB)
              LoopEndBranch->setSuccessor(ExitID, StartingBB);
          }
        }

        // Now create branch from the new BB to the old one.
        IRBuilder<> SW(StartingBB);
        SW.CreateBr(EndingBB);

        // Clear vector entries until we reach the desired loop level.
        IndVarOrder.resize(Idx + 1);
        LoopGuardBB.resize(Idx + 1);
        LoopHeaderBB.resize(Idx + 1);
        LoopBodyBB.resize(Idx + 1);
        LoopExitingBB.resize(Idx + 1);
        LoopExitBB.resize(Idx + 1);
        Bounds.resize(Idx + 1);

        // Make the new BB the deepest header so that new subloops will be
        // formed off of it.
        LoopBodyBB[Idx] = StartingBB;
        LoopHeaderBB[Idx] = StartingBB;
        LoopGuardBB[Idx] = StartingBB;

        // Now create loop nest.
        if (Partition->getLoopNestDepth() > (Idx + 1)) {
          return createLoopNest(StartingBB, EndingBB, IndVarOrder, LoopGuardBB,
                                LoopHeaderBB, LoopBodyBB, LoopExitingBB,
                                LoopExitBB, Bounds, IVMap, Partition);
        } else {
          return Idx + 1;
        }
      }
    } else {
      // Case 5: End loop nest and begin a new one.
      // This is a special case of rolling back the loops, all the way to root.
      BasicBlock *GuardBB = LoopGuardBB[0];
      BasicBlock *ExitBB = LoopExitBB[0];
      EndingBB = LoopExitingBB[0];
      StartingBB = createBB(Partition, 0, Func, ExitBB, "_ph");

      // Make sure that edges that were pointing to EndingBB from PriorExiting
      // now point to StartingBB and that StartingBB has an unconditional
      // branch to the EndingBB.
      std::vector<BasicBlock *> PredsToRemove;
      for (auto It = pred_begin(ExitBB); It != pred_end(ExitBB); ++It)
        if ((*It == EndingBB) || (*It == GuardBB))
          PredsToRemove.push_back(*It);

      for (BasicBlock *PBB : PredsToRemove) {
        Instruction *Term = PBB->getTerminator();
        assert(Term != nullptr);
        assert(isa<BranchInst>(Term));
        BranchInst *LoopEndBranch = dyn_cast<BranchInst>(Term);

        for (int ExitID = 0; ExitID < (int)LoopEndBranch->getNumSuccessors();
             ExitID++) {
          if (LoopEndBranch->getSuccessor(ExitID) == ExitBB) {
            LoopEndBranch->setSuccessor(ExitID, StartingBB);
          }
        }
      }

      // Now create branch from the new BB to the old one.
      IRBuilder<> SW(StartingBB);
      SW.CreateBr(ExitBB);

      // Clear vector entries until we reach the desired loop level.
      IndVarOrder.clear();
      LoopGuardBB.clear();
      LoopHeaderBB.clear();
      LoopBodyBB.clear();
      LoopExitingBB.clear();
      LoopExitBB.clear();
      Bounds.clear();

      // Now create loop nest.
      return createLoopNest(StartingBB, ExitBB, IndVarOrder, LoopGuardBB,
                            LoopHeaderBB, LoopBodyBB, LoopExitingBB, LoopExitBB,
                            Bounds, IVMap, Partition);
    }
  }
  return -1;
}

void LoopPartitionGraph::updateInputs(
    Instruction *I, std::unordered_map<PHINode *, PHINode *> &IVMap,
    std::unordered_map<Instruction *, Instruction *> &InstMap) {
  bool IsCall = isa<CallInst>(I);
  bool IsPHI = isa<PHINode>(I);
  bool IsBR = isa<BranchInst>(I);
  CallInst *FCall = dyn_cast_or_null<CallInst>(I);
  PHINode *PHI = dyn_cast_or_null<PHINode>(I);
  BranchInst *BR = dyn_cast_or_null<BranchInst>(I);
  int ICount = IsPHI ? PHI->getNumIncomingValues() : I->getNumOperands();
  int Count = IsCall ? FCall->arg_size() : ICount;
  if (IsBR) {
    Count = BR->isUnconditional() ? 0 : 1;
  }
  for (int Idx = 0; Idx < Count; Idx++) {
    Value *V = IsCall
                   ? FCall->getArgOperand(Idx)
                   : (IsPHI ? PHI->getIncomingValue(Idx) : I->getOperand(Idx));
    if (IsBR)
      V = BR->getCondition();
    assert(isa<Instruction>(V) || isa<Constant>(V) || isa<Argument>(V));

    if (isa<Constant>(V) || isa<Argument>(V))
      continue;

    Instruction *Input = dyn_cast<Instruction>(V);
    // Do not replace loop invariants.
    BasicBlock *IBB = Input->getParent();
    if (!L->contains(IBB) && (TemporaryBBs.count(IBB) == 0))
      continue;

    PHINode *PN = dyn_cast_or_null<PHINode>(V);
    Instruction *NewInput = nullptr;
    if ((PN != nullptr) && (IVMap.find(PN) != IVMap.end())) {
      NewInput = dyn_cast<Instruction>(IVMap[PN]);
    } else if ((Input != nullptr) && (InstMap.find(Input) != InstMap.end())) {
      NewInput = InstMap[Input];
    }
    assert((NewInput != nullptr) || (PN != nullptr));
    if (NewInput == nullptr)
      continue;
    if (IsCall)
      FCall->setArgOperand(Idx, NewInput);
    else if (IsPHI)
      PHI->setIncomingValue(Idx, NewInput);
    else
      I->setOperand(Idx, NewInput);
  }
}

// This method prints out partition instructions into loop body at approrpriate
// spots.
bool LoopPartitionGraph::generatePartitionCode(
    std::shared_ptr<LoopPartition> P, BasicBlock *Header, BasicBlock *Body,
    BasicBlock *InsertBefore, std::unordered_map<PHINode *, PHINode *> &IVMap,
    std::unordered_map<Instruction *, Instruction *> &InstMap) {
  // To print out instructions in a partition we print them out from output to
  // input. The reason is that they are stored in a setVector which is filled
  // starting with the 'store' and traversing backwards to inputs. Thus if
  // we traverse the instructions in order, they start with those closest to the
  // store and end with those farthest from it, ie. reverse order.
  //
  // Note that any time we make an instruction in the new loop, we will record
  // it in the InstMap to ensure that we can access the data in the subsequent
  // partitions if need be.
  Instruction *Term = Body->getTerminator();
  std::unique_ptr<IRBuilder<>> IRG = std::make_unique<IRBuilder<>>(Body);
  std::vector<Instruction *> NewInsts;
  std::unordered_map<BasicBlock *, Loop *> HTLMap = generateHeaderToLoopMap();

  IRG->SetInsertPoint(Term);

  // Printing partitions is now simple since the PartInstOrder holds
  // in program order. The only thing to worry about is creating new BBs when
  // control flow demands this.
  BasicBlock *CurBody = Body;
  BasicBlock *OrigBody = nullptr;
  int SubBBIndex = 1;
  LLVM_DEBUG(dbgs() << "Generating new partition code:\n");
  LLVM_DEBUG(dbgs() << " * starting body: " << Body->getName() << "\n");
  for (Instruction *S : P->getPartInstOrder()) {
    if (InstMap.find(S) != InstMap.end()) {
      // We have an entry for this instruction, so skip it since we can link
      // to this instance when needed.
      continue;
    }
    LLVM_DEBUG(dbgs() << " - [" << *S << "]\n");
    Instruction *NewS = S->clone();
    // Figure out if we are moving to a new BB or staying with the current one.
    if (OrigBody == nullptr) {
      OrigBody = S->getParent();
      OldToNewBBMap[OrigBody] = CurBody;
      IRG = std::make_unique<IRBuilder<>>(CurBody);
      IRG->SetInsertPoint(CurBody->getTerminator());
    } else if (S->getParent() != OrigBody) {
      OrigBody = S->getParent();
      if (!P->hasBranchInsts()) {
        // This partition is a straight-line partition, so even if original
        // instructions are from diverse BBs, there is no branching in here
        // so no additional control flow should be introduced.
        OldToNewBBMap[OrigBody] = CurBody;
      }
      if (OldToNewBBMap.find(OrigBody) == OldToNewBBMap.end()) {
        LLVM_DEBUG(dbgs() << "OrigBody = " << OrigBody->getName() << "\n");
        llvm_unreachable("ERROR: Missing basic block control flow mapping.\n");
      }
      CurBody = OldToNewBBMap[OrigBody];
      IRG = std::make_unique<IRBuilder<>>(CurBody);
      IRG->SetInsertPoint(CurBody->getTerminator());
    }

    // Now print a cloned instruction in the appropriate target BB.
    std::string Name = P->getName() + std::string("_") + S->getName().str();
    InstMap[S] = NewS;
    if (isa<BranchInst>(S)) {
      // In case of a branch we are clearly creating a more complex control
      // flow than just a simple single BB partition. So we have to determine
      // if all sides of the branch go to in-partition code. If so, just create
      // corresponding BBs, add a map to ensure new instructions are mapped
      // correctly and update the Body BB to reflect which BB the new
      // instructions are to be placed. To make things easier, we use HTLMap
      // to keep the mapping in order.
      BranchInst *BR = dyn_cast<BranchInst>(NewS);
      BranchInst *CBTerm = dyn_cast<BranchInst>(CurBody->getTerminator());
      assert(CBTerm->isUnconditional());
      LLVM_DEBUG(dbgs() << "Branch: " << *BR << "\n");

      for (unsigned I = 0; I < BR->getNumSuccessors(); I++) {
        BasicBlock *Successor = BR->getSuccessor(I);
        // The way we handle instruction in an inner loop partition which from
        // the outer loop basic blocks is to check if the terminator of the
        // basic block is pointing to the inner loop basic block that belongs
        // to this partition, and create a new basic block for the following
        // instructions and use that terminator to connect the basic block
        // currently working on and the new basic block if so.
        // However, this method may cause abortion when loop simplify pass
        // creates a preheader to the inner loop. Therefore, to handle this,
        // we extend the check from the terminator's successors to its
        // successors' successors if its that successor is a preheader.
        Instruction *SuccTerm = Successor->getTerminator();
        if (Successor->size() == 1 && isa<BranchInst>(SuccTerm)) {
          BranchInst *NewBR = dyn_cast<BranchInst>(SuccTerm);
          if (NewBR->isUnconditional()) {
            BasicBlock *NewSuccessor = NewBR->getSuccessor(0);
            Loop *Inner = LI->getLoopFor(NewSuccessor);
            if (Inner != nullptr && Successor == Inner->getLoopPreheader())
              Successor = NewSuccessor;
          }
        }

        bool SuccMade = (OldToNewBBMap.find(Successor) != OldToNewBBMap.end());
        LLVM_DEBUG(dbgs() << " * Successor: " << Successor->getName() << "\n");

        if (P->hasBlock(Successor)) {
          if (SuccMade) {
            // If the successor has been made already for this partition, then
            // just point to it.
            NewS->setSuccessor(I, OldToNewBBMap[Successor]);
          } else {
            // Create a new block to mirror the successor and point to it.
            std::string NewName = Body->getName().str() + std::string("_") +
                                  std::to_string(SubBBIndex);
            LLVM_DEBUG(dbgs() << "   - New block [" << NewName << "]\n");
            BasicBlock *NewBB =
                createBB(Header->getParent(), InsertBefore, false, NewName);
            OldToNewBBMap[Successor] = NewBB;
            SubBBIndex++;

            // Add link to previously pointed to next BB.
            IRBuilder<> NewBBBuilder(NewBB);
            NewBBBuilder.Insert(CBTerm->clone());

            // Change source branch to point to the new BB.
            NewS->setSuccessor(I, NewBB);
          }
        } else {
          // Point to where the original CBTerm was pointing to - that is the
          // control-flow exit.
          NewS->setSuccessor(I, CBTerm->getSuccessor(0));
        }
      }

      // Now delete the existing terminator in CurBody and slot in the new
      // branch instruction we just created.
      IRG->Insert(NewS);
      CBTerm->eraseFromParent();
      IRG->SetInsertPoint(NewS);
      LLVM_DEBUG(dbgs() << "BR done!\n");
    } else if (isa<PHINode>(S)) {
      IRG->SetInsertPoint(CurBody->getFirstNonPHI());
      IRG->Insert(NewS, Name);
      IRG->SetInsertPoint(CurBody->getTerminator());
    } else if (isa<StoreInst>(NewS)) {
      IRG->Insert(NewS);
    } else if (isa<CallInst>(NewS)) {
      CallInst *CI = dyn_cast<CallInst>(NewS);
      if (CI->getFunctionType()->getReturnType()->isVoidTy())
        IRG->Insert(NewS);
      else
        IRG->Insert(NewS, Name);
    } else {
      IRG->Insert(NewS, Name);
    }
    NewInsts.push_back(NewS);
  }
  // Now we inserted all instructions, so lets connect them as necessary
  for (Instruction *NewI : NewInsts) {
    updateInputs(NewI, IVMap, InstMap);
  }
  LLVM_DEBUG(dbgs() << "Partition code done!\n");
  return true;
}

// Given a specified loop nest, replace it with the loop nest described by
// this graph. The entry to this loop nest will be the preheader of the
// loop L, and the loop exit of L will be the exit of the loop nest
// represented by this graph. Note that L must have a single preheader
bool LoopPartitionGraph::replaceLoopNestWithGraph(bool OnlyStripMine) {
  if (!OnlyStripMine && !canGenerateCodeToReplaceLoop())
    return false;

  // Now that we know we can generate code for the loop, lets
  // get the key BBs we will work with to write out the new loop nest
  // described by this graph.
  BasicBlock *PH = nullptr;
  BasicBlock *LoopExit = nullptr;
  BasicBlock *GuardBlock = nullptr;
  getKeyLoopBlocks(&PH, &LoopExit, &GuardBlock, L);

  std::vector<std::shared_ptr<LoopPartition>> NodeOrder = getPartitionOrder();
  assert(NodeOrder.size() == Nodes.size());

  // In this section we generate the loop nest by sequentially processing
  // partitions. If a partition has a matching iteration space, we simply
  // append the code to the end of the loop body. If not, then we have
  // to create inner loop(s) or end them. To track this loop creation we
  // use IndVarOrder, LoopStartBB and LoopEndBB vectors.
  std::vector<PHINode *> IndVarOrder;
  std::vector<BasicBlock *> LoopGuardBB;
  std::vector<BasicBlock *> LoopHeaderBB;
  std::vector<BasicBlock *> LoopBodyBB;
  std::vector<BasicBlock *> LoopExitingBB;
  std::vector<BasicBlock *> LoopExitBB;
  std::vector<struct LoopPartitionBounds> Bounds;

  // Determine loop nest depth.
  computeDeepestLoop();

  std::unordered_map<PHINode *, PHINode *> IVMap;
  std::unordered_map<Instruction *, Instruction *> InstMap;
  int Index;
  std::shared_ptr<LoopPartition> PrevPart = nullptr;
  for (auto &P : NodeOrder) {
    LLVM_DEBUG(dbgs() << "Printing partition: " << P->getName() << "\n");
    int ITSNestChange =
        (PrevPart == nullptr) ? 0 : getNestChangeDepth(PrevPart, P);
    if (ITSNestChange > 0) {
      // Remove instructions mapped by previous partition from InstMap
      // as they are no longer accessible unless they are partition outputs.
      for (Instruction *I : PrevPart->getInputInstrSet()) {
        InstMap.erase(I);
      }
    }
    LLVM_DEBUG(dbgs() << " * ITS Nest Change = " << ITSNestChange << "\n");
    Index = createLoopNestFrame(PH, LoopExit, IndVarOrder, LoopGuardBB,
                                LoopHeaderBB, LoopBodyBB, LoopExitingBB,
                                LoopExitBB, Bounds, IVMap, P, ITSNestChange);
    LLVM_DEBUG(dbgs() << " * Index = " << Index << "\n");
    BasicBlock *Header = LoopHeaderBB[Index - 1];
    BasicBlock *Body = LoopBodyBB[Index - 1];
    BasicBlock *Exit = LoopExitingBB[Index - 1];
    LLVM_DEBUG(dbgs() << " * writing partition body!\n");
    LLVM_DEBUG(dbgs() << "   - Header [" << Header->getName() << "]\n");
    LLVM_DEBUG(dbgs() << "   - Body [" << Body->getName() << "]\n");
    LLVM_DEBUG(dbgs() << "   - Exit [" << Exit->getName() << "]\n");
    if (!generatePartitionCode(P, Header, Body, Exit, IVMap, InstMap))
      llvm_unreachable("ERROR: Failed to generate new loop code.\n");

    LLVM_DEBUG(dbgs() << "Process loops with large memory access\n");
    if (OnlyStripMine &&
        !processLargeMemoryAccess(PH, IndVarOrder, LoopHeaderBB, LoopExitingBB,
                                  Bounds[0].Step, IVMap)) {
      llvm_unreachable(
          "ERROR: Transformation interrupted unexpectedly. Aborting.");
    }
    PrevPart = P;
  }
  // Update PHI nodes created when writing out the new loop nest.
  for (BasicBlock *BB : GraphBBs) {
    if (!updatePHINodeControlPaths(BB))
      return false;
  }

  // Fix exit blocks too.
  for (BasicBlock *BB : LoopExitBB) {
    if (!updatePHINodeControlPaths(BB))
      return false;
  }

  // Erase existing loop.
  deleteOldLoopBodyAndTemporaryBBs();
  return true;
}

void LoopPartitionGraph::unlinkAndDestroyLoopNest(Loop *L) {
  std::stack<Loop *> S;

  S.push(L);
  while (!S.empty()) {
    Loop *Current = S.top();
    S.pop();
    auto LV = Current->getSubLoopsVector();
    for (Loop *SL : LV) {
      S.push(SL);
      Current->removeChildLoop(SL);
    }
    Loop::iterator LIL = find(LI->begin(), LI->end(), Current);
    if (LIL != LI->end()) {
      LI->removeLoop(LIL);
      LI->destroy(Current);
    }
  }
}

void LoopPartitionGraph::deleteTemporaryBBs() {
  for (auto *Block : TemporaryBBs) {
    Block->dropAllReferences();
  }
  for (auto *Block : TemporaryBBs) {
    Block->eraseFromParent();
  }
  TemporaryBBs.clear();
}

void LoopPartitionGraph::deleteOldLoopBodyAndTemporaryBBs() {
  // Portion of code modified from deleteDeadLoop utility.
  // Remove the block from the reference counting scheme, so that we can
  // delete it freely later.
  for (auto *Block : TemporaryBBs) {
    Block->dropAllReferences();
  }
  for (auto *Block : L->blocks()) {
    Instruction *I = Block->getTerminator();
    I->eraseFromParent();
    Block->dropAllReferences();
  }

  // Erase the instructions and the blocks without having to worry
  // about ordering because we already dropped the references.
  // NOTE: This iteration is safe because erasing the block does not remove
  // its entry from the loop's block list.  We do that in the next section.
  for (Loop::block_iterator LpI = L->block_begin(), LpE = L->block_end();
       LpI != LpE; ++LpI)
    (*LpI)->eraseFromParent();

  // Finally, the blocks from loopinfo.  This has to happen late because
  // otherwise our loop iterators won't work.
  SmallPtrSet<BasicBlock *, 8> blocks;
  blocks.insert(L->block_begin(), L->block_end());
  for (BasicBlock *BB : blocks)
    LI->removeBlock(BB);

  for (auto *Block : TemporaryBBs) {
    Block->eraseFromParent();
  }
  TemporaryBBs.clear();
  // The last step is to update LoopInfo now that we've eliminated this loop.
  // Note: LoopInfo::erase remove the given loop and relink its subloops with
  // its parent. While removeLoop/removeChildLoop remove the given loop but
  // not relink its subloops, which is what we want.
  unlinkAndDestroyLoopNest(L);
  L = nullptr;
}

// Given a loop nest and a specific candidate IV, check the transitive fanout of
// IV to look for add/sub as an increment and then a comparison that leads to
// an exit out of the loop. Finally, extract the constant increment/decrement
// for the induction variable. If all data is available, then return true.
// Otherwise, return false to indicate that this is not an induction variable
// we can handle or that this is not an induction variable.
bool LoopPartitionGraph::getIndVarUseChain(
    Loop *Nest, PHINode *IV, Instruction *&Increment, Instruction *&Comparison,
    BranchInst *&LoopBranch, Constant *&IncValue, bool &InclusiveEnd,
    bool &IsUnsigned) const {
  // Will only handle integer induction variables.
  if (!IV->getType()->isIntegerTy())
    return false;

  // Clear return values
  Increment = nullptr;
  Comparison = nullptr;
  LoopBranch = nullptr;
  IncValue = nullptr;
  InclusiveEnd = false;
  IsUnsigned = false;

  // Find Increment.
  for (unsigned Idx = 0; Idx < IV->getNumIncomingValues(); Idx++) {
    BasicBlock *Pred = IV->getIncomingBlock(Idx);
    if ((!Nest->contains(Pred)) &&
        (isa<Instruction>(IV->getIncomingValue(Idx)))) {
      Instruction *Candidate = dyn_cast<Instruction>(IV->getIncomingValue(Idx));
      if (Candidate->getNumOperands() != 2)
        continue;

      if (isa<Constant>(Candidate->getOperand(0))) {
        Increment = Candidate;
        IncValue = dyn_cast<Constant>(Candidate->getOperand(0));
        break;
      }
      if (isa<Constant>(Candidate->getOperand(1))) {
        Increment = Candidate;
        IncValue = dyn_cast<Constant>(Candidate->getOperand(1));
        break;
      }
    }
  }

  if (Increment == nullptr)
    return false;

  // Create Lambda function to find the branch that follows an instruction
  auto findBranch = [](Instruction *Src, Loop *LN) -> BranchInst * {
    for (auto U : Src->users()) {
      // We are looking for a conditional branch.
      auto Branch = dyn_cast<BranchInst>(U);
      if (!Branch || Branch->isUnconditional() ||
          (Branch->getNumSuccessors() != 2))
        continue;

      BasicBlock *B1 = Branch->getSuccessor(0);
      BasicBlock *B2 = Branch->getSuccessor(1);

      if ((!LN->contains(B1) && LN->contains(B2)) ||
          (LN->contains(B1) && !LN->contains(B2)))
        return Branch;
    }
    return nullptr;
  };

  // Create Lambda function to find the comparison
  auto findCompare = [](Instruction *Src, bool Exclusive) -> ICmpInst * {
    for (auto U : Src->users()) {
      auto Compare = dyn_cast<ICmpInst>(U);
      if (Compare) {
        if (Exclusive) {
          auto Pred = Compare->getPredicate();
          if ((Pred == CmpInst::ICMP_ULT) || (Pred == CmpInst::ICMP_UGT) ||
              (Pred == CmpInst::ICMP_SLT) || (Pred == CmpInst::ICMP_SGT))
            return Compare;
        } else
          return Compare;
      }
    }
    return nullptr;
  };

  // Check successors of the PHI itself.
  ICmpInst *IVS = findCompare(dyn_cast<Instruction>(IV), false);
  if (IVS) {
    BranchInst *BR = findBranch(IVS, Nest);
    IsUnsigned = IVS->isUnsigned();
    if (BR) {
      Comparison = dyn_cast<Instruction>(IVS);
      LoopBranch = BR;
      auto Pred = IVS->getPredicate();
      if ((Pred == CmpInst::ICMP_ULE) || (Pred == CmpInst::ICMP_UGE) ||
          (Pred == CmpInst::ICMP_SLE) || (Pred == CmpInst::ICMP_SGE) ||
          (Pred == CmpInst::ICMP_EQ))
        InclusiveEnd = true;
    }
  }
  // If that does not work, check the successors of the increment. That is
  // sometimes used for condition checking for loops.
  IVS = findCompare(Increment, true);
  if (IVS) {
    BranchInst *BR = findBranch(IVS, Nest);
    IsUnsigned = IVS->isUnsigned();
    if (BR) {
      Comparison = dyn_cast<Instruction>(IVS);
      LoopBranch = BR;
      InclusiveEnd = true;
    }
  }
  return (Increment && Comparison && LoopBranch && IncValue);
}

// This method determines the induction variable when there is no preheader.
// This method should only be used when the default mechanism for loop induction
// variable recognition failed.
PHINode *LoopPartitionGraph::getNonTrivialInductionVariable(
    Loop *Nest, struct LoopPartitionBounds &LPB,
    std::vector<Instruction *> &ItInsts, bool &Increasing) const {
  BasicBlock *Header = Nest->getHeader();

  // Loop must have a single header, or else bail out.
  if (Header == nullptr)
    return nullptr;

  BasicBlock *UniquePreLoopBlock = nullptr;
  PHINode *IndVar = nullptr;
  for (PHINode &Candidate : Header->phis()) {
    // For each PHI check how many predecessors it has. There should be no more
    // than 1 outside of the loop nest and if so we can treat that as our
    // "preheader".
    int PredIdx = -1;
    for (unsigned Idx = 0; Idx < Candidate.getNumIncomingValues(); Idx++) {
      BasicBlock *Pred = Candidate.getIncomingBlock(Idx);
      if (Nest->contains(Pred))
        continue;

      if (UniquePreLoopBlock == nullptr) {
        UniquePreLoopBlock = Pred;
        PredIdx = Idx;
      }

      if (UniquePreLoopBlock != Pred)
        return nullptr;
    }

    // Lets make sure we got the "preheader".
    if ((UniquePreLoopBlock == nullptr) || (PredIdx == -1))
      break;

    // Now the PHI is a candidate. To be valid we need the PHI to be used to
    // increment/decrement by some value and then either PHI itself or the
    // increment/decrement are used to control loop iteration.
    Instruction *Increment = nullptr;
    Instruction *Comparison = nullptr;
    BranchInst *LoopBranch = nullptr;
    Constant *IncValue = nullptr;
    bool InclusiveEnd = false;
    bool IsUnsigned = false;
    if (!getIndVarUseChain(Nest, &Candidate, Increment, Comparison, LoopBranch,
                           IncValue, InclusiveEnd, IsUnsigned))
      continue;

    // Extract the necessary information from the induction variable.
    LPB.IndVar = &Candidate;
    LPB.Start = Candidate.getIncomingValue(PredIdx);
    if (isa<Constant>(Comparison->getOperand(0)))
      LPB.End = Comparison->getOperand(0);
    else
      LPB.End = Comparison->getOperand(1);
    LPB.InclusiveStart = true;
    LPB.InclusiveEnd = InclusiveEnd;
    if (isa<ConstantInt>(IncValue))
      LPB.Step = cast<ConstantInt>(IncValue);
    else
      LPB.Step = nullptr;
    LPB.StepInstr = Increment;
    LPB.IsUnsigned = IsUnsigned;

    // Mark iteration space instructions.
    ItInsts.push_back(&Candidate);
    ItInsts.push_back(Increment);
    ItInsts.push_back(Comparison);
    ItInsts.push_back(LoopBranch);

    // Break search as we do not accept more than one induction variable here.
    break;
  }

  // If there is not a unique pre-loop block ("preheader") then abort.
  if ((UniquePreLoopBlock == nullptr) || (IndVar == nullptr))
    return nullptr;

  Increasing = (LPB.StepInstr->getOpcode() == Instruction::Add);
  return LPB.IndVar;
}

bool LoopPartitionGraph::computeIterationSpaceForLoopNest(
    Loop *Nest, std::unordered_map<BasicBlock *, LoopPartition *> &ItSpaceMap,
    LoopPartition *ParentLoopPart) {
  // There is no loop so exit normally.
  if (Nest == nullptr)
    return true;

  LLVM_DEBUG(dbgs() << "computeIterationSpaceForLoopNest for ["
                    << Nest->getName() << "]\n");
  // Now analyze the loop nest.
  bool Increasing = true;
  std::vector<Instruction *> ItInsts;
  struct LoopPartitionBounds LPB;
  LPB.InclusiveStart = true;
  LPB.IndVar = getInductionVariableAndBounds(
      Nest, &LPB.Start, &LPB.End, &LPB.StepInstr, &LPB.Step, Increasing,
      LPB.InclusiveEnd, LPB.IsUnsigned, ItInsts);
  if ((LPB.IndVar == nullptr) &&
      !getNonTrivialInductionVariable(Nest, LPB, ItInsts, Increasing)) {
    return false;
  }
  if (!LPB.IndVar->getType()->isIntegerTy()) {
    return false;
  }
  LoopPartition *RootPart =
      (ParentLoopPart == nullptr)
          ? createNewPartition().get()
          : createNewPartition(*ParentLoopPart, nullptr).get();

  RootPart->addIterationDimension(LPB);
  RootPart->markItSpaceInstructions(ItInsts);
  BasicBlock *Header = Nest->getHeader();
  ItSpaceMap[Header] = RootPart;
  // Now go over child loops
  for (Loop *SL : Nest->getSubLoopsVector())
    if (!computeIterationSpaceForLoopNest(SL, ItSpaceMap, RootPart))
      return false;

  // Mark all BBs in this loop, but not within the child loops,
  // as having the same iteration space as root partition. However,
  // if BBs are separated from the header along any path by a different
  // partition then clone root partition to indicate that even though
  // the iteration space is the same, the partition is different. We will
  // use this information later to determine partition outputs.
  if (Nest->isInnermost()) {
    if (!computeIterationSpaceForBBs(Nest, ItSpaceMap)) {
      return false;
    }
  } else {
    // This is the nested loop case.
    std::queue<BasicBlock *> Q;
    for (BasicBlock *BB : Nest->getBlocks())
      Q.push(BB);

    while (!Q.empty()) {
      BasicBlock *BB = Q.front();
      Q.pop();

      if (BB == Header)
        continue;

      bool IsSameAsRoot = true;
      bool ReProcess = false;
      for (auto It = pred_begin(BB), Et = pred_end(BB); It != Et; ++It) {
        BasicBlock *Predecessor = *It;
        if (!Nest->contains(Predecessor))
          continue;
        auto SearchBB = ItSpaceMap.find(Predecessor);
        if (SearchBB != ItSpaceMap.end()) {
          // We found a partition that we can add to
          if (SearchBB->second != RootPart) {
            IsSameAsRoot = false;
          }
          break;
        }
        if (std::next(It) == Et) {
          // If we have gone through every predecessor of the current block
          // and no partition was found, we re-add the BB to the queue.
          // We know eventually that a predecessor will be found in the
          // ItSpaceMap since the loop header is in the map and
          // every BB in the loop is connected to the header by a path.
          // Similar to an algorithm like Bellman-Ford, in the absolute
          // worst case we will still process atleast one BB per iteration
          // of adding BB's back to the queue
          ReProcess = true;
          Q.push(BB);
        }
      }

      if (!ReProcess && !isBlockPartOfSubLoop(Nest, BB)) {
        ItSpaceMap[BB] = IsSameAsRoot
                             ? RootPart
                             : createNewPartition(*RootPart, nullptr).get();
      }
    }
  }

  // Otherwise return true as we are done with the loop nest.
  return true;
}

Value *LoopPartitionGraph::getPtrBaseOutsideLoop(StoreInst &Store,
                                                 bool NoTransform) const {
  Value *Ptr = Store.getPointerOperand();

  assert(Ptr != nullptr);

  if (isa<BitCastInst>(Ptr)) {
    // Sometimes the address uses a bitcast, so go through it.
    BitCastInst *BC = dyn_cast<BitCastInst>(Ptr);
    Ptr = BC->getOperand(0);
  }
  if (isa<BitCastOperator>(Ptr)) {
    // Sometimes the address uses a bitcast, so go through it.
    BitCastOperator *BC = dyn_cast<BitCastOperator>(Ptr);
    Ptr = BC->getOperand(0);
  }

  if (!isa<GetElementPtrInst>(Ptr))
    return nullptr;

  // Now find a GEP instruction with the base pointer. There may
  // be several GEPs in a chain in cases where we do a little ptr arithmetic
  // on top of the base pointer. So *(a + i + 16) may actually be 2 GEPs in a
  // chain.
  GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Ptr);
  Value *Src = GEP->getPointerOperand();
  while (isa<GetElementPtrInst>(Src)) {
    GEP = dyn_cast<GetElementPtrInst>(Src);
    Src = GEP->getPointerOperand();
  }

  // If there is no transformation involved, skip the check about if the pointer
  // is outside or not.
  if (!NoTransform && isa<Instruction>(Src)) {
    Instruction *SrcI = dyn_cast<Instruction>(Src);
    if (L->contains(SrcI->getParent()))
      return nullptr;
  }
  return Src;
}

void LoopPartitionGraph::addInputsToPartitionTopologically(
    SetVector<Instruction *> &InstSet, LoopPartition *LP) {
  assert(LP != nullptr);
  std::unordered_map<Instruction *, int> OutputCount;

  for (Instruction *I : InstSet) {
    if (OutputCount.find(I) == OutputCount.end())
      OutputCount[I] = 0;
    for (unsigned Idx = 0; Idx < I->getNumOperands(); Idx++) {
      Value *V = I->getOperand(Idx);
      if (!isa<Instruction>(V))
        continue;
      Instruction *Input = dyn_cast<Instruction>(V);
      if (InstSet.count(Input) == 0)
        continue;
      if (OutputCount.find(Input) == OutputCount.end())
        OutputCount[Input] = 0;
      OutputCount[Input]++;
    }
  }

  std::queue<Instruction *> Q;

  for (Instruction *I : InstSet) {
    assert(OutputCount.find(I) != OutputCount.end());
    if (OutputCount[I] == 0)
      Q.push(I);
  }

  while (!Q.empty()) {
    Instruction *I = Q.front();
    Q.pop();
    LP->addInstruction(I);
    for (unsigned Idx = 0; Idx < I->getNumOperands(); Idx++) {
      Value *V = I->getOperand(Idx);
      if (!isa<Instruction>(V))
        continue;
      Instruction *Input = dyn_cast<Instruction>(V);
      if (InstSet.count(Input) == 0)
        continue;
      assert(OutputCount[Input] > 0);
      OutputCount[Input]--;
      if (OutputCount[Input] == 0)
        Q.push(Input);
    }
  }
}

bool LoopPartitionGraph::isWhiteListCall(CallInst *CI) const {
  Function *Callee = CI->getCalledFunction();
  if (Callee == nullptr)
    return false;
  bool Result = false;
  if (Callee->doesNotAccessMemory() &&
      Callee->hasFnAttribute(Attribute::WillReturn) &&
      Callee->hasFnAttribute(Attribute::NoUnwind)) {
    Result = true;
  }
  if (Callee->getName() == "llvm.dbg.value") {
    Result = true;
  }
  return Result;
}

bool LoopPartitionGraph::isDroppableCall(CallInst *CI) const {
  Function *Callee = CI->getCalledFunction();
  if (Callee == nullptr)
    return false;
  return (Callee->getName() == "llvm.dbg.value");
}

bool LoopPartitionGraph::isPartitionOutput(Instruction *I) const {
  bool Result = false;
  for (auto &N : getNodes()) {
    if (N->hasStore(I)) {
      Result = true;
      break;
    }
  }
  return Result;
}

bool LoopPartitionGraph::createLoopPartitions(bool NoTransform) {
  std::unordered_map<BasicBlock *, LoopPartition *> ItSpaceMap;
  std::unordered_map<Instruction *, std::vector<LoopPartition *>> ITPMap;
  int Idx = 0;
  std::unordered_map<Value *, LoopPartition *> PtrToPartMap;
  std::unordered_map<Instruction *, int> InstrToCountMap;

  LLVM_DEBUG(dbgs() << "Computing iteration space\n");
  if (!computeIterationSpaceForLoopNest(L, ItSpaceMap, nullptr))
    return false;

  LLVM_DEBUG(dbgs() << "Finding partition outputs\n");
  BasicBlock *Header = L->getHeader();
  assert(Header != nullptr);
  for (BasicBlock *BB : L->blocks()) {
    auto SearchBB = ItSpaceMap.find(BB);
    if (SearchBB == ItSpaceMap.end()) {
      LLVM_DEBUG(dbgs() << "No iteration space for BB: " << BB->getName()
                        << "\n");
      return false;
    }
    LoopPartition *BBPart = SearchBB->second;

    // Now go through instructions in the Basic Block.
    for (Instruction &I : *BB) {
      Instruction *Instr = &I;
      assert(InstrToCountMap.find(Instr) == InstrToCountMap.end());
      InstrToCountMap[Instr] = 0;
      if (isa<StoreInst>(Instr)) {
        StoreInst *Store = cast<StoreInst>(Instr);
        Value *PtrBase = getPtrBaseOutsideLoop(*Store, NoTransform);
        // The above method returns nullptr if the store pointer cannot be
        // determined or the pointer is modified within the loop.
        if (PtrBase == nullptr) {
          LLVM_DEBUG(dbgs() << "Unable to determine pointer base for ["
                            << *Store << "]\n");
          return false;
        }

        // Otherwise, check if the partition for the PtrBase exists. If so,
        // check if the partition for the PtrBase and the current BB trivially
        // match. If not, then abort as well since we cannot handle writing to
        // the same array with different iteration spaces.
        auto Search = PtrToPartMap.find(PtrBase);
        if (Search != PtrToPartMap.end()) {
          LoopPartition *ExistingPart = Search->second;
          if (!ExistingPart->hasMatchingIterationSpace(*BBPart)) {
            LLVM_DEBUG(dbgs() << "BB Partitions do not match for pointer base ["
                              << *PtrBase << "]\n");
            return false;
          }

          // Since the martitions match, then add the new store to the
          // existing partition.
          ExistingPart->addStore(Instr);
          ITPMap[Instr].push_back(ExistingPart);
        } else {
          LoopPartition *NewPart = createNewPartition(*BBPart, Instr).get();
          NewPart->setName(std::string("cone.") + std::to_string(Idx + 1));
          ITPMap[Instr].push_back(NewPart);
          PtrToPartMap[PtrBase] = NewPart;
          Idx++;
        }
      } else if (isLoopGeneratedValue(L, Instr)) {
        // In this case the value is generated by the loop and then used
        // outside. That is a common case in GEMM like applications, so we
        // should treat this value like a store, which will appear downstream.
        LoopPartition *NewPart = createNewPartition(*BBPart, Instr).get();
        NewPart->setName(std::string("cone.") + std::to_string(Idx + 1));
        ITPMap[Instr].push_back(NewPart);
        Idx++;
      } else if (hasUseOutsideOfLoop(*Instr)) {
        // If we have an instruction whose use would be required after the
        // transformation then abort. At the moment, transforms using
        // LoopPartition data structure cannot handle such scenarios; however,
        // this should be something we can extend in the future.
        LLVM_DEBUG(dbgs() << "Unable to handle: " << *Instr << "\n");
        return false;
      } else {
        // See if the instruction in question is used in a subloop, and it is
        // not an induction variable. Also, check if the instruction is used
        // in a BB that belongs to a different partition.
        if (BBPart->isPartitionIndVar(Instr))
          continue;
        Loop *IML = LI->getLoopFor(Instr->getParent());
        for (User *U : Instr->users()) {
          Instruction *I = cast<Instruction>(U);
          BasicBlock *IBB = I->getParent();
          bool HasOtherPartUse = false;
          if (IBB != Header) {
            auto CandPart = ItSpaceMap.find(IBB);
            if ((CandPart != ItSpaceMap.end()) && (CandPart->second != BBPart))
              HasOtherPartUse = true;
          }
          if (HasOtherPartUse || isBlockPartOfSubLoop(IML, IBB)) {
            LoopPartition *NewPart = createNewPartition(*BBPart, Instr).get();
            NewPart->setName(std::string("cone.") + std::to_string(Idx + 1));
            ITPMap[Instr].push_back(NewPart);
            Idx++;
            break;
          }
        }
      }
    }
  }

  clearEmptyPartitions();
  LLVM_DEBUG(dbgs() << "Filling partitions with instructions\n");
  // Now make the cones of operations that drive the stores and add them to
  // partitions for each store.
  for (auto &N : getNodes()) {
    LoopPartition *LP = N.get();
    std::queue<Instruction *> Queue;
    std::unordered_set<Instruction *> Visited;

    LLVM_DEBUG(dbgs() << "---===[ New Partition ]===---\n");
    Loop *IML = nullptr; // Innermost Loop in which the "store" resides.
    for (Instruction *Store : LP->getStoreSet()) {
      LLVM_DEBUG(dbgs() << " * store [" << *Store << "]\n");
      Queue.push(Store);
      if (IML == nullptr)
        IML = LI->getLoopFor(Store->getParent());
    }
    assert(IML != nullptr);

    PHINode *IndVar = LP->getInnerLoopIndVar();
    Instruction *StepInstr = LP->getStepInstrForIndVar(IndVar);
    if (StepInstr == nullptr) {
      return false;
    }
    // What we will do now is find every instruction that drives our set of
    // stores AND is confined to the innermost loop. The objective here is
    // to locate all instructions that need to be in a given partition having
    // the same iteration space. Doing so will allow us to identify any
    // instructions that we need to source data from and those our particular
    // partitioning algorithm missed. In the latter case (as will become clear
    // later), we will abort the process of creating partitions, as it clearly
    // failed to do so in a legal manner.
    SetVector<Instruction *> InstSet;
    while (!Queue.empty()) {
      Instruction *Instr = Queue.front();
      Queue.pop();

      LLVM_DEBUG(dbgs() << "Processing [" << *Instr << "]\n");
      // For the moment ignore calls. We will enhance this to support calls
      // with no side effects later.
      if (isa<CallInst>(Instr)) {
        if (!isWhiteListCall(dyn_cast<CallInst>(Instr))) {
          return false;
        }
      }

      if (Visited.count(Instr) > 0)
        continue;
      Visited.insert(Instr);

      if (handlingPHINodes(LP, Instr, IndVar, IML))
        continue;

      if ((Instr == IndVar) || (Instr == StepInstr))
        continue;

      // Otherwise, process instruction inputs.
      for (unsigned Index = 0; Index < Instr->getNumOperands(); Index++) {
        Value *Operand = Instr->getOperand(Index);

        // For the moment do not handle inline operators.
        if (!isa<Instruction>(Operand) && isa<Operator>(Operand)) {
          return false;
        }

        if (isa<Instruction>(Operand)) {
          Instruction *Input = dyn_cast<Instruction>(Operand);
          // Skip induction variable. We know about it already.
          if ((Input == IndVar) || isPartitionOutput(Input))
            continue;

          // Skip instructions from outside of this loop or its child loops.
          BasicBlock *PBB = Input->getParent();
          if (!IML->contains(PBB) || isBlockPartOfSubLoop(IML, PBB))
            continue;

          // If source input is not in the same BB, then add the branch
          // instruction from the source BB. This is effectively an indirect
          // input due to control flow.
          if (PBB != Instr->getParent()) {
            Instruction *SrcBR = PBB->getTerminator();
            if (!isa<BranchInst>(SrcBR))
              continue;
            Queue.push(SrcBR);
            if (Visited.count(SrcBR) == 0)
              InstSet.insert(SrcBR);
          }

          // Now we can have a scenario here where we end up going to
          // previous loop iteration. That is OK, since we won't visit the
          // same node twice. But, when generating the code we will have to
          // realize this happened and adjust indices or code generation
          // accordingly.
          if (Visited.count(Input) == 0) {
            Queue.push(Input);
            InstSet.insert(Input);
            // Mark this instruction as belonging to the given partition.
            if (ITPMap.find(Input) == ITPMap.end())
              ITPMap[Input].push_back(LP);
            else {
              auto Search =
                  std::find(ITPMap[Input].begin(), ITPMap[Input].end(), LP);
              if (Search == ITPMap[Input].end())
                ITPMap[Input].push_back(LP);
            }
          }
        }
      }
    }
    // Now we need to sort inputs topologically, before inserting them into the
    // partition.
    addInputsToPartitionTopologically(InstSet, LP);

    for (auto *I : LP->getStoreSet()) {
      assert(InstrToCountMap.find(I) != InstrToCountMap.end());
      InstrToCountMap[I]++;
    }

    for (auto *I : LP->getInputInstrSet()) {
      assert(InstrToCountMap.find(I) != InstrToCountMap.end());
      InstrToCountMap[I]++;
    }

    LLVM_DEBUG(dbgs() << "Partition size = " << LP->getInputInstrSet().size()
                      << "\n");
  }

  // Now perform validation.
  unsigned ErrCount = validatePartitions(InstrToCountMap);

  createUseDefEdges();
  createReadWriteEdges();
  generateInstOrderForPartitions();

  // If there is no transformation, ignore the error
  return (ErrCount == 0) || NoTransform;
}

bool LoopPartitionGraph::haveMatchingValues(Value *V0, Value *V1, Value *Start,
                                            Value *End) const {
  // Strip zext/sext
  V0 = stripZSExt(V0);
  V1 = stripZSExt(V1);
  Start = stripZSExt(Start);
  End = stripZSExt(End);
  // First match V0.
  Value *V0Match = nullptr;
  if (isa<ConstantInt>(V0)) {
    ConstantInt *V0I = dyn_cast<ConstantInt>(V0);
    if (isa<ConstantInt>(Start)) {
      ConstantInt *SC = dyn_cast<ConstantInt>(Start);
      if ((SC->isNegative() && (SC->getSExtValue() == V0I->getSExtValue())) ||
          (!SC->isNegative() && (SC->getZExtValue() == V0I->getZExtValue())))
        V0Match = End;
    }
    if (isa<ConstantInt>(End)) {
      ConstantInt *SC = dyn_cast<ConstantInt>(End);
      if ((SC->isNegative() && (SC->getSExtValue() == V0I->getSExtValue())) ||
          (!SC->isNegative() && (SC->getZExtValue() == V0I->getZExtValue())))
        V0Match = End;
    }
  } else {
    // This is a variable.
    if (V0 == Start)
      V0Match = Start;
    else if (V0 == End)
      V0Match = End;
    else
      return false;
  }
  if (V0Match == nullptr)
    return false;

  // Now Match V1
  Value *Other = (V0Match == Start) ? End : Start;
  if (isa<ConstantInt>(V1) && isa<ConstantInt>(Other)) {
    ConstantInt *V1I = dyn_cast<ConstantInt>(V1);
    ConstantInt *SC = dyn_cast<ConstantInt>(Other);
    if ((SC->isNegative() && (SC->getSExtValue() == V1I->getSExtValue())) ||
        (!SC->isNegative() && (SC->getZExtValue() == V1I->getZExtValue())))
      return true;
  } else {
    // This is a variable.
    if (V1 == Other)
      return true;
  }
  return false;
}

bool LoopPartitionGraph::isUnconditionalSuccessor(BasicBlock *Src,
                                                  BasicBlock *Dst) const {
  assert(Src != nullptr);
  assert(Dst != nullptr);

  Instruction *Term = Src->getTerminator();
  if ((Term == nullptr) || !isa<BranchInst>(Term))
    return false;

  BranchInst *Branch = dyn_cast<BranchInst>(Term);
  if (!Branch->isUnconditional())
    return false;

  return (Branch->getSuccessor(0) == Dst);
}

bool LoopPartitionGraph::isBlockPartOfSubLoop(Loop *SL, BasicBlock *BB) const {
  for (Loop *LL : SL->getSubLoopsVector()) {
    if (LL->contains(BB))
      return true;
  }
  return false;
}

// Check if an instruction has a use outside of a given loop.
bool LoopPartitionGraph::hasUseOutsideOfLoop(const Instruction &Instr) const {
  for (const User *U : Instr.users()) {
    const Instruction *I = cast<Instruction>(U);
    if (!L->contains(I))
      return true;
  }

  return false;
}

// Check if an instruction has exactly one use outside of a given loop.
bool LoopPartitionGraph::hasOneUseOutsideOfLoop(const Instruction &Inst) const {
  int Count = 0;
  for (const User *U : Inst.users()) {
    const Instruction *I = cast<Instruction>(U);
    if (!L->contains(I))
      Count++;
  }
  return (Count == 1);
}

bool LoopPartitionGraph::isLoopGeneratedValue(Loop *SubL,
                                              Instruction *Instr) const {
  int Count = 0;
  int CountOutside = 0;
  int FeedsBackToPHI = 0;
  BasicBlock *BB = Instr->getParent();
  assert(BB != nullptr);

  Loop *LoopForInstr = LI->getLoopFor(BB);

  for (const User *U : Instr->users()) {
    const Instruction *I = cast<Instruction>(U);
    Count++;

    if (!LoopForInstr->contains(I))
      CountOutside++;
    else {
      if (isa<PHINode>(I))
        FeedsBackToPHI++;
    }
  }

  // If the only uses are PHI nodes in this loop and outside values,
  // then this value is generated by this loop - potentially stored in another
  // location.
  return ((CountOutside + FeedsBackToPHI == Count) && (CountOutside > 0));
}

bool LoopPartitionGraph::isStoreToAliasableMem(const StoreInst &Store) const {
  const Value *Ptr = Store.getPointerOperand();
  assert(Ptr != nullptr);
  assert(isa<GetElementPtrInst>(Ptr));

  const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Ptr);
  const Value *Src = GEP->getPointerOperand();
  const Value *V = getUnderlyingObject(Src);

  // Here we have a couple of cases
  // V == Src -> means this is the object, so we have to do more digging and
  //             see if it is a global or function argument. In the latter
  //             case, check if the argument is labeled as restrict to ensure
  //             the memory array pointed to does not overlap anything. In the
  //             former case, just give up. Otherwise, check that V is not a
  //             from some structure, which could make it an indirectly
  //             aliased array.
  // V != Src -> means the Src is masking the true source of the data. If the
  //             source of the data is an argument/global then handle them as
  //             in the case above. Otherwise, make sure this is not loaded
  //             from some other structure.
  // Good news is - the above cases are identical in how we handle them, so
  // yay.
  if (isa<Argument>(V)) {
    const Argument *Arg = cast<Argument>(V);
    return !Arg->hasNoAliasAttr();
  }
  return false;
}

// Check if the given phi node is a loop carried dependency that represents
// a store from the previous iteration. If so, return the said store as
// argument.
bool LoopPartitionGraph::isCarriedStoreInput(PHINode *LoopCarriedDep, Loop *SL,
                                             StoreInst **TargetStore) const {
  assert(LoopCarriedDep != nullptr);
  assert(TargetStore != nullptr);

  unsigned TotalIncoming = LoopCarriedDep->getNumIncomingValues();
  for (unsigned Idx = 0; Idx < TotalIncoming; Idx++) {
    BasicBlock *SourceBlock = LoopCarriedDep->getIncomingBlock(Idx);
    Value *SrcVal = LoopCarriedDep->getIncomingValue(Idx);

    if ((!SL->contains(SourceBlock)) || (!isa<Instruction>(SrcVal)))
      continue;

    // At this point, the source is from the current loop, so check if it
    // drives any StoreInst.
    for (auto &SrcUse : SrcVal->uses()) {
      User *Dest = SrcUse.getUser();
      if (isa<StoreInst>(Dest)) {
        *TargetStore = dyn_cast_or_null<StoreInst>(Dest);
        return true;
      }
    }
  }
  return false;
}

// This function extends loop induction variable search by examining
// the non-obvious cases where the loop latch does not contain the condition
// for loop exit, and instead that check is present in the predecessor of the
// loop latch.
PHINode *LoopPartitionGraph::getInductionVariableAndBounds(
    Loop *SL, Value **StartValue, Value **EndValue, Instruction **StepInst,
    ConstantInt **ConstIncr, bool &Increasing, bool &InclusiveEnd,
    bool &IsUnsigned, std::vector<Instruction *> &ItInsts) const {
  assert(SE != nullptr);
  Value *StepValue = nullptr;
  PHINode *IndVar = nullptr;
  BasicBlock *Latch = SL->getLoopLatch();
  BasicBlock *PH = SL->getLoopPreheader();

  if ((Latch == nullptr) || (PH == nullptr))
    return nullptr;

  Instruction *Term = Latch->getTerminator();
  BranchInst *Branch = dyn_cast<BranchInst>(Term);
  if (Branch == nullptr)
    return nullptr;

  ItInsts.push_back(dyn_cast<Instruction>(Branch));
  if (!Branch->isConditional()) {
    // If it is then look at the single predecessor of the latch.
    BasicBlock *PreLatch = Latch->getSinglePredecessor();

    if (PreLatch == nullptr)
      return nullptr;

    Term = PreLatch->getTerminator();
    Branch = dyn_cast_or_null<BranchInst>(Term);
    if ((Branch == nullptr) || (!Branch->isConditional()))
      return nullptr;

    // Check that the other target of the branch is OUTSIDE of this loop.
    for (unsigned int Idx = 0; Idx < Branch->getNumSuccessors(); Idx++) {
      BasicBlock *Successor = Branch->getSuccessor(Idx);

      if (Successor == Latch)
        continue;

      // If the successor ends in the same loop, then abort.
      if (L->contains(Successor))
        return nullptr;
    }
    ItInsts.push_back(dyn_cast<Instruction>(Branch));
  }

  Value *BCond = Branch->getCondition();
  if (BCond == nullptr || !isa<Instruction>(BCond))
    return nullptr;

  ICmpInst *Cond = dyn_cast_or_null<ICmpInst>(BCond);
  if (Cond == nullptr)
    return nullptr;
  else
    ItInsts.push_back(dyn_cast<Instruction>(Cond));

  Value *LatchCmpOp0 = dyn_cast_or_null<Value>(Cond->getOperand(0));
  Value *LatchCmpOp1 = dyn_cast_or_null<Value>(Cond->getOperand(1));
  auto Pred = Cond->getPredicate();
  IsUnsigned = false;

  bool LatchOnTrue =
      (Branch->getSuccessor(1) == SL->getHeader()) ? false : true;

  switch (Pred) {
  case CmpInst::ICMP_EQ:
  case CmpInst::ICMP_SGE:
  case CmpInst::ICMP_SLE:
    InclusiveEnd = LatchOnTrue;
    break;

  case CmpInst::ICMP_UGE:
  case CmpInst::ICMP_ULE:
    InclusiveEnd = LatchOnTrue;
    IsUnsigned = true;
    break;

  case CmpInst::ICMP_ULT:
  case CmpInst::ICMP_UGT:
    IsUnsigned = true;
    InclusiveEnd = !LatchOnTrue;
    break;

  default:
    InclusiveEnd = false;
    break;
  }

  BasicBlock *Header = SL->getHeader();
  assert(Header != nullptr);

  // Here we have to handle how the induction variable and its increment
  // are actually implementing the loop to ensure we get the correct bounds
  // and behaviour. There are two things to pay attention to:
  // 1. Is the bound inclusive (<=, >=) or not (<, >)
  // 2. Is the exit condition testing the induction variable or the increment
  // Note that testing the increment is to test the next value of induction
  // variable which semantically is the variable in the for(...) statement.
  // So for for (i=0; i< N; i++) to test if we should do another loop is to test
  // if stepInst < N at the end of a current iteration. That is a bit subtle,
  // but crucial for handling bounds.
  for (PHINode &IndV : Header->phis()) {
    InductionDescriptor IndDesc;
    BasicBlock *EndParent = nullptr;
    if (!InductionDescriptor::isInductionPHI(&IndV, SL, SE, IndDesc))
      continue;

    *StartValue = IndDesc.getStartValue();
    *StepInst = IndDesc.getInductionBinOp();
    const SCEV *Step = IndDesc.getStep();
    if ((*StepInst == nullptr) || (*StartValue == nullptr) || (Step == nullptr))
      continue;

    if (LatchCmpOp0 == &IndV || LatchCmpOp0 == *StepInst) {
      *EndValue = LatchCmpOp1;
      if (isa<Instruction>(LatchCmpOp1))
        EndParent = dyn_cast<Instruction>(LatchCmpOp1)->getParent();
    } else if (LatchCmpOp1 == &IndV || LatchCmpOp1 == *StepInst) {
      *EndValue = LatchCmpOp0;
      if (isa<Instruction>(LatchCmpOp0))
        EndParent = dyn_cast<Instruction>(LatchCmpOp0)->getParent();
    } else {
      *EndValue = nullptr;
    }

    // Check that loop ends based on loop invariant condition.
    if (EndParent && SL->contains(EndParent)) {
      *EndValue = nullptr;
      continue;
    }

    Value *StepInstOp1 = (*StepInst)->getOperand(1);
    Value *StepInstOp0 = (*StepInst)->getOperand(0);
    StepValue = nullptr;
    if (SE->getSCEV(StepInstOp1) == Step)
      StepValue = StepInstOp1;
    else if (SE->getSCEV(StepInstOp0) == Step)
      StepValue = StepInstOp0;

    // case 1:
    // IndV = phi[{InitialValue, preheader}, {StepInst, latch}]
    // StepInst = IndVar + step
    // cmp = StepInst <op> EndValue
    if ((*StepInst == LatchCmpOp0 || *StepInst == LatchCmpOp1) ||
        (&IndV == LatchCmpOp0 || &IndV == LatchCmpOp1)) {
      IndVar = &IndV;
      break;
    }
  }

  // If you did not find an induction variable by now, abort.
  if (IndVar == nullptr)
    return nullptr;

  if ((*StartValue == nullptr) || (*EndValue == nullptr) ||
      (*StepInst == nullptr) || (StepValue == nullptr))
    return nullptr;

  // Look only at loops with constant increment.
  if (!isa<ConstantInt>(StepValue) ||
      (((*StepInst)->getOpcode() != Instruction::Add) &&
       ((*StepInst)->getOpcode() != Instruction::Sub)))
    return nullptr;

  // When increment is a subtraction, then it should be the second operand
  // as we do not handle 1-i type iteration (yet)
  if ((*StepInst)->getOpcode() == Instruction::Sub) {
    if ((*StepInst)->getOperand(1) != StepValue)
      return nullptr;
  }

  // The increment we want should be a constant (No FP)
  *ConstIncr = dyn_cast<ConstantInt>(StepValue);
  Increasing = ((*ConstIncr)->isNegative())
                   ? ((*StepInst)->getOpcode() == Instruction::Sub)
                   : ((*StepInst)->getOpcode() == Instruction::Add);

  ItInsts.push_back(*StepInst);
  ItInsts.push_back(dyn_cast<Instruction>(IndVar));

  // Now return the induction variable.
  return IndVar;
}

bool LoopPartitionGraph::isLoopPartOfNest(Loop *CurrentLoop) const {
  Loop *TmpLoop = CurrentLoop;
  while (TmpLoop != nullptr) {
    if (TmpLoop == L)
      return true;
    TmpLoop = TmpLoop->getParentLoop();
  }
  return false;
}

BasicBlock *LoopPartitionGraph::findClosestDominator(Loop *SL,
                                                     BasicBlock *Header,
                                                     BasicBlock *BB) const {
  // Given a loop, its header and a BB, go backwards from the BB up-to
  // the header to find a basic block that dominates BB, closest to BB.
  // To do this, perform backwards breadth first search and stop when you see
  // either the header or a BB that DT says dominates the given BB.
  std::queue<BasicBlock *> Q;
  if (Header == BB)
    return Header;

  for (auto It = pred_begin(BB); It != pred_end(BB); ++It)
    if ((*It != nullptr) && (SL->contains(*It)))
      Q.push(*It);

  DomTreeNode *Dst = DomTree->getNode(BB);
  while (!Q.empty()) {
    BasicBlock *Cur = Q.front();
    Q.pop();
    DomTreeNode *Src = DomTree->getNode(Cur);
    if ((DomTree->dominates(Src, Dst)) || (Cur == Header))
      return Cur;
    for (auto It = pred_begin(Cur); It != pred_end(Cur); ++It)
      if ((*It != nullptr) && (SL->contains(*It)))
        Q.push(*It);
  }
  return nullptr;
}

bool LoopPartitionGraph::computeIterationSpaceForBBs(
    Loop *Nest, std::unordered_map<BasicBlock *, LoopPartition *> &ItSpaceMap) {
  // Iteration space for the header must be present or the algorithm fails.
  BasicBlock *Header = Nest->getHeader();
  LLVM_DEBUG(dbgs() << "computeIterationSpaceForBBs for [" << Nest->getName()
                    << "]\n");
  auto HeaderItSpace = ItSpaceMap.find(Header);
  if (HeaderItSpace == ItSpaceMap.end()) {
    return false;
  }
  std::unordered_set<BasicBlock *> Visited;
  // First create a list of BBs to go through, starting with the header.
  // Only include BBs that are part of this loop.
  std::queue<BasicBlock *> BBQueue;
  BBQueue.push(Header);
  Visited.insert(Header);

  while (!BBQueue.empty()) {
    BasicBlock *CurBB = BBQueue.front();
    BBQueue.pop();
    // Lets look at how we are transiting to the next basic block within
    // the loop.
    Instruction *Terminator = CurBB->getTerminator();
    if ((Terminator == nullptr) || (!isa<BranchInst>(Terminator)))
      return false;

    BranchInst *Branch = cast<BranchInst>(Terminator);
    if (Branch->isUnconditional()) {
      BasicBlock *Next = Branch->getSuccessor(0);
      assert(Next != nullptr);

      // If we are going to add a BB into stack for processing,
      // it has to be one that is contained within this loop L and not yet
      // visited.
      if (!Nest->contains(Next) || Visited.count(Next)) {
        continue;
      }
      // Only visit basic blocks not visited before.
      if (ItSpaceMap.find(Next) == ItSpaceMap.end()) {
        // Since the branch is unconditional, use the closest common dominator's
        // iteration space.
        BasicBlock *SrcDom = findClosestDominator(Nest, Header, Next);
        if (SrcDom == nullptr)
          return false;
        auto Search = ItSpaceMap.find(SrcDom);
        if (Search == ItSpaceMap.end())
          return false;
        // If CurBB partition is not exactly the same SrcDom partition, then
        // there may be partitions in between, so make a new partition, even if
        // it has the same parameters. This is because we want to enable
        // creation of stores from one partition to the next, especially when
        // source and destination are separated by a partition.
        if ((SrcDom != CurBB) && (ItSpaceMap[CurBB] != Search->second)) {
          ItSpaceMap[Next] =
              createNewPartition(*(Search->second), nullptr).get();
        } else
          ItSpaceMap[Next] = Search->second;
        if (Visited.count(Next)) {
          LLVM_DEBUG(dbgs() << "BB is visited twice in computeIterationSpace: " << Next->getName()
                            << "\n");
        } else {
          BBQueue.push(Next);
          Visited.insert(Next);
        }
      }
    } else {
      // Conditional branch so we have true/false successors.
      Value *ConditionVal = Branch->getCondition();
      Instruction *Condition = dyn_cast_or_null<Instruction>(ConditionVal);
      LoopPartition *CurPart = ItSpaceMap[CurBB];

      // Now we get into control-flow handling
      // Check that condition is not nullptr and if so determine the
      // format of the check. If it does not match a supported way of
      // modifying the iteration space, then set Condition to nullptr so that
      // we don't bother creating a new partition for this BB.
      ICmpInst *CmpCond = dyn_cast_or_null<ICmpInst>(Condition);
      bool IsItSpaceCondition = false;
      if (CmpCond != nullptr) {
        // In this could be an iteration space condition.
        PHINode *IndVar = CurPart->getInnerLoopIndVar();
        if ((CmpCond->getOperand(0) == IndVar) ||
            (CmpCond->getOperand(1) == IndVar))
          IsItSpaceCondition = true;
      }

      // If it is not an iteration space condition, then set the target it space
      // to match closest dominator of each target.
      if (!IsItSpaceCondition) {
        for (unsigned SuccIdx = 0; SuccIdx < Branch->getNumSuccessors();
             SuccIdx++) {
          BasicBlock *Next = Branch->getSuccessor(SuccIdx);
          if (!Nest->contains(Next) || Visited.count(Next))
            continue;
          BasicBlock *SrcDom = findClosestDominator(Nest, Header, Next);
          if (SrcDom == nullptr)
            return false;
          auto Search = ItSpaceMap.find(SrcDom);
          if (Search == ItSpaceMap.end())
            return false;
          // If CurBB partition is not exactly the same SrcDom partition, then
          // there may be partitions in between, so make a new partition, even
          // if it has the same parameters. This is because we want to enable
          // creation of stores from one partition to the next, especially when
          // source and destination are separated by a partition.
          if ((SrcDom != CurBB) && (ItSpaceMap[CurBB] != Search->second)) {
            ItSpaceMap[Next] =
                createNewPartition(*(Search->second), nullptr).get();
          } else
            ItSpaceMap[Next] = Search->second;
          if (Visited.count(Next)) {
            LLVM_DEBUG(dbgs() << "BB is visited twice in computeIterationSpace:" << Next->getName()
                              << "\n");
          } else {
            BBQueue.push(Next);
            Visited.insert(Next);
          }
        }
      } else {
        for (unsigned SuccIdx = 0; SuccIdx < Branch->getNumSuccessors();
             SuccIdx++) {
          // New bound conditions.
          BasicBlock *Next = Branch->getSuccessor(SuccIdx);
          struct LoopPartitionBounds LPB;
          LPB.IndVar = CurPart->getInnerLoopIndVar();
          LPB.Step = nullptr;

          // Check if successor is part of the loop and not the
          // header for which we already have an iteration space.
          if (!Nest->contains(Next) || Next == Header || Visited.count(Next))
            continue;

          CurPart->getInnerLoopBoundsFromCmpInst(
              CmpCond, LPB.Start, LPB.End, LPB.InclusiveStart, LPB.InclusiveEnd,
              (SuccIdx == 0));

          // Sanity check.
          if (LPB.Start == nullptr || LPB.End == nullptr)
            return false;

          // Here we create a new partition.
          LoopPartition *NewPart = createNewPartition(*CurPart, nullptr).get();
          NewPart->updateIterationDimension(LPB);
          if (ItSpaceMap.find(Next) == ItSpaceMap.end()) {
            if (Visited.count(Next)) {
              LLVM_DEBUG(dbgs() << "BB is visited twice in computeIterationSpace:" << Next->getName()
                                << "\n");
            } else {
              BBQueue.push(Next);
              Visited.insert(Next);
            }
          }
          ItSpaceMap[Next] = NewPart;

          // Mark iteration space instructions in the new partition.
          NewPart->markItSpaceInstruction(Condition);
          NewPart->markItSpaceInstruction(Branch);
        }
      }
    }
  }
  return true;
}

bool LoopPartitionGraph::isPHIFromPartitionStore(PHINode *PHI,
                                                 LoopPartition *Partition) {
  for (unsigned Idx = 0; Idx < PHI->getNumIncomingValues(); Idx++) {
    Value *V = PHI->getIncomingValue(Idx);
    if (isa<Instruction>(V)) {
      if (Partition->hasStore(dyn_cast_or_null<Instruction>(V)))
        return true;
    }
  }
  return false;
}

BasicBlock *
LoopPartitionGraph::createBB(std::shared_ptr<LoopPartition> Partition,
                             int Index, Function *Parent, BasicBlock *BeforeBB,
                             const std::string Suffix, bool IsTemporary) {
  std::string Name =
      Partition->getName() + "_" + std::to_string(Index) + Suffix;
  BasicBlock *BB =
      BasicBlock::Create(Parent->getContext(), Name, Parent, BeforeBB);

  // Also, store a pointer to the new BB, as we may need to know if a given BB
  // is part of the the new loop nest.
  if (!IsTemporary)
    GraphBBs.insert(BB);
  else
    TemporaryBBs.insert(BB);
  return BB;
}

BasicBlock *LoopPartitionGraph::createBB(Function *Parent, BasicBlock *BeforeBB,
                                         bool IsTemporary, Twine Name) {
  BasicBlock *BB =
      BasicBlock::Create(Parent->getContext(), Name, Parent, BeforeBB);

  // Also, store a pointer to the new BB, as we may need to know if a given BB
  // is part of the the new loop nest.
  if (!IsTemporary)
    GraphBBs.insert(BB);
  else
    TemporaryBBs.insert(BB);
  return BB;
}

bool LoopPartitionGraph::isValueOnPath(BasicBlock *Parent, BasicBlock *BB,
                                       BasicBlock *StartBB) const {
  std::unordered_set<BasicBlock *> Visited;

  // Check for trivial cases first
  if (Parent == BB)
    return true;

  if (StartBB == BB)
    return (Parent == BB);

  // Now perform deeper search.
  Visited.insert(BB);
  std::queue<BasicBlock *> Queue;
  for (auto It = pred_begin(BB); It != pred_end(BB); ++It)
    if ((*It != nullptr) && (GraphBBs.count(*It) != 0))
      Queue.push(*It);

  Visited.insert(StartBB);
  while (!Queue.empty()) {
    BasicBlock *Pred = Queue.front();
    Queue.pop();
    Visited.insert(Pred);
    if (Pred == Parent)
      return true;

    // Add Pred's predecessors to the queue.
    for (auto It = pred_begin(BB); It != pred_end(BB); ++It)
      if ((*It != nullptr) && (GraphBBs.count(*It) != 0) &&
          (Visited.count(Pred) == 0))
        Queue.push(*It);
  }

  return false;
}

// In this method we visit all PHINodes of a given BB, and update the BasicBlock
// entry to indicate path for which the value should be used. This is done
// through a depth first search of the blocks.
bool LoopPartitionGraph::updatePHINodeControlPaths(BasicBlock *BB) {
  std::vector<BasicBlock *> BBPreds;

  for (auto It = pred_begin(BB); It != pred_end(BB); ++It)
    if ((*It != nullptr) && ((GraphBBs.count(*It) != 0) || !L->contains(*It)))
      BBPreds.push_back(*It);

  assert(BBPreds.size() > 0);

  for (Instruction &Inst : *BB) {
    // After the first non-phi node, no more PHIs will be found.
    Instruction *I = &Inst;
    if (!isa<PHINode>(I))
      break;

    PHINode *PHI = dyn_cast<PHINode>(I);
    assert(PHI != nullptr);
    LLVM_DEBUG(dbgs() << "update PHI:     " << *PHI << "\n");
    unsigned Count = PHI->getNumIncomingValues();
    assert(Count <= 2);
    for (unsigned Idx = 0; Idx < Count; Idx++) {
      BasicBlock *InputBB = OldToNewBBMap[PHI->getIncomingBlock(Idx)];
      if (BBPreds.size() == 1) {
        PHI->setIncomingBlock(Idx, BBPreds[0]);
      } else {
        for (unsigned PredIdx = 0; PredIdx < BBPreds.size(); PredIdx++) {
          if (isValueOnPath(InputBB, BBPreds[PredIdx], BB)) {
            PHI->setIncomingBlock(Idx, BBPreds[PredIdx]);
          }
        }
      }
    }

    if (Count < BBPreds.size()) {
      for (BasicBlock *Pred : BBPreds) {
        if (PHI->getBasicBlockIndex(Pred) == -1) {
          UndefValue *UDV = UndefValue::get(PHI->getType());
          PHI->addIncoming(UDV, Pred);
        }
      }
    }
  }
  return true;
}

//------------------------------------------------------------------------------
// Loop Partition Graph Transformations
//------------------------------------------------------------------------------
bool LoopPartitionGraph::applyLoopOrder(std::vector<PHINode *> NewOrder) {
  // This method applies a new order of loops. This requires that loops have
  // consistent bounds.
  std::unordered_map<PHINode *, struct LoopPartitionBounds> LBMap;

  // First collect all loop bound info into LBMap.
  for (int Idx = 0; Idx < getNumNodes(); Idx++) {
    std::shared_ptr<LoopPartition> P = getNodeAt(Idx);

    // Add loop bounds to local data structure.
    for (PHINode *IV : P->getLoopOrder()) {
      if (LBMap.find(IV) == LBMap.end()) {
        if (!P->getBoundsForIndVar(IV, LBMap[IV]))
          return false;
      } else {
        if (!P->hasMatchingLoopBounds(LBMap[IV]))
          return false;
      }
    }
  }

  if (NewOrder.size() != LBMap.size())
    return false;

  // At this point we know all loops in all partitions have matching bounds so
  // we can set the order of loops to match across the board. Now add all loop
  // induction variables to all partitions. This effectively will end up
  // combining all partitions into a common loop body, but that is OK.
  for (auto &LBM : LBMap) {
    PHINode *IV = LBM.first;
    for (int Idx = 0; Idx < getNumNodes(); Idx++) {
      std::shared_ptr<LoopPartition> P = getNodeAt(Idx);
      if (!P->hasIndVar(IV))
        P->addIterationDimension(LBM.second);
    }
  }

  // Now set order of all induction variables in partition to match
  for (int Idx = 0; Idx < getNumNodes(); Idx++) {
    std::shared_ptr<LoopPartition> P = getNodeAt(Idx);
    if (!P->setLoopOrder(NewOrder))
      return false;
  }
  return true;
}

std::vector<PHINode *> LoopPartitionGraph::getIndVarList() const {
  std::vector<PHINode *> List;
  for (int Idx = 0; Idx < getNumNodes(); Idx++) {
    std::shared_ptr<LoopPartition> P = getNodeAt(Idx);
    for (PHINode *IV : P->getLoopOrder()) {
      if (find(List.begin(), List.end(), IV) == List.end())
        List.push_back(IV);
    }
  }
  return List;
}

std::unordered_set<PHINode *> LoopPartitionGraph::getIndVarSet() const {
  std::unordered_set<PHINode *> MySet;
  for (int Idx = 0; Idx < getNumNodes(); Idx++) {
    std::shared_ptr<LoopPartition> P = getNodeAt(Idx);
    for (PHINode *IV : P->getLoopOrder()) {
      if (MySet.count(IV) == 0)
        MySet.insert(IV);
    }
  }
  return MySet;
}

PHINode *LoopPartitionGraph::createStripminedLoop(
    PHINode *IV, int Size,
    std::unordered_map<PHINode *, struct LoopPartitionBounds> &LBMap) {
  // This creates a new loop by stripmining the loop referenced by IV. The new
  // loop has a step of 'Size' and it affects iterator for IV so that together
  // two loops iterate over the same space as the original IV loop. The new
  // loop is added to LBMap with appropriate start/end points and IV loop info
  // is updated accordingly as well.
  assert(Size > 1);
  BasicBlock *NestHead = L->getHeader();
  BasicBlock *ControlLogic = createBB(NestHead->getParent(), NestHead, true);
  IRBuilder<> IRB(ControlLogic);

  // First, create a new induction variable.
  PHINode *NewIV =
      IRB.CreatePHI(IV->getType(), 2, IV->getName() + ".stripmine");
  Instruction *OldStep = LBMap[IV].StepInstr;

  LBMap[NewIV].IndVar = NewIV;
  LBMap[NewIV].Start = LBMap[IV].Start;
  LBMap[NewIV].End = LBMap[IV].End;
  LBMap[NewIV].InclusiveStart = LBMap[IV].InclusiveStart;
  LBMap[NewIV].InclusiveEnd = LBMap[IV].InclusiveEnd;
  LBMap[NewIV].IsUnsigned = LBMap[IV].IsUnsigned;

  // The end and start values should match the original IV. So copy them over.
  // Create the increment of Size for NewIV.
  ConstantInt *CI = dyn_cast<ConstantInt>(
      ConstantInt::get(NewIV->getType(), Size, !LBMap[IV].IsUnsigned));
  Value *Increment = IRB.CreateAdd(NewIV, CI, "", OldStep->hasNoUnsignedWrap(),
                                   OldStep->hasNoSignedWrap());
  LBMap[NewIV].Step = CI;
  LBMap[NewIV].StepInstr = dyn_cast<Instruction>(Increment);
  assert(LBMap[NewIV].StepInstr != nullptr);

  assert(IV->getNumIncomingValues() == 2);
  for (unsigned Idx = 0; Idx < IV->getNumIncomingValues(); Idx++) {
    Value *V = IV->getIncomingValue(Idx);
    BasicBlock *BB = IV->getIncomingBlock(Idx);

    // Check if it is the feedback path
    if (V != LBMap[NewIV].Start)
      NewIV->addIncoming(Increment, ControlLogic);
    else
      NewIV->addIncoming(V, BB);
  }

  // The new loop is created, now we just need to adjust the iteration bounds
  // on the loop we stripmined.
  BasicBlock *EndEqBB = createBB(NestHead->getParent(), NestHead, true);
  IRBuilder<> EndIR(EndEqBB);

  // Write the equation for the loop iteration end to be:
  //  temp = NewIV + Size
  //  end = (LBMap[IV].End < temp) ? LBMap[IV].End : temp
  Value *Temp = EndIR.CreateAdd(NewIV, CI);
  Value *Compare = EndIR.CreateCmp(
      (LBMap[IV].IsUnsigned ? CmpInst::ICMP_ULT : CmpInst::ICMP_SLT),
      LBMap[IV].End, Temp);
  Value *Select = EndIR.CreateSelect(Compare, LBMap[IV].End, Temp);

  // Now point the old IV to the new end condition ('Select') and start
  // value 'NewIV'.
  LBMap[IV].Start = NewIV;
  LBMap[IV].End = Select;

  return NewIV;
}

bool LoopPartitionGraph::tileLoops(std::vector<PHINode *> LoopPriority,
                                   int BlockSize) {
  // When tiling loops, we take the LoopPriority list and make sure
  // to tile the specified loops by BlockSize. The newly created loops that
  // skip over BlockSize entries will be pulled to be the outermost loops
  // in the LoopPriority order. The remaining loops will remain as they are.
  // Note that since new loop induction variables are added, they will be
  // added to all partitions in this graph.
  std::unordered_map<PHINode *, struct LoopPartitionBounds> LBMap;

  // First collect all loop bound info into LBMap.
  for (int Idx = 0; Idx < getNumNodes(); Idx++) {
    std::shared_ptr<LoopPartition> P = getNodeAt(Idx);

    // Add loop bounds to local data structure.
    for (PHINode *IV : P->getLoopOrder()) {
      if (LBMap.find(IV) == LBMap.end()) {
        if (!P->getBoundsForIndVar(IV, LBMap[IV]))
          return false;
      } else {
        if (!P->hasMatchingLoopBounds(LBMap[IV]))
          return false;
      }
    }
  }
  // Make sure that the PHI nodes in LoopPriority list appear in the LBMap
  // for this graph.
  for (PHINode *IV : LoopPriority)
    if (LBMap.find(IV) == LBMap.end())
      return false;

  // To perform tiling, we need to create new loops with new iterators and
  // incrementors. The new instructions need to be placed in a BB, which we will
  // create as a means for temporary storage. These BBs will be flagged as
  // temporary storage and deleted when the LoopPartitionGraph is destroyed.
  // To make things easier, we create a new BB for each sequence of operations
  // form a single result to be used in the loop bound checking. This way, we
  // just need to make sure we copy BB contents to destination Instruction after
  // instruction without worrying about building dependency graphs.
  int Offset = 0;
  for (PHINode *IV : LoopPriority) {
    PHINode *NewIV = createStripminedLoop(IV, BlockSize, LBMap);
    if (NewIV == nullptr)
      return false;

    for (int Idx = 0; Idx < getNumNodes(); Idx++) {
      std::shared_ptr<LoopPartition> P = getNodeAt(Idx);
      P->addIterationDimension(LBMap[NewIV], Offset);
      P->updateIterationDimension(LBMap[IV]);
    }
    Offset++;
  }

  return true;
}

int LoopPartitionGraph::getNestChangeDepth(std::shared_ptr<LoopPartition> Src,
                                           std::shared_ptr<LoopPartition> Dst) {
  // Make sure partitions have in/out edges or no point in searching.
  if ((InEdges.find(Dst.get()) == InEdges.end()) ||
      (OutEdges.find(Src.get()) == OutEdges.end()))
    return 0;

  // Search the shorter list of the two.
  if (InEdges[Dst.get()].size() > OutEdges[Src.get()].size()) {
    for (auto &Edge : OutEdges[Src.get()]) {
      if (Edge.Type != DependencyType::ITSNestChange)
        continue;
      if (Edge.Destination.get() == Dst.get())
        return Edge.ITSChange;
    }
  } else {
    for (auto &Edge : InEdges[Dst.get()]) {
      if (Edge.Type != DependencyType::ITSNestChange)
        continue;
      if (Edge.Source.get() == Src.get())
        return Edge.ITSChange;
    }
  }
  // In case of no change, or lack of ITSNestChange edge, return 0;
  return 0;
}

void LoopPartitionGraph::adjustPartitionStoresForUseDef(
    std::shared_ptr<LoopPartition> Src, std::shared_ptr<LoopPartition> Dst) {
  // Now check if this caused a dependency between partitions, from Src to Dst.
  std::vector<Instruction *> NewStoresLP;
  for (auto *I : Src->getInputInstrSet()) {
    for (User *U : I->users()) {
      Instruction *UI = dyn_cast_or_null<Instruction>(U);
      if (Dst->hasInputInstr(UI) || Dst->hasStore(UI)) {
        NewStoresLP.push_back(I);
        break;
      }
    }
  }

  // If so then make the instructions into partition outputs and create
  // dependency edges.
  for (auto *I : NewStoresLP) {
    Src->removeInputOrStore(I);
    Src->addStore(I);
    for (User *U : I->users()) {
      Instruction *UI = dyn_cast_or_null<Instruction>(U);
      if (Dst->hasInputInstr(UI) || Dst->hasStore(UI)) {
        createEdge(Src, I, Dst, UI, DependencyType::UseDef);
      }
    }
  }
}

bool LoopPartitionGraph::separatePartition(
    std::shared_ptr<LoopPartition> LP,
    std::unordered_set<Instruction *> &Insts) {
  assert(LP != nullptr);
  assert(LP.get() != nullptr);

  // Copy partition with iteration space bounds.
  std::shared_ptr<LoopPartition> NewLP =
      createNewPartition(*(LP.get()), nullptr);
  assert(NewLP != nullptr);
  NewLP->setName(LP->getName() + ".split");
  for (Instruction *I : Insts) {
    if (isa<PHINode>(I)) {
      PHINode *PHI = dyn_cast<PHINode>(I);
      assert(PHI != nullptr);
      if (LP->hasIndVar(PHI))
        continue;
    }
    // Move instruction to the new partition
    if (LP->hasInputInstr(I))
      NewLP->addInstruction(I);
    if (LP->hasStore(I))
      NewLP->addStore(I);
    LP->removeInputOrStore(I);
  }
  // Now check if this caused a dependency between partitions.
  adjustPartitionStoresForUseDef(LP, NewLP);
  adjustPartitionStoresForUseDef(NewLP, LP);

  // Generate instruction order for the graph now that partitions were updated.
  generateInstOrderForPartitions();
  return true;
}

const std::vector<struct LoopPartitionEdge> &
LoopPartitionGraph::getInEdges(std::shared_ptr<LoopPartition> LP) const {
  auto Search = InEdges.find(LP.get());
  assert(Search != InEdges.end());
  return Search->second;
}

const std::vector<struct LoopPartitionEdge> &
LoopPartitionGraph::getOutEdges(std::shared_ptr<LoopPartition> LP) const {
  auto Search = OutEdges.find(LP.get());
  assert(Search != OutEdges.end());
  return Search->second;
}
bool findStoreToLargeArray(PHINode *InnerIV,
                           SmallVector<GetElementPtrInst *> &StoreMemInsts) {
  SmallVector<Instruction *> Worklist;
  for (User *User : InnerIV->users()) {
    Worklist.push_back(dyn_cast<Instruction>(User));
  }
  while (!Worklist.empty()) {
    Instruction *I = Worklist.pop_back_val();
    if (I == nullptr)
      continue;
    if (BinaryOperator *BO = dyn_cast<BinaryOperator>(I)) {
      for (User *User : BO->users()) {
        Worklist.push_back(dyn_cast<Instruction>(User));
      }
    }
    if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(I)) {
      for (User *User : GEP->users()) {
        Worklist.push_back(dyn_cast<Instruction>(User));
      }
    }
    if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
      GetElementPtrInst *MemAddr =
          dyn_cast<GetElementPtrInst>(SI->getPointerOperand());
      // Abort as soon as we see a memory addressing we don't expect
      if (MemAddr == nullptr || MemAddr->getNumOperands() != 2)
        return false;
      StoreMemInsts.push_back(MemAddr);
    }
  }
  return true;
}
Value *findMemOffset(GetElementPtrInst *MemAddr, PHINode *InnerIV) {
  Value *Offset = nullptr;
  if (MemAddr->getOperand(1) == InnerIV)
    Offset = ConstantInt::get(Type::getInt64Ty(InnerIV->getContext()), 0);
  BinaryOperator *BO = dyn_cast<BinaryOperator>(MemAddr->getOperand(1));
  if (BO != nullptr) {
    for (unsigned Idx = 0; Idx < BO->getNumOperands(); Idx++) {
      if (BO->getOperand(Idx) == InnerIV)
        Offset = BO->getOperand(1 - Idx);
    }
  }
  return Offset;
}

void createMemcpyForSmallArray(
    GetElementPtrInst *MemAddr, PHINode *OuterIV, BasicBlock *StartBB,
    BasicBlock *ExitBB, std::unordered_map<Value *, Value *> &OffsetToAlloca,
    Value *Offset, int BlockSize) {
  Type *Ty = MemAddr->getSourceElementType();
  FixedVectorType *VTy = FixedVectorType::get(Ty, BlockSize);
  IRBuilder<> AllocaBuilder(StartBB->getTerminator());
  AllocaInst *NewAI = AllocaBuilder.CreateAlloca(VTy);
  OffsetToAlloca[Offset] = NewAI;
  IRBuilder<> MemcpyBuilder(ExitBB->getFirstNonPHI());
  Value *OffsetInst = MemcpyBuilder.CreateAdd(OuterIV, Offset);
  SmallVector<Value *, 4> Indices;
  Indices.push_back(OffsetInst);
  Value *DstAddr = MemcpyBuilder.CreateGEP(Ty, MemAddr->getPointerOperand(),
                                           ArrayRef(Indices));
  DataLayout DL = StartBB->getParent()->getParent()->getDataLayout();
  Align Alignment = MemAddr->getPointerAlignment(DL);
  MemcpyBuilder.CreateMemCpy(DstAddr, Alignment, NewAI, Alignment,
                             VTy->getPrimitiveSizeInBits() / 8);
}

bool LoopPartitionGraph::processLargeMemoryAccess(
    BasicBlock *StartingBB, std::vector<PHINode *> &IndVarOrder,
    std::vector<BasicBlock *> &LoopHeaderBB,
    std::vector<BasicBlock *> &LoopExitingBB, ConstantInt *Step,
    std::unordered_map<PHINode *, PHINode *> &IVMap) {
  // Currently we are only handling stripmined loop
  if (LoopHeaderBB.size() != 2 || IndVarOrder.size() != 2)
    return true;

  int BlockSize = Step->getZExtValue();
  PHINode *OuterIV = IVMap[IndVarOrder[0]];
  PHINode *InnerIV = IVMap[IndVarOrder[1]];

  // Create the index within small arrays
  BasicBlock *InnerLoopHeader = LoopHeaderBB[1];
  IRBuilder<> IRB(InnerLoopHeader);
  IRB.SetInsertPoint(InnerLoopHeader->getFirstNonPHI());
  Value *DstOffset = IRB.CreateURem(InnerIV, Step);

  // Using a worklist, we find all the GEPs that are used
  // to store to the large array
  SmallVector<GetElementPtrInst *> StoreMemInsts;
  if (!findStoreToLargeArray(InnerIV, StoreMemInsts))
    return false;

  // This maps offsets to their respective small array allocation site
  std::unordered_map<Value *, Value *> OffsetToAlloca;

  for (GetElementPtrInst *MemAddr : StoreMemInsts) {
    LLVM_DEBUG(dbgs() << "Memory address:     " << *MemAddr << "\n");
    // Find the offset at which the store is accessing
    Value *Offset = findMemOffset(MemAddr, InnerIV);
    if (Offset == nullptr)
      continue;
    LLVM_DEBUG(dbgs() << "offset:     " << *Offset << "\n");
    // If we haven't seen the offset, we need to add:
    // 1. small array with size equal to the stripmining blocksize
    // 2. change all the geps to access the small array instead of the large
    // array
    // 3. create memcpy to store the small array into the large array
    if (OffsetToAlloca.find(Offset) == OffsetToAlloca.end()) {
      // If we see a phi node with offset incoming values we need to create
      // a new phi node that gets the respective small array.
      if (PHINode *PN = dyn_cast<PHINode>(Offset)) {
        IRBuilder<> IRB(MemAddr->getParent()->getFirstNonPHI());
        PHINode *NewPHI = IRB.CreatePHI(MemAddr->getOperand(0)->getType(),
                                        PN->getNumIncomingValues());
        for (unsigned Idx = 0; Idx < PN->getNumIncomingValues(); Idx++) {
          ConstantInt *PHInc = dyn_cast<ConstantInt>(PN->getIncomingValue(Idx));
          if (PHInc == nullptr)
            return false;
          if (OffsetToAlloca.find(PHInc) == OffsetToAlloca.end())
            createMemcpyForSmallArray(MemAddr, OuterIV, StartingBB,
                                      LoopExitingBB[0], OffsetToAlloca, PHInc,
                                      BlockSize);
          NewPHI->addIncoming(OffsetToAlloca[PHInc], PN->getIncomingBlock(Idx));
        }
        OffsetToAlloca[Offset] = NewPHI;
      } else
        createMemcpyForSmallArray(MemAddr, OuterIV, StartingBB,
                                  LoopExitingBB[0], OffsetToAlloca, Offset,
                                  BlockSize);
    }
    MemAddr->setOperand(0, OffsetToAlloca[Offset]);
    MemAddr->setOperand(1, DstOffset);
    LLVM_DEBUG(dbgs() << "store to array:     " << *OffsetToAlloca[Offset]
                      << "\n");
  }
  NumLoopsStripMined++;
  return true;
}

/// Check if ReadWrite edges are OK to enable distribution.
bool LoopPartitionGraph::checkReadWriteEdges() {
  for (auto &SrcP : getNodes()) {
    for (auto &DstP : getNodes()) {
      if (SrcP.get() == DstP.get())
        continue;
      if (hasSrcToDstEdge(SrcP, DstP)) {
        if (hasSrcToDstEdge(DstP, SrcP))
          return false;
        for (const auto &Edge : getOutEdges(SrcP)) {
          if (Edge.Source.get() == Edge.Destination.get())
            continue;
          if (Edge.Type == DependencyType::UseDef)
            continue;
          if ((Edge.Type != DependencyType::ReadWrite) || (Edge.Direction <= 0))
            return false;
          Instruction *S = Edge.SourceInstr;
          Instruction *D = Edge.DestinationInstr;
          if ((S == nullptr) || (D == nullptr))
            return false;
          if (!isa<StoreInst>(S) || !isa<LoadInst>(D))
            return false;
          StoreInst *Store = dyn_cast<StoreInst>(S);
          LoadInst *Load = dyn_cast<LoadInst>(D);
          GetElementPtrInst *SPtr =
              dyn_cast<GetElementPtrInst>(Store->getPointerOperand());
          GetElementPtrInst *LPtr =
              dyn_cast<GetElementPtrInst>(Load->getPointerOperand());
          if ((SPtr == nullptr) || (LPtr == nullptr))
            return false;
          if (SPtr->getPointerOperand() != LPtr->getPointerOperand())
            return false;
        }
      }
    }
  }
  return true;
}

/// Handle PHI nodes are either induction variables or loop-carried stores.
bool LoopPartitionGraph::handlingPHINodes(LoopPartition *LP, Instruction *Instr,
                                          PHINode *IndVar, Loop *IML) {
  BasicBlock *Header = IML->getHeader();
  assert(Header != nullptr);
  if (!isa<PHINode>(Instr) || Instr->getParent() != Header)
    return false;
  // For PHI nodes there are a few scenarios we will handle for now. The first
  // is that the PHI represents a loop carried dependency, which also drives a
  // store instruction. In such a case, we are just saving ourselves a load and
  // we just need to be mindful that we handle that correctly. The second case
  // we will handle is one where instead of a store, this PHI takes as input a
  // partition output (a partition store) that may be an LCSSA value. Since we
  // arrived at this PHI by traversing from a partition output, then this PHI
  // drives the output and iteratively updates it through each iteration. In
  // this case, for safety, check that the partition has exactly one output. If
  // not, the dependency may be too complex for us to handle at the moment, so
  // we will conservatively abort. The final scenario we handle is when the PHI
  // is the induction variable and we expect to see this especially when loading
  // values in a loop.
  StoreInst *TargetStore = nullptr;
  PHINode *LoopCarriedDep = dyn_cast_or_null<PHINode>(Instr);
  bool IsIndVar = (LoopCarriedDep == IndVar);
  bool IsCarriedStore = isCarriedStoreInput(LoopCarriedDep, IML, &TargetStore);
  bool IsCarriedPartitionOutput = (LP->getUniqueStore() != nullptr) &&
                                  isPHIFromPartitionStore(LoopCarriedDep, LP);
  LLVM_DEBUG(dbgs() << "Instr [" << *Instr << "] = " << IsIndVar << " | "
                    << IsCarriedStore << " | " << IsCarriedPartitionOutput
                    << "\n");
  if (!IsIndVar && !IsCarriedStore && !IsCarriedPartitionOutput) {
    LLVM_DEBUG(dbgs() << "Loop-carried dependency issue: " << *LoopCarriedDep
                      << "\n");
    // Mark this partition as having a problematic PHI, which should indicate to
    // user that there may be issues with transforms performed on this
    // partition.
    LP->setProblemPHI();
  }
  return true;
}

/// Perform validation to the partition.
unsigned LoopPartitionGraph::validatePartitions(
    std::unordered_map<Instruction *, int> &InstrToCountMap) {
  // Each instruction must belong to a single partition, with the exception of
  // instructions that define iteration space.
  LLVM_DEBUG(dbgs() << "Partition validation:\n");
  unsigned ErrCount = 0;
  for (auto &Item : InstrToCountMap) {
    if (Item.second == 0) {
      bool IsError = true;

      for (auto &N : getNodes()) {
        LoopPartition *P = N.get();
        if (P->isItSpaceInstruction(Item.first))
          IsError = false;
      }

      // Handle droppable calls (like llvm.dbg.value)
      if (isa<CallInst>(Item.first)) {
        if (isDroppableCall(dyn_cast<CallInst>(Item.first)))
          IsError = false;
      }

      if (!IsError)
        continue;

      // Handle branches
      if (isa<BranchInst>(Item.first)) {
        BranchInst *Branch = dyn_cast<BranchInst>(Item.first);
        if (Branch->isUnconditional())
          IsError = false;
        else {
          Value *Cond = Branch->getCondition();
          if (isa<ICmpInst>(Cond)) {
            ICmpInst *ICond = dyn_cast<ICmpInst>(Cond);
            Value *V0 = ICond->getOperand(0);
            Value *V1 = ICond->getOperand(1);
            bool ExitLoop = false;
            for (auto &N : getNodes()) {
              for (auto &IVBPair : N->getLoopBounds()) {
                auto &LPB = IVBPair.second;
                if (haveMatchingValues(V0, V1, LPB.Start, LPB.End)) {
                  IsError = false;
                  ExitLoop = true;
                  break;
                }
              }
              if (ExitLoop)
                break;
            }
          }
        }
      }

      if (IsError) {
        LLVM_DEBUG(dbgs() << "Instruction [" << *(Item.first)
                          << "] count = " << Item.second << "\n");
        ErrCount++;
      }
    }
  }
  LLVM_DEBUG(dbgs() << "Total errors found = " << ErrCount << "\n");
  LLVM_DEBUG(dbgs() << "Partition count = " << getNodes().size() << "\n");
  return ErrCount;
}

bool LoopPartitionGraph::hasAnyExitPHIs() const {
  BasicBlock *PH = nullptr;
  BasicBlock *LoopExit = nullptr;
  BasicBlock *GuardBlock = nullptr;
  getKeyLoopBlocks(&PH, &LoopExit, &GuardBlock, L);
  return (isa<PHINode>(&(LoopExit->front())));
}

/// Split the partitions that contains target instructions.
bool LoopPartitionGraph::createEdgesBetweenMallocPartitions() {
  std::shared_ptr<LoopPartition> PrevNode = nullptr;
  std::shared_ptr<LoopPartition> PrevRest = nullptr;
  std::shared_ptr<LoopPartition> FirstRest = nullptr;
  LLVM_DEBUG(dbgs() << "Creating ITSNestChange edges between partitions\n");
  for (auto &Curr : getNodes()) {
    if (RestNodes.count(Curr) == 0) {
      // Create an edge with ITSNestChange = 1 between each pair of "malloc"
      // partitions in order to split them into individual loops.
      if (PrevNode != nullptr) {
        createEdge(PrevNode, Curr, 1);
        LLVM_DEBUG(dbgs() << "Creating edge: " << PrevNode->getName()
                          << " ----[ 1 ]---> " << Curr->getName() << "\n");
      }
      PrevNode = Curr;
    } else {
      // Create an edge with ITSNestChange = 0 between each pair of "rest"
      // partitions in order to maintain the original loop structure.
      if (PrevRest != nullptr) {
        createEdge(PrevRest, Curr, 0);
        LLVM_DEBUG(dbgs() << "Creating edge: " << PrevRest->getName()
                          << " ----[ 0 ]---> " << Curr->getName() << "\n");
      } else {
        FirstRest = Curr;
      }
      PrevRest = Curr;
    }
  }
  if (PrevNode == nullptr)
    return false;
  // Connect the last "malloc" partition and the first "rest" partition with an
  // edge to make sure the "malloc" partitions are placed before "rest"
  // partitions.
  if (FirstRest != nullptr) {
    createEdge(PrevNode, FirstRest, 1);
    LLVM_DEBUG(dbgs() << "Creating edge: " << PrevNode->getName()
                      << " ----[ 1 ]---> " << FirstRest->getName() << "\n");
  }
  return true;
}

/// Create partitions based on stores from the the target malloc calls.
bool LoopPartitionGraph::createMallocPartitions() {
  std::unordered_map<BasicBlock *, LoopPartition *> ItSpaceMap;
  std::unordered_map<Instruction *, std::vector<LoopPartition *>> ITPMap;
  std::unordered_map<Value *, LoopPartition *> PtrToPartMap;
  std::unordered_map<Value *, LoopPartition *> SrcToPartMap;
  std::unordered_map<Instruction *, int> InstrToCountMap;
  unsigned RestIdx = 1;
  unsigned MallocIdx = 1;

  LLVM_DEBUG(dbgs() << "Computing iteration space\n");
  if (!computeIterationSpaceForLoopNest(L, ItSpaceMap, nullptr))
    return false;

  LLVM_DEBUG(dbgs() << "Finding partition outputs\n");
  BasicBlock *Header = L->getHeader();
  assert(Header != nullptr);
  for (BasicBlock *BB : L->blocks()) {
    auto SearchBB = ItSpaceMap.find(BB);
    if (SearchBB == ItSpaceMap.end()) {
      LLVM_DEBUG(dbgs() << "No iteration space for BB: " << BB->getName()
                        << "\n");
      return false;
    }
    LoopPartition *BBPart = SearchBB->second;
    // Allocate the "rest" partition to contain all the instructions
    // that not related to the targeted store instruction.
    std::shared_ptr<LoopPartition> RestPartPtr = nullptr;
    LoopPartition *RestPart = nullptr;
    // Before create a new "rest" partition, we search around the "rest"
    // partitions and try to find a partition which have the same iteration
    // space. If such a partition exist, we can keep inserting instructions into
    // it and no need to create a new one.
    for (auto &CurrRest : RestNodes) {
      LoopPartition *ExistingRest = CurrRest.get();
      if (ExistingRest->hasMatchingIterationSpace(*BBPart)) {
        RestPart = ExistingRest;
        break;
      }
    }
    if (RestPart == nullptr) {
      RestPartPtr = createNewPartition(*BBPart, nullptr);
      RestPart = RestPartPtr.get();
      RestPart->setName(std::string("rest.") + std::to_string(RestIdx));
      RestNodes.insert(RestPartPtr);
      RestIdx++;
    }

    // Now go through instructions in the Basic Block.
    assert(RestPart != nullptr);
    for (Instruction &I : *BB) {
      Instruction *Instr = &I;
      assert(InstrToCountMap.find(Instr) == InstrToCountMap.end());
      InstrToCountMap[Instr] = 0;
      if (isa<CallInst>(Instr)) {
        CallInst *CI = dyn_cast<CallInst>(Instr);
        if (isDroppableCall(CI))
          continue;
        // For all the function calls that are not memory allocations, check
        // their operands to see if they might have dependencies to multiple
        // targeted malloc call.
        if (!CI->hasRetAttr(Attribute::NoAlias) ||
            CI->getFunctionType()->getReturnType()->isVoidTy()) {
          Value *TargetedOp = nullptr;
          bool IsTargeted = false;
          for (unsigned Index = 0; Index < Instr->getNumOperands(); Index++) {
            Value *Operand = Instr->getOperand(Index);
            if (!isa<Instruction>(Operand))
              continue;
            // Check the operand before BitCast, if there are multiple different
            // operands are targeted, the process is aborted thus we don't
            // know which partition these instructions belong to.
            Value *PtrBase = getOperandSrc(Operand);
            if (isTargetMallocCall(PtrBase)) {
              if (IsTargeted && PtrBase != TargetedOp) {
                LLVM_DEBUG(dbgs()
                           << "Unable to handle function call [" << *Instr
                           << "] with parameters [" << *TargetedOp << " ] and ["
                           << *PtrBase << "]\n");
                return false;
              }
              IsTargeted = true;
              TargetedOp = PtrBase;
              // Otherwise, if the operand is a load or GEP, we follow the chain
              // to find its final source for checking. The function call needs
              // the arguments which value may come from targets outside,
              // partition the target may cause instruction ordering issue so we
              // abort.
            } else if (PtrBase == nullptr) {
              PtrBase = getOperandPtrBase(Operand);
              if (!isa<Instruction>(PtrBase))
                continue;
              Instruction *SrcI = dyn_cast<Instruction>(PtrBase);
              auto SearchPtr = PtrToPartMap.find(PtrBase);
              if (!L->contains(SrcI->getParent()) &&
                  SearchPtr != PtrToPartMap.end() &&
                  SearchPtr->second != RestPart &&
                  SearchPtr->second->getLoopNestDepth() >=
                      BBPart->getLoopNestDepth()) {
                LLVM_DEBUG(dbgs()
                           << "Unable to handle function call [" << *Instr
                           << "] with parameter dereferencing [" << *PtrBase
                           << "]\n");
                return false;
              }
            }
          }
          // For the call instructions that do not have operand related to a
          // target function call and return void, they have no relationship to
          // any of the store instructions, thus place them into the "rest" part
          // now so they will not be missed in the second step.
          if (!IsTargeted &&
              CI->getFunctionType()->getReturnType()->isVoidTy()) {
            RestPart->addStore(Instr);
            ITPMap[Instr].push_back(RestPart);
          }
        }
        // Create a new partition for the targeted stores, and place all other
        // stores into the "rest" partition.
      } else if (isa<StoreInst>(Instr)) {
        StoreInst *Store = cast<StoreInst>(Instr);
        Value *PtrBase = getPtrBaseOutsideLoop(*Store);
        Value *StoreSrc = Store->getValueOperand();
        // Found a targeted distributable store instruction
        if (isTargetMallocCall(StoreSrc)) {
          auto SearchSrc = SrcToPartMap.find(StoreSrc);
          auto SearchPtr = PtrToPartMap.find(PtrBase);
          LLVM_DEBUG(dbgs()
                     << "Found distributable store [" << *Instr << "]\n");
          // Check whether another targeted store that has the same pointer or
          // value operand as the current store, if so, the current store is
          // placed into the partition that the another store is located instead
          // of create a new one.
          if (SearchSrc != SrcToPartMap.end() &&
              SearchPtr == PtrToPartMap.end()) {
            LoopPartition *ExistingPart = SearchSrc->second;
            if (!ExistingPart->hasMatchingIterationSpace(*BBPart)) {
              LLVM_DEBUG(dbgs()
                         << "BB Partitions do not match for store source ["
                         << *StoreSrc << "]\n");
              return false;
            }
            ExistingPart->addStore(Instr);
            ITPMap[Instr].push_back(ExistingPart);
            if (PtrBase)
              PtrToPartMap[PtrBase] = ExistingPart;
          } else if (SearchSrc == SrcToPartMap.end() &&
                     SearchPtr != PtrToPartMap.end()) {
            LoopPartition *ExistingPart = SearchPtr->second;
            if (!ExistingPart->hasMatchingIterationSpace(*BBPart)) {
              LLVM_DEBUG(dbgs()
                         << "BB Partitions do not match for pointer base ["
                         << *PtrBase << "]\n");
              return false;
            }
            ExistingPart->addStore(Instr);
            ITPMap[Instr].push_back(ExistingPart);
            SrcToPartMap[StoreSrc] = ExistingPart;
          } else if (SearchSrc == SrcToPartMap.end() &&
                     SearchPtr == PtrToPartMap.end()) {
            LoopPartition *NewPart = createNewPartition(*BBPart, Instr).get();
            NewPart->setName(std::string("malloc.") +
                             std::to_string(MallocIdx));
            ITPMap[Instr].push_back(NewPart);
            if (PtrBase)
              PtrToPartMap[PtrBase] = NewPart;
            SrcToPartMap[StoreSrc] = NewPart;
            MallocIdx++;
          } else {
            LoopPartition *ExistingPart = SearchSrc->second;
            if (ExistingPart != SearchPtr->second) {
              LLVM_DEBUG(dbgs() << "Unable to handle: " << *Instr << "\n");
              return false;
            }
            if (!ExistingPart->hasMatchingIterationSpace(*BBPart)) {
              LLVM_DEBUG(dbgs()
                         << "BB Partitions do not match for pointer base ["
                         << *PtrBase << "]\n");
              return false;
            }
            ExistingPart->addStore(Instr);
            ITPMap[Instr].push_back(ExistingPart);
          }
          // Not targeted store are placed into the "rest" partition.
        } else {
          RestPart->addStore(Instr);
          ITPMap[Instr].push_back(RestPart);
        }
      } else if (hasUseOutsideOfLoop(*Instr)) {
        LLVM_DEBUG(dbgs() << "Unable to handle: " << *Instr << "\n");
        return false;
      }
    }
  }

  clearEmptyPartitions();

  LLVM_DEBUG(dbgs() << "Filling partitions with instructions\n");
  // BFS for instructions based on the operands of the instructions in
  // queue, start from the store and call instructions.
  for (auto &N : getNodes()) {
    LoopPartition *LP = N.get();
    std::queue<Instruction *> Queue;
    std::unordered_set<Instruction *> Visited;

    LLVM_DEBUG(dbgs() << "---===[ New Partition ]===---\n");
    LLVM_DEBUG(for (Instruction *ID
                    : LP->getItSpaceInstructions()) dbgs()
               << "*  ITS [" << *ID << "]\n");
    Loop *IML = nullptr;
    for (Instruction *Store : LP->getStoreSet()) {
      LLVM_DEBUG(dbgs() << " * store [" << *Store << "]\n");
      Queue.push(Store);
      if (IML == nullptr)
        IML = LI->getLoopFor(Store->getParent());
    }
    assert(IML != nullptr);

    PHINode *IndVar = LP->getInnerLoopIndVar();
    Instruction *StepInstr = LP->getStepInstrForIndVar(IndVar);
    if (StepInstr == nullptr)
      return false;
    // This method should insert all instructions that have dependencies
    // with the store and call instructions into their partition without
    // missing nor duplicated.
    SetVector<Instruction *> InstSet;
    while (!Queue.empty()) {
      Instruction *Instr = Queue.front();
      Queue.pop();

      LLVM_DEBUG(dbgs() << "Processing [" << *Instr << "]\n");
      if (Visited.count(Instr) > 0)
        continue;
      Visited.insert(Instr);

      if (handlingPHINodes(LP, Instr, IndVar, IML))
        continue;

      if ((Instr == IndVar) || (Instr == StepInstr))
        continue;

      // Process instruction inputs.
      for (unsigned Index = 0; Index < Instr->getNumOperands(); Index++) {
        Value *Operand = Instr->getOperand(Index);

        if (isa<Instruction>(Operand)) {
          Instruction *Input = dyn_cast<Instruction>(Operand);
          // Skip induction variable. We know about it already.
          if ((Input == IndVar) || isPartitionOutput(Input))
            continue;

          BasicBlock *PBB = Input->getParent();
          // Skip instructions from its child loops.
          if (isBlockPartOfSubLoop(IML, PBB))
            continue;
          // For instructions from outer loop, they might be partitioned from
          // the loop they originally belong to. GVN pass may simplify the inner
          // loops by directly dereferencing values from outer loop intead of
          // load them first, which may cause dependency issue. The following
          // method is to address this issue.
          if (!IML->contains(PBB)) {
            if (N->getLoopNestDepth() == 1)
              continue;
            // If the operand is a target call, it is partitioned away from the
            // original loop structure, which causes dependency issue. To solve
            // this issue, a load instruction is created to load from where the
            // target call value stored, so the inner loop can handle it.
            else if (isTargetMallocCall(Input)) {
              auto SearchSrc = SrcToPartMap.find(Input);
              assert(SearchSrc != SrcToPartMap.end());
              for (Instruction *St : SearchSrc->second->getStoreSet()) {
                StoreInst *Store = cast<StoreInst>(St);
                if (Store->getValueOperand() == Input) {
                  IRBuilder builder(Instr);
                  Value *Candidate = Store->getPointerOperand();
                  Value *Load =
                      builder.CreateLoad(Candidate->getType(), Candidate);
                  Instr->setOperand(Index, Load);
                  LLVM_DEBUG(dbgs() << "Creating missing load: [" << *Load
                                    << "] and Modified GEP instruction: ["
                                    << *Instr << "]\n");
                  Input = dyn_cast<Instruction>(Load);
                  break;
                }
              }
              // Instead of skipping all instructions from outer loop, we insert
              // all GEPs into the partition to discard the impact of GVN. The
              // redundant GEPs are removed later when doing transformation.
            } else if (!isa<GetElementPtrInst>(Input))
              continue;
          }

          // If the operand is in another basic block, we insert the terminator
          // branch from it to the queue as well, so the control flow between
          // basic blocks of the operand and the instruction can be handled.
          if (PBB != Instr->getParent()) {
            Instruction *SrcBR = PBB->getTerminator();
            if (!isa<BranchInst>(SrcBR))
              continue;
            Queue.push(SrcBR);
            if (Visited.count(SrcBR) == 0)
              InstSet.insert(SrcBR);
          }

          if (Visited.count(Input) == 0) {
            Queue.push(Input);
            InstSet.insert(Input);
            // Mark this instruction as belonging to the given partition.
            if (ITPMap.find(Input) == ITPMap.end())
              ITPMap[Input].push_back(LP);
            else {
              auto Search =
                  std::find(ITPMap[Input].begin(), ITPMap[Input].end(), LP);
              if (Search == ITPMap[Input].end())
                ITPMap[Input].push_back(LP);
            }
          }
        }
      }
    }

    addInputsToPartitionTopologically(InstSet, LP);

    for (auto *I : LP->getStoreSet()) {
      assert(InstrToCountMap.find(I) != InstrToCountMap.end());
      InstrToCountMap[I]++;
    }

    for (auto *I : LP->getInputInstrSet()) {
      if (InstrToCountMap.find(I) == InstrToCountMap.end()) {
        if (isa<GetElementPtrInst>(I) || isa<LoadInst>(I))
          continue;
        LLVM_DEBUG(dbgs() << "Unable to handle: " << *I << "\n");
        return false;
      }
      InstrToCountMap[I]++;
    }

    LLVM_DEBUG(dbgs() << "Partition size = " << LP->getInputInstrSet().size()
                      << "\n");
  }

  // Now perform validation.
  unsigned ErrCount = validatePartitions(InstrToCountMap);

  createUseDefEdges();
  createReadWriteEdges();
  generateInstOrderForPartitions();

  return ErrCount == 0;
}

/// Check if an instruction is a GEP or a load before BitCast.
Value *LoopPartitionGraph::getOperandSrc(Value *Instr) const {
  assert(Instr != nullptr);
  if (isa<BitCastInst>(Instr)) {
    BitCastInst *BC = dyn_cast<BitCastInst>(Instr);
    Instr = BC->getOperand(0);
  }
  if (isa<BitCastOperator>(Instr)) {
    BitCastOperator *BC = dyn_cast<BitCastOperator>(Instr);
    Instr = BC->getOperand(0);
  }
  if (isa<GetElementPtrInst>(Instr) || isa<LoadInst>(Instr))
    return nullptr;
  return Instr;
}

/// Find the final source of a GEP or a load through the chain.
Value *LoopPartitionGraph::getOperandPtrBase(Value *Instr) const {
  assert(Instr != nullptr);
  while (isa<GetElementPtrInst>(Instr) || isa<LoadInst>(Instr)) {
    if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Instr))
      Instr = GEP->getPointerOperand();
    else if (LoadInst *Load = dyn_cast<LoadInst>(Instr))
      Instr = Load->getPointerOperand();
  }
  return Instr;
}

/// Check if an instruction is a targeted malloc call.
bool LoopPartitionGraph::isTargetMallocCall(Value *Instr) const {
  if (!Instr || !isa<CallInst>(Instr))
    return false;
  bool isTarget = false;
  // Isolate the usage of the pass exclusive attribute.
  return isTarget;
}

} // namespace LoopTools
