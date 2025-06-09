//===-- PGOMemOPSizeOpt.cpp - Optimizations based on value profiling ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the transformation that optimizes memory intrinsics
// such as memcpy using the size value profile. When memory intrinsic size
// value profile metadata is available, a single memory intrinsic is expanded
// to a sequence of guarded specialized versions that are called with the
// hottest size(s), for later expansion into more optimal inline sequences.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/ProfileData/InstrProf.h"
#define INSTR_PROF_VALUE_PROF_MEMOP_API
#include "llvm/ProfileData/InstrProfData.inc"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Transforms/Instrumentation/PGOInstrumentation.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <cassert>
#include <cstdint>
#include <vector>

#if defined(ENABLE_ACPO)
#include "llvm/Analysis/ModelDataCollector.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Instrumentation/ACPOAI4CMEMOPModel.h"
#endif

using namespace llvm;

#define DEBUG_TYPE "pgo-memop-opt"

STATISTIC(NumOfPGOMemOPOpt, "Number of memop intrinsics optimized.");
STATISTIC(NumOfPGOMemOPAnnotate, "Number of memop intrinsics annotated.");

// The minimum call count to optimize memory intrinsic calls.
static cl::opt<unsigned>
    MemOPCountThreshold("pgo-memop-count-threshold", cl::Hidden, cl::init(1000),
                        cl::desc("The minimum count to optimize memory "
                                 "intrinsic calls"));

// Command line option to disable memory intrinsic optimization. The default is
// false. This is for debug purpose.
static cl::opt<bool> DisableMemOPOPT("disable-memop-opt", cl::init(false),
                                     cl::Hidden, cl::desc("Disable optimize"));

// The percent threshold to optimize memory intrinsic calls.
static cl::opt<unsigned>
    MemOPPercentThreshold("pgo-memop-percent-threshold", cl::init(40),
                          cl::Hidden,
                          cl::desc("The percentage threshold for the "
                                   "memory intrinsic calls optimization"));

// Maximum number of versions for optimizing memory intrinsic call.
static cl::opt<unsigned>
    MemOPMaxVersion("pgo-memop-max-version", cl::init(3), cl::Hidden,
                    cl::desc("The max version for the optimized memory "
                             " intrinsic calls"));

// Scale the counts from the annotation using the BB count value.
static cl::opt<bool>
    MemOPScaleCount("pgo-memop-scale-count", cl::init(true), cl::Hidden,
                    cl::desc("Scale the memop size counts using the basic "
                             " block count value"));

cl::opt<bool>
    MemOPOptMemcmpBcmp("pgo-memop-optimize-memcmp-bcmp", cl::init(true),
                       cl::Hidden,
                       cl::desc("Size-specialize memcmp and bcmp calls"));

static cl::opt<unsigned>
    MemOpMaxOptSize("memop-value-prof-max-opt-size", cl::Hidden, cl::init(128),
                    cl::desc("Optimize the memop size <= this value"));

#if defined(ENABLE_ACPO)
cl::opt<bool>
    EnableAI4CMEMOP("enable-ai4c-memop", cl::init(false), cl::Hidden,
                    cl::desc("Leverage AOT ML model to optimize memop."));

static cl::opt<std::string>
    MemOPDumpFile("memop-dump-file", cl::init("-"), cl::Hidden,
                  cl::desc("Name of a file to store memop data in."));

