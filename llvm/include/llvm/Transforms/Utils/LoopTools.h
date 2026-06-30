//===- LoopTools.h --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2021-2022. Huawei Technologies Co., Ltd. All rights reserved.
//
//===----------------------------------------------------------------------===//
//
// This header file contains declaration of key data structures used in
// LoopTools framework. The key data structures are:
// - LoopPartitionBounds
// - LoopPartition
// - LoopPartitionGraph
// The top-level interface is the LoopPartitionGraph which is capable of
// representing a loop nest using LoopPartitions and LoopPartitionBounds to
// enable loop analysis and transformation for cases such as tiling.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_UTILS_LOOPTOOLS_H
#define LLVM_TRANSFORMS_UTILS_LOOPTOOLS_H

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
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <functional>
#include <list>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

using namespace llvm;

namespace LoopTools {

struct LoopPartitionBounds {
  PHINode *IndVar = nullptr;
  Value *Start = nullptr;
  Value *End = nullptr;
  bool InclusiveStart = false;
  bool InclusiveEnd = false;
  ConstantInt *Step = nullptr;
  bool IsUnsigned = false;
  Instruction *StepInstr = nullptr;
};

// Define an enum class to specify types of dependencies between loop
// partitions.
enum class DependencyType { None, UseDef, ReadWrite, ITSNestChange };
class LoopPartition;

// Define a structure to hold partition edge information.
struct LoopPartitionEdge {
  DependencyType Type = DependencyType::None;
  std::shared_ptr<LoopPartition> Source = nullptr;
  std::shared_ptr<LoopPartition> Destination = nullptr;
  Instruction *SourceInstr = nullptr;
  Instruction *DestinationInstr = nullptr;
  int ITSChange = 0;
  int Direction = 0; // 0 - undef, 1 - forward, (-1) - backward
};

// This class defines a block of code of a loop and associates it with
// an iteration space that is defined by the loop nesting. The block of code
// is defined to begin with input data from load operations or loop-invariant
// variables, go through intermediate operations and end at a store, or
// multiple stores as may be the case.
class LoopPartition {
public:
  LoopPartition(std::vector<struct LoopPartitionBounds> &LoopIndVarOrder,
                Instruction *Store);

  // Create a new partition with matching iteration space, but for a different
  // store instruction.
  LoopPartition(LoopPartition &Other, Instruction *Store);

  // Create loop partition duplicate.
  LoopPartition(const LoopPartition &Other);

  // Clean constructor.
  LoopPartition();

  // Destructor.
  ~LoopPartition();

  // Add iteration space dimension, ie. a new induction variable.
  void addIterationDimension(const struct LoopPartitionBounds &LPB,
                             int Pos = -1);

  // Update existing iteration dimension parameters.
  void updateIterationDimension(const struct LoopPartitionBounds &LPB);

  // Add instruction to partition.
  void addInstruction(Instruction *I);

  // Add store or loop-carried PHI to partition.
  void addStore(Instruction *I);

  // Given a description of iteration space, copy it over to this partition.
  void cloneIterationSpace(
      const std::vector<struct LoopPartitionBounds> &LoopIndVarOrder);

  // Given a variable order and a map of loop bounds, copy over iteration space
  // to this partition.
  void cloneIterationSpace(
      const std::vector<PHINode *> &VarOrder,
      const std::unordered_map<PHINode *, struct LoopPartitionBounds> &ISM);

  void printPartition() const;
  void printIterationSpace() const;
  void markItSpaceInstructions(std::vector<Instruction *> &ItSpaceInsts);
  void markItSpaceInstruction(Instruction *I);
  void copyItSpaceInstructions(const LoopPartition &Other);

  // Merge provided partition into this one.
  void mergePartitions(const LoopPartition &Partition);

  void clearInputInstrSet() { InputInstrSet.clear(); }
  void clearStoreSet() { StoreSet.clear(); }

