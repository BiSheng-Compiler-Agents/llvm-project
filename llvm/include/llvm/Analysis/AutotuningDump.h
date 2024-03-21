#if defined(ENABLE_AUTOTUNER)
// ===-- AutotuningDump.h - Auto-Tuning-----------------------------------===//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// ===--------------------------------------------------------------------===//
//
// This file contains pass collecting IR of tuned regions and storing them into
// predetrmined locations, to be used later by autotuning ML guidance
//
// ===--------------------------------------------------------------------===//

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include <string>

namespace llvm {
class AutotuningDump {
public:
  AutotuningDump(bool IncrementalCompilation = false);
  bool run(Module &F, function_ref<LoopInfo &(Function &)> GetLI);

private:
  std::string AutoTuneDirPath;
  std::unique_ptr<raw_ostream> createFile(const Twine &File);
  int getConfigNumber();
  void dumpToStream(llvm::raw_ostream &os, const Loop &L) const;
  void dumpToStream(llvm::raw_ostream &os, const Function &F) const;
  void dumpFunctions(llvm::Module &M);
  void dumpLoops(llvm::Module &M, function_ref<LoopInfo &(Function &)> GetLI);
  void dumpModule(llvm::Module &M);
  std::string getDirectoryName(const std::string File) const;
  std::string getFileName(std::string FilePath);

  bool IsIncrementalCompilation;
};

class AutotuningDumpLegacy : public ModulePass {
public:
  static char ID;
  AutotuningDumpLegacy(bool IncrementalCompilation = false);
  StringRef getPassName() const override;
  bool runOnModule(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  bool IsIncrementalCompilation;
};

class AutotuningDumpAnalysis
    : public AnalysisInfoMixin<AutotuningDumpAnalysis> {
  friend AnalysisInfoMixin<AutotuningDumpAnalysis>;
  static AnalysisKey Key;

public:
  AutotuningDumpAnalysis(bool IncrementalCompilation = false) {
    IsIncrementalCompilation = IncrementalCompilation;
  }

  // This pass only prints IRs of selected function or loops without doing any
  // real analyses, thus the return value is meaningless. To avoid leaking data
  // or memory, we typedef Result to Optional<bool> to avoid having to return an
  // AutotuningDump object.
  using Result = std::optional<bool>;
  Result run(Module &M, ModuleAnalysisManager &AM);

private:
  bool IsIncrementalCompilation;
};
} // namespace llvm
#endif