namespace {
class ModelDataAI4CMemOPCollector : public ModelDataCollector {
public:
  ModelDataAI4CMemOPCollector(formatted_raw_ostream &OS,
                              std::string OutputFileName,
                              FunctionAnalysisManager *FAM)
      : ModelDataCollector(OS, OutputFileName), FAM(FAM) {}
  void collectFeatures(Function *F, Instruction *I, const char *op_name) {
    // Now MemOPSizeOpt could only optimize memcpy and bcmp
    int8_t type = op_name == "memcpy" ? 1 : op_name == "bcmp" ? 2 : 0;
    resetRegisteredFeatures();
    Module *GlobalM = F->getParent();
    ACPOCollectFeatures::FeatureInfo GlobalFeatureInfo{
        ACPOCollectFeatures::FeatureIndex::NumOfFeatures,
        {FAM, nullptr},
        {F, nullptr, I->getParent(), GlobalM, nullptr}};

    registerFeature({ACPOCollectFeatures::Scope::Function}, GlobalFeatureInfo);
    registerFeature({ACPOCollectFeatures::Scope::BasicBlock},
                    GlobalFeatureInfo);
    ModelDataCollector::collectFeatures();

    // Insert Memop type
    Features.push_back(std::make_pair<std::string, std::string>(
        "memop_type", std::to_string(type)));

    int8_t dst_align = 0, dst_from = 0, src_align = 0, src_from = 0;
    // Insert Memop Align info of Dst and Src
    if (type == 1) {
      // For ptr param of memcpy
      dst_align =
          dyn_cast<MemIntrinsic>(I)->getDestAlign().valueOrOne().value();
      dst_from = getPtrType(dyn_cast<MemIntrinsic>(I)->getArgOperand(0));
      src_align =
          dyn_cast<MemCpyInst>(I)->getSourceAlign().valueOrOne().value();
      src_from = getPtrType(dyn_cast<MemIntrinsic>(I)->getArgOperand(1));
    } else if (type == 2) {
      // For ptr param of bcmp
      auto *DstPtr = dyn_cast<CallInst>(I)->getArgOperand(0);
      auto *SrcPtr = dyn_cast<CallInst>(I)->getArgOperand(1);
      dst_from = getPtrType(DstPtr);
      src_from = getPtrType(SrcPtr);
    }
    Features.push_back(std::make_pair<std::string, std::string>(
        "dst_align", std::to_string(dst_align)));

    Features.push_back(std::make_pair<std::string, std::string>(
        "dst_from", std::to_string(dst_from)));

    Features.push_back(std::make_pair<std::string, std::string>(
        "src_align", std::to_string(src_align)));

    Features.push_back(std::make_pair<std::string, std::string>(
        "src_from", std::to_string(src_from)));
  }

  // Set how the Dst/Src ptr is from as a feature
  // 1: Alloca
  // 2: From call malloc intrinsic
  // 3: Other function's return
  // 4: From load
  // 5: Global variable
  // 0: Other ways
  uint16_t getPtrType(Value *Ptr) {
    if (dyn_cast<AllocaInst>(Ptr)) {
      return 1;
    } else if (dyn_cast<CallInst>(Ptr)) {
      if (dyn_cast<CallInst>(Ptr)->getCalledFunction() &&
          dyn_cast<CallInst>(Ptr)->getCalledFunction()->getName().startswith(
              "malloc")) {
        return 2;
      } else {
        return 3;
      }
    } else if (dyn_cast<LoadInst>(Ptr)) {
      return 4;
    } else if (dyn_cast<GlobalVariable>(Ptr)) {
      return 5;
    } else {
      return 0;
    }
  }

public:
  FunctionAnalysisManager *FAM;
};
} // namespace

SmallVector<uint64_t, 16> getACPOAdvice(Function *F,
                                        ModelDataAI4CMemOPCollector *MDC) {
  SmallVector<uint64_t, 16> SizeIds;
  int64_t OPT = 0;
  auto &ORE = MDC->FAM->getResult<OptimizationRemarkEmitterAnalysis>(*F);
  std::unique_ptr<ACPOAI4CMEMOPModel> AI4CMEMOP =
      std::make_unique<ACPOAI4CMEMOPModel>(&(F->getContext()), &ORE);
  std::vector<std::pair<std::string, std::string>> Features =
      MDC->getFeatures();

  std::vector<int> PossibleSizes = {0, 1,  2,  3,  4,  5,  6,   7,   8,
                                    9, 16, 17, 32, 33, 65, 129, 257, 513};

  for (int psize : PossibleSizes) {
    if (psize == 0)
      continue;
    Features.push_back(std::make_pair<std::string, std::string>(
        "opt_size", std::to_string(psize)));
    AI4CMEMOP->setMLCustomFeatures(Features);
    std::unique_ptr<ACPOAdvice> Advice = AI4CMEMOP->getAdvice();
    Constant *Val = Advice->getField("OPT");
    assert(Val != nullptr);
    assert(isa<ConstantInt>(Val));
    ConstantInt *OPTPtr = dyn_cast<ConstantInt>(Val);
    OPT = OPTPtr->getSExtValue();
    if (OPT)
      SizeIds.push_back(psize);
    Features.pop_back();
  }

  return SizeIds;
}
#endif