  // Getter methods.
  // Get the depth of the loop nest for this partition.
  int getLoopNestDepth() const { return (int)LoopOrder.size(); }

  // Get induction variable for innermost loop in this partition.
  PHINode *getInnerLoopIndVar() const;
  Instruction *getStepInstrForIndVar(PHINode *IndVar) const;
  void getInnerLoopBoundsFromCmpInst(ICmpInst *CmpCond, Value *&Start,
                                     Value *&End, bool &StartInc, bool &EndInc,
                                     bool PathTrue) const;
  Instruction *getUniqueStore() const;
  PHINode *getIndVarAtIndex(int Index) const;
  bool getBoundsForIndVar(PHINode *IV, struct LoopPartitionBounds &LPB) const;
  const std::unordered_set<Instruction *> &getItSpaceInstructions() const;

  // Retrieve loop order data.
  const std::vector<PHINode *> &getLoopOrder() const { return LoopOrder; }

  // Retrieve loop bounds information as a map.
  const std::unordered_map<PHINode *, struct LoopPartitionBounds> &
  getLoopBounds() const {
    return LBMap;
  }

  const SetVector<Instruction *> &getInputInstrSet() const {
    return InputInstrSet;
  }

  const SetVector<Instruction *> &getStoreSet() const { return StoreSet; }
  unsigned getNumStores() const { return StoreSet.size(); }
  std::string getName() const { return Name; };

  // Setter methods
  void setName(std::string NewName) { Name = NewName; }

  // Specify the order of loops for this partition. First entry is outer-most
  // induction variable, and the last is the inner-most.
  bool setLoopOrder(std::vector<PHINode *> &NewLoopOrder);

  // Check if the provided partition has a matching iteration space.
  bool hasMatchingIterationSpace(LoopPartition &Partition) const;
  bool hasMatchingLoopBounds(const struct LoopPartitionBounds &Bounds) const;

  // Check if loop bounds have constant int bounds.
  bool hasConstantIntLoopBounds();

  // Check if the provided iteration space matches this partition.
  bool hasMatchingIterationSpace(
      const std::vector<PHINode *> &IndVarOrder,
      const std::vector<struct LoopPartitionBounds> &Bounds) const;

  bool differsOnlyInInnerMostBound(
      const std::vector<PHINode *> &IndVarOrder,
      const std::vector<struct LoopPartitionBounds> &Bounds) const;

  int findDeepestCommonLoopIdx(
      const std::vector<PHINode *> &IndVarOrder,
      const std::vector<struct LoopPartitionBounds> &Bounds,
      int ITSNestChange = 0) const;

  const SetVector<Instruction *> &getPartInstOrder() const {
    return PartInstOrder;
  }

  bool hasInputInstr(Instruction *I) const {
    return (InputInstrSet.count(I) > 0);
  }

  bool removeInputOrStore(Instruction *I) {
    return InputInstrSet.remove(I) || StoreSet.remove(I);
  }

  unsigned size() const { return (InputInstrSet.size() + StoreSet.size()); }

  bool isEmpty() const {
    return ((InputInstrSet.size() + StoreSet.size()) == 0);
  }

  bool hasStore(Instruction *I) const { return (StoreSet.count(I) > 0); }
  bool hasIndVar(PHINode *IndVar) const {
    return (LBMap.find(IndVar) != LBMap.end());
  }

  bool isIncreasing(PHINode *IndVar) const;
  bool isItSpaceInstruction(Instruction *I) const;
  void orderInstructions(std::vector<BasicBlock *> &BlockOrder);
  bool hasComplexControlFlow() const { return (TotalBasicBlocks > 1); }
  bool hasBlock(BasicBlock *BB) const {
    return (PartitionBlocks.count(BB) > 0);
  }
  int getNumPartitionBlocks() const { return (int)PartitionBlocks.size(); }
  bool hasBranchInsts() const { return HasBranches; }
  void setProblemPHI() { HasProblematicPHIs = true; }
  bool hasProblemPHI() const { return HasProblematicPHIs; }

