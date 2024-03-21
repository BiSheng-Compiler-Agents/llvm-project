//===-- StructuralHash.cpp - IR Hashing -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/StructuralHash.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#if defined(ENABLE_AUTOTUNER)
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CommandLine.h"
#endif

using namespace llvm;

#if defined(ENABLE_AUTOTUNER)
// AutoTuner Flag to use callsite Debug Location for hash cacluation.
static cl::opt<bool> HashCallSite(
    "hash-prior-to-callsite", cl::init(true), cl::Hidden,
    cl::desc("Use function IR prior to a call site to compute the hashcode for"
             " the call site"));
#endif

namespace {

// Basic hashing mechanism to detect structural change to the IR, used to verify
// pass return status consistency with actual change. Loosely copied from
// llvm/lib/Transforms/Utils/FunctionComparator.cpp

class StructuralHashImpl {
  hash_code Hash;
#if defined(ENABLE_AUTOTUNER)
  const uint64_t BLOCK_HEADER_HASH = 45798;
#endif

  template <typename T> void hash(const T &V) { Hash = hash_combine(Hash, V); }

public:
  StructuralHashImpl() : Hash(4) {}

#if defined(ENABLE_AUTOTUNER)
  void update(const MachineBasicBlock &MBB) {
    // Update the structural hash when we encounter a new basic block.
    // Prevents CodeRegions with different structures, but many empty
    // BasicBlocks to have the same structural hash.
    if (const BasicBlock *Block = MBB.getBasicBlock()) {
      hash(BLOCK_HEADER_HASH); // Block header
      for (auto &Inst : *Block)
        hash(Inst.getOpcode());
    }
  }

  void update(const std::vector<BasicBlock *> BBs) {
    // Update the structural hash when we encounter a new basic block.
    // Prevents CodeRegions with different structures, but many empty
    // BasicBlocks to have the same structural hash.
    for (BasicBlock *BB : BBs) {
      if (BB == nullptr)
        continue;

      hash(BLOCK_HEADER_HASH); // Block header
      for (auto &Inst : *BB)
        hash(Inst.getOpcode());
    }
  }

  void update(const llvm::CallBase &CB) {
    StringRef Name = "";
    if (HashCallSite) {
      update(*CB.getCaller(), std::addressof(CB));
    } else {
      const Function &F = *CB.getCaller();
      Name = F.getName();
      std::string FileName = Name.str();
      for (uint64_t Idx = 0; Idx < Name.size(); Idx = Idx + sizeof(uint64_t)) {
        uint64_t Value = 0;
        FileName.copy((char *)&Value, sizeof(uint64_t), Idx);
        hash(Value);
      }
    }

    update(*CB.getCalledFunction());
  }

  void update(const SwitchInst &SI) {
    hash(SI.getNumCases());
    for (auto Case : SI.cases()) {
      hash(BLOCK_HEADER_HASH);
      const BasicBlock *BB = Case.getCaseSuccessor();
      for (auto &Inst : *BB)
        hash(Inst.getOpcode());
    }
  }

  void update(const Function &F, const CallBase *TargetCB = nullptr) {
    if (F.isDeclaration())
      return;

    const Instruction *I =
        TargetCB ? (dyn_cast<Instruction>(TargetCB)) : nullptr;
#else
  void update(const Function &F) {
    // Declarations don't affect analyses.
    if (F.isDeclaration())
      return;
#endif

    hash(12345); // Function header

    hash(F.isVarArg());
    hash(F.arg_size());

    SmallVector<const BasicBlock *, 8> BBs;
    SmallPtrSet<const BasicBlock *, 16> VisitedBBs;

    BBs.push_back(&F.getEntryBlock());
    VisitedBBs.insert(BBs[0]);
    while (!BBs.empty()) {
      const BasicBlock *BB = BBs.pop_back_val();
#if defined(ENABLE_AUTOTUNER)
      hash(BLOCK_HEADER_HASH); // Block header
      for (auto &Inst : *BB) {
        hash(Inst.getOpcode());
        if (I && Inst.isIdenticalTo(I))
          return;
      }
#else
      hash(45798); // Block header
      for (auto &Inst : *BB)
        hash(Inst.getOpcode());
#endif

      const Instruction *Term = BB->getTerminator();
      for (unsigned i = 0, e = Term->getNumSuccessors(); i != e; ++i) {
        if (!VisitedBBs.insert(Term->getSuccessor(i)).second)
          continue;
        BBs.push_back(Term->getSuccessor(i));
      }
    }
  }

  void update(const GlobalVariable &GV) {
    // Declarations and used/compiler.used don't affect analyses.
    // Since there are several `llvm.*` metadata, like `llvm.embedded.object`,
    // we ignore anything with the `.llvm` prefix
    if (GV.isDeclaration() || GV.getName().starts_with("llvm."))
      return;
    hash(23456); // Global header
    hash(GV.getValueType()->getTypeID());
  }

  void update(const Module &M) {
    for (const GlobalVariable &GV : M.globals())
      update(GV);
    for (const Function &F : M)
      update(F);
  }

  uint64_t getHash() const { return Hash; }
};

} // namespace

#if defined(ENABLE_AUTOTUNER)
uint64_t llvm::StructuralHash(const MachineBasicBlock &MBB) {
  StructuralHashImpl H;
  H.update(MBB);
  return H.getHash();
}

uint64_t llvm::StructuralHash(const std::vector<BasicBlock *> BBs) {
  StructuralHashImpl H;
  H.update(BBs);
  return H.getHash();
}

uint64_t llvm::StructuralHash(const CallBase &CB) {
  StructuralHashImpl H;
  H.update(CB);
  return H.getHash();
}

uint64_t llvm::StructuralHash(const SwitchInst &SI) {
  StructuralHashImpl H;
  H.update(SI);
  return H.getHash();
}
#endif

uint64_t llvm::StructuralHash(const Function &F) {
  StructuralHashImpl H;
  H.update(F);
  return H.getHash();
}

uint64_t llvm::StructuralHash(const Module &M) {
  StructuralHashImpl H;
  H.update(M);
  return H.getHash();
}