namespace {

static const char *getMIName(const MemIntrinsic *MI) {
  switch (MI->getIntrinsicID()) {
  case Intrinsic::memcpy:
    return "memcpy";
  case Intrinsic::memmove:
    return "memmove";
  case Intrinsic::memset:
    return "memset";
  default:
    return "unknown";
  }
}

// A class that abstracts a memop (memcpy, memmove, memset, memcmp and bcmp).
struct MemOp {
  Instruction *I;
  MemOp(MemIntrinsic *MI) : I(MI) {}
  MemOp(CallInst *CI) : I(CI) {}
  MemIntrinsic *asMI() { return dyn_cast<MemIntrinsic>(I); }
  CallInst *asCI() { return cast<CallInst>(I); }
  MemOp clone() {
    if (auto MI = asMI())
      return MemOp(cast<MemIntrinsic>(MI->clone()));
    return MemOp(cast<CallInst>(asCI()->clone()));
  }
  Value *getLength() {
    if (auto MI = asMI())
      return MI->getLength();
    return asCI()->getArgOperand(2);
  }
  void setLength(Value *Length) {
    if (auto MI = asMI())
      return MI->setLength(Length);
    asCI()->setArgOperand(2, Length);
  }
  StringRef getFuncName() {
    if (auto MI = asMI())
      return MI->getCalledFunction()->getName();
    return asCI()->getCalledFunction()->getName();
  }
  bool isMemmove() {
    if (auto MI = asMI())
      if (MI->getIntrinsicID() == Intrinsic::memmove)
        return true;
    return false;
  }
  bool isMemcmp(TargetLibraryInfo &TLI) {
    LibFunc Func;
    if (asMI() == nullptr && TLI.getLibFunc(*asCI(), Func) &&
        Func == LibFunc_memcmp) {
      return true;
    }
    return false;
  }
  bool isBcmp(TargetLibraryInfo &TLI) {
    LibFunc Func;
    if (asMI() == nullptr && TLI.getLibFunc(*asCI(), Func) &&
        Func == LibFunc_bcmp) {
      return true;
    }
    return false;
  }
  const char *getName(TargetLibraryInfo &TLI) {
    if (auto MI = asMI())
      return getMIName(MI);
    LibFunc Func;
    if (TLI.getLibFunc(*asCI(), Func)) {
      if (Func == LibFunc_memcmp)
        return "memcmp";
      if (Func == LibFunc_bcmp)
        return "bcmp";
    }
    llvm_unreachable("Must be MemIntrinsic or memcmp/bcmp CallInst");
    return nullptr;
  }
};

class MemOPSizeOpt : public InstVisitor<MemOPSizeOpt> {
public:
#if defined(ENABLE_ACPO)
   MemOPSizeOpt(Function &Func, BlockFrequencyInfo &BFI,
                OptimizationRemarkEmitter &ORE, DominatorTree *DT,
                TargetLibraryInfo &TLI, ModelDataAI4CMemOPCollector &MDC)
       : Func(Func), BFI(BFI), ORE(ORE), DT(DT), TLI(TLI), MDC(MDC),
         Changed(false) {
#else
  MemOPSizeOpt(Function &Func, BlockFrequencyInfo &BFI,
               OptimizationRemarkEmitter &ORE, DominatorTree *DT,
               TargetLibraryInfo &TLI)
      : Func(Func), BFI(BFI), ORE(ORE), DT(DT), TLI(TLI), Changed(false) {
#endif // Enable_ACPO {
    ValueDataArray =
        std::make_unique<InstrProfValueData[]>(INSTR_PROF_NUM_BUCKETS);
  }
  bool isChanged() const { return Changed; }
  void perform() {
    WorkList.clear();
    visit(Func);

    for (auto &MO : WorkList) {
      ++NumOfPGOMemOPAnnotate;
      if (perform(MO)) {
        Changed = true;
        ++NumOfPGOMemOPOpt;
        LLVM_DEBUG(dbgs() << "MemOP call: " << MO.getFuncName()
                          << "is Transformed.\n");
      }
    }
  }

  void visitMemIntrinsic(MemIntrinsic &MI) {
    Value *Length = MI.getLength();
    // Not perform on constant length calls.
    if (isa<ConstantInt>(Length))
      return;
    WorkList.push_back(MemOp(&MI));
  }

  void visitCallInst(CallInst &CI) {
    LibFunc Func;
    if (TLI.getLibFunc(CI, Func) &&
        (Func == LibFunc_memcmp || Func == LibFunc_bcmp) &&
        !isa<ConstantInt>(CI.getArgOperand(2))) {
      WorkList.push_back(MemOp(&CI));
    }
  }

private:
  Function &Func;
  BlockFrequencyInfo &BFI;
  OptimizationRemarkEmitter &ORE;
  DominatorTree *DT;
  TargetLibraryInfo &TLI;
  bool Changed;
  std::vector<MemOp> WorkList;
  // The space to read the profile annotation.
  std::unique_ptr<InstrProfValueData[]> ValueDataArray;
  bool perform(MemOp MO);
#if defined(ENABLE_ACPO)
  std::vector<std::vector<std::string>> Records;
  ModelDataAI4CMemOPCollector &MDC;
#endif
};

static bool isProfitable(uint64_t Count, uint64_t TotalCount) {
  assert(Count <= TotalCount);
  if (Count < MemOPCountThreshold)
    return false;
  if (Count < TotalCount * MemOPPercentThreshold / 100)
    return false;
  return true;
}

static inline uint64_t getScaledCount(uint64_t Count, uint64_t Num,
                                      uint64_t Denom) {
  if (!MemOPScaleCount)
    return Count;
  bool Overflowed;
  uint64_t ScaleCount = SaturatingMultiply(Count, Num, &Overflowed);
  return ScaleCount / Denom;
}

bool MemOPSizeOpt::perform(MemOp MO) {
  assert(MO.I);
  if (MO.isMemmove())
    return false;
  if (!MemOPOptMemcmpBcmp && (MO.isMemcmp(TLI) || MO.isBcmp(TLI)))
    return false;

  uint32_t NumVals, MaxNumVals = INSTR_PROF_NUM_BUCKETS;
  uint64_t TotalCount;
  uint64_t ActualCount;
  uint64_t SavedTotalCount;
  uint64_t RemainCount;
  uint64_t SavedRemainCount;
  SmallVector<uint64_t, 16> SizeIds;
  SmallVector<uint64_t, 16> CaseCounts;
  SmallDenseSet<uint64_t, 16> SeenSizeId;
  uint64_t MaxCount = 0;
  unsigned Version = 0;
  SmallVector<InstrProfValueData, 24> RemainingVDs;
  uint64_t SumForOpt;
  const char *op_name = MO.getName(TLI);
#if defined(ENABLE_ACPO)
  if (EnableAI4CMEMOP) {
    MDC.collectFeatures(&Func, MO.I, op_name);
    SizeIds = getACPOAdvice(MO.I->getFunction(), &MDC);
    if (!SizeIds.size())
      return false;
  } else {
#endif    
    if (!getValueProfDataFromInst(*MO.I, IPVK_MemOPSize, MaxNumVals,
                                  ValueDataArray.get(), NumVals, TotalCount)) {
      return false;
    }
    ActualCount = TotalCount;
    SavedTotalCount = TotalCount;
    if (MemOPScaleCount) {
      auto BBEdgeCount = BFI.getBlockProfileCount(MO.I->getParent());
      if (!BBEdgeCount) {
        return false;
      }
      ActualCount = *BBEdgeCount;
    }

    ArrayRef<InstrProfValueData> VDs(ValueDataArray.get(), NumVals);
    LLVM_DEBUG(dbgs() << "Read one memory intrinsic profile with count "
                      << ActualCount << "\n");
    LLVM_DEBUG(for (auto &VD
                    : VDs) {
      dbgs() << "  (" << VD.Value << "," << VD.Count << ")\n";
    });
    if (ActualCount < MemOPCountThreshold) {
      return false;
    }
    // Skip if the total value profiled count is 0, in which case we can't
    // scale up the counts properly (and there is no profitable transformation).
    if (TotalCount == 0) {
      return false;
    }

    TotalCount = ActualCount;
    if (MemOPScaleCount)
      LLVM_DEBUG(dbgs() << "Scale counts: numerator = " << ActualCount
                        << " denominator = " << SavedTotalCount << "\n");

    // Keeping track of the count of the default case:
    RemainCount = TotalCount;
    SavedRemainCount = SavedTotalCount;
    // Default case is in the front -- save the slot here.
    CaseCounts.push_back(0);
    for (auto I = VDs.begin(), E = VDs.end(); I != E; ++I) {
      auto &VD = *I;
      int64_t V = VD.Value;
      uint64_t C = VD.Count;
      if (MemOPScaleCount)
        C = getScaledCount(C, ActualCount, SavedTotalCount);

      if (!InstrProfIsSingleValRange(V) || V > MemOpMaxOptSize) {
        RemainingVDs.push_back(VD);
        continue;
      }

      // ValueCounts are sorted on the count. Break at the first un-profitable
      // value.
      if (!isProfitable(C, RemainCount)) {
        RemainingVDs.insert(RemainingVDs.end(), I, E);
        break;
      }

      if (!SeenSizeId.insert(V).second) {
        errs() << "warning: Invalid Profile Data in Function " << Func.getName()
               << ": Two identical values in MemOp value counts.\n";
        return false;
      }

      SizeIds.push_back(V);
      CaseCounts.push_back(C);
      if (C > MaxCount)
        MaxCount = C;

      assert(RemainCount >= C);
      RemainCount -= C;
      assert(SavedRemainCount >= VD.Count);
      SavedRemainCount -= VD.Count;

      if (++Version >= MemOPMaxVersion && MemOPMaxVersion != 0) {
        RemainingVDs.insert(RemainingVDs.end(), I + 1, E);
        break;
      }
    }

    if (Version == 0) {
      return false;
    }

    CaseCounts[0] = RemainCount;
    if (RemainCount > MaxCount)
      MaxCount = RemainCount;

    SumForOpt = TotalCount - RemainCount;
    LLVM_DEBUG(dbgs() << "Optimize one memory intrinsic call to " << Version
                      << " Versions (covering " << SumForOpt << " out of "
                      << TotalCount << ")\n");
#if defined(ENABLE_ACPO)
  }
#endif
  
  // mem_op(..., size)
  // ==>
  // switch (size) {
  //   case s1:
  //      mem_op(..., s1);
  //      goto merge_bb;
  //   case s2:
  //      mem_op(..., s2);
  //      goto merge_bb;
  //   ...
  //   default:
  //      mem_op(..., size);
  //      goto merge_bb;
  // }
  // merge_bb:

  BasicBlock *BB = MO.I->getParent();
  LLVM_DEBUG(dbgs() << "\n\n== Basic Block Before ==\n");
  LLVM_DEBUG(dbgs() << *BB << "\n");
  auto OrigBBFreq = BFI.getBlockFreq(BB);

  BasicBlock *DefaultBB = SplitBlock(BB, MO.I, DT);
  BasicBlock::iterator It(*MO.I);
  ++It;
  assert(It != DefaultBB->end());
  BasicBlock *MergeBB = SplitBlock(DefaultBB, &(*It), DT);
  MergeBB->setName("MemOP.Merge");
  BFI.setBlockFreq(MergeBB, OrigBBFreq.getFrequency());
  DefaultBB->setName("MemOP.Default");

  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Eager);
  auto &Ctx = Func.getContext();
  IRBuilder<> IRB(BB);
  BB->getTerminator()->eraseFromParent();
  Value *SizeVar = MO.getLength();
  SwitchInst *SI = IRB.CreateSwitch(SizeVar, DefaultBB, SizeIds.size());
  Type *MemOpTy = MO.I->getType();
  PHINode *PHI = nullptr;
  if (!MemOpTy->isVoidTy()) {
    // Insert a phi for the return values at the merge block.
    IRBuilder<> IRBM(MergeBB->getFirstNonPHI());
    PHI = IRBM.CreatePHI(MemOpTy, SizeIds.size() + 1, "MemOP.RVMerge");
    MO.I->replaceAllUsesWith(PHI);
    PHI->addIncoming(MO.I, DefaultBB);
  }