  // Is the given instruction an induction variable for this partition?
  bool isPartitionIndVar(Instruction *Instr) const;

  // Is the given instruction an induction variable increment for this
  // partition?
  bool isPartitionIndVarIncrement(Instruction *Instr) const;
  void appendInstructionOrder(std::vector<Instruction *> &Insts);
  void prependInstructionOrderAfterPHIs(std::vector<Instruction *> &Insts);

protected:
  void printIndented(int Indent, std::string Text) const;

private:
  // Loop order holds the ordering of loops in the partition
  // from outermost to inner-most. This is important for legality
  // analysis.
  std::vector<PHINode *> LoopOrder;

  // LBMap holds bounds for a given loop in the loop nest,
  // with the key being the loop induction variable.
  std::unordered_map<PHINode *, struct LoopPartitionBounds> LBMap;

  // Instruction set for the target stores. It does not contain stores
  // themselves, which are in a separate list.
  SetVector<Instruction *> InputInstrSet;

  // Target stores, which could be literal store operations, or simply a final
  // result of a computation of a loop. This set contains 'stores' this
  // partition handles, potentially more than one if partitions need to be
  // merged due to unresolvable dependencies or performance considerations.
  SetVector<Instruction *> StoreSet;

  // This field contains the order in which the partition instructions are
  // to be printed. This includes both input and store instruction.
  SetVector<Instruction *> PartInstOrder;

  // This set is filled when loop iteration space has been established, to
  // allow us to identify instructions that are part of the loop partition,
  // but they only affect iteration space.
  std::unordered_set<Instruction *> ItSpaceInstructions;

  // Partition name.
  std::string Name;

  // Number of basic blocks for this partition
  int TotalBasicBlocks = 0;

  // Set of basic blocks used in this partition, based on original instruction
  // placement.
  std::unordered_set<BasicBlock *> PartitionBlocks;

  // Has branches?
  bool HasBranches = false;

  // Has Problem PHIs?
  bool HasProblematicPHIs = false;
};

// This class describes a graph of loop partitions, exposing the dependencies
// (def-use, RW) to enable higher-level analysis for tiling and distribution
// as well as other loop transformations.
class LoopPartitionGraph {
public:
  LoopPartitionGraph(Loop *LoopNest, ScalarEvolution *SCEV, DominatorTree *DT,
                     LoopInfo *LoopInfoStruct, const LoopAccessInfo *LAI);

  ~LoopPartitionGraph();

  std::shared_ptr<LoopPartition>
  createNewPartition(std::vector<struct LoopPartitionBounds> &LoopIndVarOrder,
                     Instruction *Store);
  std::shared_ptr<LoopPartition> createNewPartition(LoopPartition &Other,
                                                    Instruction *Store);
  std::shared_ptr<LoopPartition> createNewPartition(const LoopPartition &Other);
  std::shared_ptr<LoopPartition> createNewPartition();
  const std::vector<std::shared_ptr<LoopPartition>> &getNodes() const;
  void clearEmptyPartitions();

  // This method create edges between partitions based on dependencies between
  // instructions. It finds partition that has Src in its Store set and then
  // goes through all other partitions that have Dst in their InputInstrSet and
  // creates edges of EdgeType between such pairs of partitions. Please note
  // that each pair of partitions can have multiple edges between them.
  bool createEdge(Instruction *Src, Instruction *Dst, DependencyType EdgeType,
                  int Dir = 0);