  // Clear the value profile data.
  MO.I->setMetadata(LLVMContext::MD_prof, nullptr);
  // If all promoted, we don't need the MD.prof metadata.
  if (SavedRemainCount > 0 || Version != NumVals) {
    // Otherwise we need update with the un-promoted records back.
    ArrayRef<InstrProfValueData> RemVDs(RemainingVDs);
    annotateValueSite(*Func.getParent(), *MO.I, RemVDs, SavedRemainCount,
                      IPVK_MemOPSize, NumVals);
  }

  LLVM_DEBUG(dbgs() << "\n\n== Basic Block After==\n");

  std::vector<DominatorTree::UpdateType> Updates;
  if (DT)
    Updates.reserve(2 * SizeIds.size());

  for (uint64_t SizeId : SizeIds) {
    BasicBlock *CaseBB = BasicBlock::Create(
        Ctx, Twine("MemOP.Case.") + Twine(SizeId), &Func, DefaultBB);
    MemOp NewMO = MO.clone();
    // Fix the argument.
    auto *SizeType = dyn_cast<IntegerType>(NewMO.getLength()->getType());
    assert(SizeType && "Expected integer type size argument.");
    ConstantInt *CaseSizeId = ConstantInt::get(SizeType, SizeId);
    NewMO.setLength(CaseSizeId);
    NewMO.I->insertInto(CaseBB, CaseBB->end());
    IRBuilder<> IRBCase(CaseBB);
    IRBCase.CreateBr(MergeBB);
    SI->addCase(CaseSizeId, CaseBB);
    if (!MemOpTy->isVoidTy())
      PHI->addIncoming(NewMO.I, CaseBB);
    if (DT) {
      Updates.push_back({DominatorTree::Insert, CaseBB, MergeBB});
      Updates.push_back({DominatorTree::Insert, BB, CaseBB});
    }
    LLVM_DEBUG(dbgs() << *CaseBB << "\n");
  }
  DTU.applyUpdates(Updates);
  Updates.clear();