  // Add an edge from a SrcPart partition to any partition with specified
  // Dst instruction in its InputInstrSet.
  bool createEdge(std::shared_ptr<LoopPartition> SrcPart, Instruction *Src,
                  Instruction *Dst, DependencyType EdgeType, int Dir = 0);
  void createEdge(std::shared_ptr<LoopPartition> Src,
                  std::shared_ptr<LoopPartition> Dst, DependencyType EdgeType,
                  int Dir = 0);
  void createEdge(std::shared_ptr<LoopPartition> Src,
                  std::shared_ptr<LoopPartition> Dst, unsigned ITSNestChange,
                  int Dir = 0);
  bool createEdge(std::shared_ptr<LoopPartition> SrcPart, Instruction *Src,
                  std::shared_ptr<LoopPartition> DstPart, Instruction *Dst,
                  DependencyType EdgeType, int Dir = 0);

  // This method finds all def/use edges and adds them to the LoopPartitionGraph
  void createUseDefEdges();
  void createReadWriteEdges();
  int getNestChangeDepth(std::shared_ptr<LoopPartition> Src,
                         std::shared_ptr<LoopPartition> Dst);
  bool hasReadWriteEdges();
  int getNumNodes() const { return Nodes.size(); }
  std::shared_ptr<LoopPartition> getNodeAt(int Index) const;
  void removeNodeAt(int Idx);
  int getInEdgeCount(std::shared_ptr<LoopPartition> Partition) const;
  int getOutEdgeCount(std::shared_ptr<LoopPartition> Partition) const;
  bool hasSrcToDstEdge(std::shared_ptr<LoopPartition> Src,
                       std::shared_ptr<LoopPartition> Dst) const;

  void moveEdges(std::shared_ptr<LoopPartition> From,
                 std::shared_ptr<LoopPartition> To);
  std::unordered_map<BasicBlock *, Loop *> generateHeaderToLoopMap() const;
  std::vector<BasicBlock *> generateBBOrder();
  void generateInstOrderForPartitions();
  void mergeCompatiblePartitions();

  void getKeyLoopBlocks(BasicBlock **PH, BasicBlock **LoopExit,
                        BasicBlock **GuardBlock, Loop *SL) const;

  bool canGenerateCodeToReplaceLoop() const;
  void printGraphConnectivity() const;
  bool createLoopPartitions(bool NoTransform = false);

  // Sort partitions in topological order.
  std::vector<std::shared_ptr<LoopPartition>> getPartitionOrder() const;
  int createLoopNest(BasicBlock *StartingBB, BasicBlock *EndingBB,
                     std::vector<PHINode *> &IndVarOrder,
                     std::vector<BasicBlock *> &LoopGuardBB,
                     std::vector<BasicBlock *> &LoopHeaderBB,
                     std::vector<BasicBlock *> &LoopBodyBB,
                     std::vector<BasicBlock *> &LoopExitingBB,
                     std::vector<BasicBlock *> &LoopExitBB,
                     std::vector<struct LoopPartitionBounds> &Bounds,
                     std::unordered_map<PHINode *, PHINode *> &IVMap,
                     std::shared_ptr<LoopPartition> Partition);

  int createLoopNestFrame(BasicBlock *StartingBB, BasicBlock *EndingBB,
                          std::vector<PHINode *> &IndVarOrder,
                          std::vector<BasicBlock *> &LoopGuardBB,
                          std::vector<BasicBlock *> &LoopHeaderBB,
                          std::vector<BasicBlock *> &LoopBodyBB,
                          std::vector<BasicBlock *> &LoopExitingBB,
                          std::vector<BasicBlock *> &LoopExitBB,
                          std::vector<struct LoopPartitionBounds> &Bounds,
                          std::unordered_map<PHINode *, PHINode *> &IVMap,
                          std::shared_ptr<LoopPartition> Partition,
                          int ITSNestChange = 0);

  // Begin transforms of the loop partition graph.
  bool applyLoopOrder(std::vector<PHINode *> NewOrder);
  bool tileLoops(std::vector<PHINode *> LoopPriority, int BlockSize);
  // End transforms.