  if (MaxCount)
    setProfMetadata(Func.getParent(), SI, CaseCounts, MaxCount);

  LLVM_DEBUG(dbgs() << *BB << "\n");
  LLVM_DEBUG(dbgs() << *DefaultBB << "\n");
  LLVM_DEBUG(dbgs() << *MergeBB << "\n");

  ORE.emit([&]() {
    using namespace ore;
    return OptimizationRemark(DEBUG_TYPE, "memopt-opt", MO.I)
           << "optimized " << NV("Memop", MO.getName(TLI)) << " with count "
           << NV("Count", SumForOpt) << " out of " << NV("Total", TotalCount)
           << " for " << NV("Versions", Version) << " versions";
  });

  return true;
}
} // namespace

#if defined(ENABLE_ACPO)
 static bool PGOMemOPSizeOptImpl(Function &F, BlockFrequencyInfo &BFI,
                                 OptimizationRemarkEmitter &ORE,
                                 DominatorTree *DT, TargetLibraryInfo &TLI,
                                 ModelDataAI4CMemOPCollector &MDC) {
#else
static bool PGOMemOPSizeOptImpl(Function &F, BlockFrequencyInfo &BFI,
                                OptimizationRemarkEmitter &ORE,
                                DominatorTree *DT, TargetLibraryInfo &TLI) {
#endif // ENABLE_ACPO
  if (DisableMemOPOPT)
    return false;

  if (F.hasFnAttribute(Attribute::OptimizeForSize))
    return false;
#if defined(ENABLE_ACPO)
  MemOPSizeOpt MemOPSizeOpt(F, BFI, ORE, DT, TLI, MDC);
#else
  MemOPSizeOpt MemOPSizeOpt(F, BFI, ORE, DT, TLI);
#endif
  MemOPSizeOpt.perform();
  return MemOPSizeOpt.isChanged();
}

PreservedAnalyses PGOMemOPSizeOpt::run(Function &F,
                                       FunctionAnalysisManager &FAM) {
  auto &BFI = FAM.getResult<BlockFrequencyAnalysis>(F);
  auto &ORE = FAM.getResult<OptimizationRemarkEmitterAnalysis>(F);
  auto *DT = FAM.getCachedResult<DominatorTreeAnalysis>(F);
  auto &TLI = FAM.getResult<TargetLibraryAnalysis>(F);
#if defined(ENABLE_ACPO)
   std::error_code EC;
   raw_fd_ostream RawOS(MemOPDumpFile.getValue(), EC, sys::fs::CD_OpenAlways,
                        sys::fs::FA_Write, sys::fs::OF_Append);
   formatted_raw_ostream OS(RawOS);
   ModelDataAI4CMemOPCollector MDC(OS, MemOPDumpFile, &FAM);
   bool Changed = PGOMemOPSizeOptImpl(F, BFI, ORE, DT, TLI, MDC);
#else
  bool Changed = PGOMemOPSizeOptImpl(F, BFI, ORE, DT, TLI);
#endif
  if (!Changed)
    return PreservedAnalyses::all();
  auto PA = PreservedAnalyses();
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}