  std::vector<PHINode *> getIndVarList() const;
  std::unordered_set<PHINode *> getIndVarSet() const;
  // Given a specified loop nest, replace it with the loop nest described by
  // this graph. The entry to this loop nest will be the preheader of the
  // loop L, and the loop exit of L will be the exit of the loop nest
  // represented by this graph. Note that L must have a single preheader
  bool replaceLoopNestWithGraph(bool OnlyStripMine = false);
  bool hasProblemPHIs() const;

  // Create a new partition by moving specified instructions from partition
  // LP and moving them into a new partition. The newly created partition
  // will be connected with the existing graphs correctly. If an error occurs
  // during this process, the method returns false. Otherwise, a new partition
  // is created, a graph updated and the return value is true.
  bool separatePartition(std::shared_ptr<LoopPartition> LP,
                         std::unordered_set<Instruction *> &Insts);

  // process loops with frequent scattered stores to a large memory
  bool processLargeMemoryAccess(
      BasicBlock *StartingBB, std::vector<PHINode *> &IndVarOrder,
      std::vector<BasicBlock *> &LoopHeaderBB,
      std::vector<BasicBlock *> &LoopExitingBB, ConstantInt *Step,
      std::unordered_map<PHINode *, PHINode *> &IVMap);

  // Create Basic blocks, potentially temporary.
  BasicBlock *createBB(std::shared_ptr<LoopPartition> Partition, int Index,
                       Function *Parent, BasicBlock *BeforeBB,
                       const std::string Suffix, bool IsTemporary = false);
  BasicBlock *createBB(Function *Parent, BasicBlock *BeforeBB,
                       bool IsTemporary = false, Twine Name = "");

  // Get vectors of In/Out edges for analysis.
  const std::vector<struct LoopPartitionEdge> &
  getInEdges(std::shared_ptr<LoopPartition> LP) const;
  const std::vector<struct LoopPartitionEdge> &
  getOutEdges(std::shared_ptr<LoopPartition> LP) const;
  bool checkReadWriteEdges();
  bool hasAnyExitPHIs() const;
  bool createEdgesBetweenMallocPartitions();
  bool createMallocPartitions();

protected:
  bool isWhiteListCall(CallInst *CI) const;
  bool isDroppableCall(CallInst *CI) const;
  bool isPartitionOutput(Instruction *I) const;
  BasicBlock *findClosestDominator(Loop *SL, BasicBlock *Header,
                                   BasicBlock *BB) const;
  bool getIndVarUseChain(Loop *Nest, PHINode *IV, Instruction *&Increment,
                         Instruction *&Comparison, BranchInst *&LoopBranch,
                         Constant *&IncValue, bool &InclusiveEnd,
                         bool &IsUnsigned) const;
  PHINode *getNonTrivialInductionVariable(Loop *Nest,
                                          struct LoopPartitionBounds &LPB,
                                          std::vector<Instruction *> &ItInsts,
                                          bool &Increasing) const;
  void unlinkAndDestroyLoopNest(Loop *L);
  void addInputsToPartitionTopologically(SetVector<Instruction *> &InstSet,
                                         LoopPartition *LP);
  bool computeIterationSpaceForLoopNest(
      Loop *Nest, std::unordered_map<BasicBlock *, LoopPartition *> &ItSpaceMap,
      LoopPartition *ParentLoopPart);
  Value *getPtrBaseOutsideLoop(StoreInst &Store,
                               bool NoTransform = false) const;
  bool isUnconditionalSuccessor(BasicBlock *Src, BasicBlock *Dst) const;
  bool isBlockPartOfSubLoop(Loop *SL, BasicBlock *BB) const;
  bool hasUseOutsideOfLoop(const Instruction &Instr) const;
  bool hasOneUseOutsideOfLoop(const Instruction &Inst) const;
  bool isLoopGeneratedValue(Loop *SubL, Instruction *Instr) const;
  bool isCarriedStoreInput(PHINode *LoopCarriedDep, Loop *SL,
                           StoreInst **TargetStore) const;
  bool isStoreToAliasableMem(const StoreInst &Store) const;
  PHINode *getInductionVariableAndBounds(
      Loop *SL, Value **StartValue, Value **EndValue, Instruction **StepInst,
      ConstantInt **ConstIncr, bool &Increasing, bool &InclusiveEnd,
      bool &IsUnsigned, std::vector<Instruction *> &ItInsts) const;
  bool computeIterationSpaceForBBs(
      Loop *Nest,
      std::unordered_map<BasicBlock *, LoopPartition *> &ItSpaceMap);
  bool isPHIFromPartitionStore(PHINode *PHI, LoopPartition *Partition);
  void computeDeepestLoop();
  bool generatePartitionCode(
      std::shared_ptr<LoopPartition> P, BasicBlock *Header, BasicBlock *Body,
      BasicBlock *InsertBefore, std::unordered_map<PHINode *, PHINode *> &IVMap,
      std::unordered_map<Instruction *, Instruction *> &InstMap);
  void updateInputs(Instruction *I,
                    std::unordered_map<PHINode *, PHINode *> &IVMap,
                    std::unordered_map<Instruction *, Instruction *> &InstMap);
  bool updatePHINodeControlPaths(BasicBlock *BB);
  bool isValueOnPath(BasicBlock *Parent, BasicBlock *BB,
                     BasicBlock *StartBB) const;
  void deleteOldLoopBodyAndTemporaryBBs();
  void deleteTemporaryBBs();
  PHINode *createStripminedLoop(
      PHINode *IV, int Size,
      std::unordered_map<PHINode *, struct LoopPartitionBounds> &LBMap);
  Value *
  createOrGetReplacementLogic(Value *Original, BasicBlock *PreHeader,
                              std::unordered_map<PHINode *, PHINode *> &IVMap);
  bool isLoopPartOfNest(Loop *CurrentLoop) const;

  // Check if V0 and V1 as a pair match Start/End. So V0=Start,V1=End or
  // V0=End and V1=Start. This check bypasses simple operations such as sign
  // extension to compare values.
  bool haveMatchingValues(Value *V0, Value *V1, Value *Start, Value *End) const;
  void adjustPartitionStoresForUseDef(std::shared_ptr<LoopPartition> Src,
                                      std::shared_ptr<LoopPartition> Dst);
  bool handlingPHINodes(LoopPartition *LP, Instruction *Instr, PHINode *IndVar,
                        Loop *IML);
  unsigned
  validatePartitions(std::unordered_map<Instruction *, int> &InstrToCountMap);
  Value *getOperandSrc(Value *Instr) const;
  Value *getOperandPtrBase(Value *Instr) const;
  bool isTargetMallocCall(Value *Instr) const;

private:
  // List all loop partitions as nodes of the partition graph.
  std::vector<std::shared_ptr<LoopPartition>> Nodes;

  // Hold edges between partitions, where key is the source partition ptr.
  std::unordered_map<LoopPartition *, std::vector<struct LoopPartitionEdge>>
      InEdges;
  std::unordered_map<LoopPartition *, std::vector<struct LoopPartitionEdge>>
      OutEdges;
  ScalarEvolution *SE = nullptr;
  int DeepestLoopLevel = 0;
  Loop *L = nullptr;
  LoopInfo *LI = nullptr;
  DominatorTree *DomTree = nullptr;
  const LoopAccessInfo *LAI = nullptr;
  std::unordered_set<BasicBlock *> GraphBBs;
  std::unordered_set<BasicBlock *> TemporaryBBs;
  std::unordered_set<std::shared_ptr<LoopPartition>> RestNodes;
  std::unordered_map<BasicBlock *, BasicBlock *> OldToNewBBMap;
};

} // namespace LoopTools
#endif // LLVM_TRANSFORMS_UTILS_LOOPTOOLS_H

