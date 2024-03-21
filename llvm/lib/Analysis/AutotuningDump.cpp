#if defined(ENABLE_AUTOTUNER)
// ===-- AutotuningDump.cpp - Auto-Tuning---------------------------------===//
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
#include "llvm/Analysis/AutotuningDump.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/AutoTuner/AutoTuning.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"
#include <sys/stat.h>

using namespace llvm;

#define DEBUG_TYPE "autotuning-dump"

enum AutotuningDumpOpt { whole_modules, functions, loops };

// Enable Debug Options to be specified on the command line
cl::opt<AutotuningDumpOpt> AutotuningDumpMode(
    "autotuning-dump-mode", cl::desc("Choose autotuning dump mode:"),
    cl::init(whole_modules),
    cl::values(clEnumVal(whole_modules, "dump each module in its own file"),
               clEnumVal(functions, "dump each function in its own file"),
               clEnumVal(loops, "dump each loop in its own file")));

AutotuningDump::AutotuningDump(bool IncrementalCompilation) {
  // Check if the environment variable AUTOTUNE_DATADIR is set.
  IsIncrementalCompilation = IncrementalCompilation;
  AutoTuneDirPath = "autotune_datadir";
  if (std::optional<std::string> MaybePath =
          llvm::sys::Process::GetEnv("AUTOTUNE_DATADIR"))
    AutoTuneDirPath = *MaybePath;
}

int AutotuningDump::getConfigNumber() {
  auto ConfigNumOrErr = autotuning::Engine.getConfigNumber();
  if (ConfigNumOrErr)
    return *ConfigNumOrErr;
  else {
    report_fatal_error("Invalid/missing Autotuner configuration ID");
    return -1;
  }
}

void AutotuningDump::dumpToStream(llvm::raw_ostream &os, const Loop &L) const {
  L.print(os);
}

void AutotuningDump::dumpToStream(llvm::raw_ostream &os,
                                  const Function &F) const {
  F.print(os, /*AAW*/ nullptr, /*ShouldPreserveUseListOrder*/ false,
          /*IsForDebug*/ false, /*PrintCompleteIR*/ true);
}

// Create appropriate file. File will contains AbsolutePath/FileName.
std::unique_ptr<raw_ostream> AutotuningDump::createFile(const Twine &File) {
  std::error_code EC;
  return std::make_unique<raw_fd_ostream>((File).str(), EC,
                                          sys::fs::CD_CreateAlways,
                                          sys::fs::FA_Write, sys::fs::OF_None);
}

std::string AutotuningDump::getDirectoryName(const std::string File) const {
  std::string DirectoryName = AutoTuneDirPath;
  if (!autotuning::Engine.isMLEnabled())
    DirectoryName += "/IR_files";

  DirectoryName = DirectoryName + "/" + File + "/";

  // Create directory if not already present.
  if (std::error_code EC = sys::fs::create_directories(DirectoryName))
    errs() << "could not create directory: " << DirectoryName << ": "
           << EC.message();

  return DirectoryName;
}

std::string AutotuningDump::getFileName(std::string FilePath) {
  if (autotuning::Engine.isMLEnabled())
    return std::to_string(this->getConfigNumber()) + ".ll";
  std::replace(FilePath.begin(), FilePath.end(), '/', '_');
  return FilePath + ".ll";
}

void AutotuningDump::dumpModule(Module &M) {
  std::unique_ptr<raw_ostream> fptr;
  LLVM_DEBUG(dbgs() << "AutotuningDump: Dump module IR files.\n");
  if (IsIncrementalCompilation) {
    std::string Filename = M.getSourceFileName();
    llvm::SmallString<128> FilenameVec = StringRef(Filename);
    llvm::sys::fs::make_absolute(FilenameVec);
    size_t Pos = FilenameVec.rfind(".");
    if (Pos != std::string::npos) {
      FilenameVec.pop_back_n(FilenameVec.size() - Pos);
      FilenameVec.append(".ll");
    }
    fptr = createFile(FilenameVec);
  } else {
    std::string File = llvm::sys::path::filename(M.getName()).str();
    std::string DirectoryName = getDirectoryName(File);
    std::string FileName = getFileName(M.getName().str());
    fptr = createFile(DirectoryName + FileName);
  }

  M.print(*fptr, nullptr, true, false);
}

void AutotuningDump::dumpFunctions(Module &M) {
  std::string FilePath = M.getName().str();
  std::replace(FilePath.begin(), FilePath.end(), '/', '_');
  std::string DirectoryName = getDirectoryName(FilePath);
  for (Function &F : M.getFunctionList()) { // go through all functions
    if (F.isDeclaration() || F.empty())
      continue;

    AutoTuningEnabledFunction *AutotuneFunc = &F.getATEFunction();
    assert(AutotuneFunc);
    autotuning::Engine.initContainer(AutotuneFunc, "autotuning-dump",
                                     F.getName(), false);
    std::string FuncName = F.getName().str();
    // check the whole function
    if (AutotuneFunc->requiresIRDump(true)) {
      auto fptr = createFile(DirectoryName + Twine(FuncName) + ".ll");
      this->dumpToStream(*fptr, F);
    }
  }
}

void AutotuningDump::dumpLoops(Module &M,
                               function_ref<LoopInfo &(Function &)> GetLI) {
  for (Function &F : M) {
    // Nothing to do for declarations.
    if (F.isDeclaration() || F.empty())
      continue;

    LoopInfo &LI = GetLI(F);
    for (auto &L : LI.getLoopsInPreorder()) {
      Function *Func = nullptr;
      StringRef FuncName = "";
      if (!L->isInvalid())
        Func = L->getHeader()->getParent();
      if (Func)
        FuncName = Func->getName();

      autotuning::Engine.initContainer(L, "autotuning-dump", FuncName, false);
      if (L->requiresIRDump()) {
        std::string FuncName = L->getCodeRegion().getFuncName();
        unsigned SourceLine = L->getCodeRegion().getSourceLoc().SourceLine;
        std::string DirectoryName = AutoTuneDirPath + "/" +
                                    llvm::sys::path::filename(FuncName).str() +
                                    "_loop_" + std::to_string(SourceLine);
        std::string FileName = std::to_string(this->getConfigNumber()) + ".ll";
        auto fptr = createFile(DirectoryName + "/" + FileName);
        this->dumpToStream(*fptr, *L);
      }
    }
  }
}

bool AutotuningDump::run(Module &M,
                         function_ref<LoopInfo &(Function &)> GetLI) {
  // Change to absolute path.
  SmallString<256> OutputPath = StringRef(AutoTuneDirPath);
  sys::fs::make_absolute(OutputPath);

  // Creating new output directory, if it does not exists.
  if (std::error_code EC = sys::fs::create_directories(OutputPath)) {
    llvm::errs() << (make_error<StringError>(
        "could not create directory: " + Twine(OutputPath) + ": " +
            EC.message(),
        EC));
    return false;
  }

  if (IsIncrementalCompilation) {
    LLVM_DEBUG(
        dbgs()
        << "AutotuningDump: IR files writing for incremental compilation.\n");
    dumpModule(M);
    return false;
  }

  switch (AutotuningDumpMode) {
  case whole_modules:
    dumpModule(M);
    break;
  case functions:
    dumpFunctions(M);
    break;
  case loops:
    dumpLoops(M, GetLI);
  }

  return false;
}

AutotuningDumpLegacy::AutotuningDumpLegacy(bool IncrementalCompilation)
    : ModulePass(AutotuningDumpLegacy::ID) {
  IsIncrementalCompilation = IncrementalCompilation;
  initializeAutotuningDumpLegacyPass(*PassRegistry::getPassRegistry());
}

bool AutotuningDumpLegacy::runOnModule(Module &M) {
  if (!autotuning::Engine.isDumpEnabled())
    return false;

  auto GetLI = [this](Function &F) -> LoopInfo & {
    return getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();
  };

  AutotuningDump Impl(IsIncrementalCompilation);
  return Impl.run(M, GetLI);
}

StringRef AutotuningDumpLegacy::getPassName() const {
  return "Autotuning Dump";
}

void AutotuningDumpLegacy::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<LoopInfoWrapperPass>();
}

char AutotuningDumpLegacy::ID = 0;
INITIALIZE_PASS_BEGIN(AutotuningDumpLegacy, "autotuning-dump",
                      "Dump IR for Autotuned Code Regions", false, false)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(AutotuningDumpLegacy, "autotuning-dump",
                    "Dump IR for Autotuned Code Regions", false, false)

ModulePass *llvm::createAutotuningDumpPass() {
  return new AutotuningDumpLegacy();
}

AnalysisKey AutotuningDumpAnalysis::Key;

AutotuningDumpAnalysis::Result
AutotuningDumpAnalysis::run(Module &M, ModuleAnalysisManager &AM) {
  if (!autotuning::Engine.isDumpEnabled())
    return false;

  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  auto GetLI = [&FAM](Function &F) -> LoopInfo & {
    return FAM.getResult<LoopAnalysis>(F);
  };

  AutotuningDump Impl(IsIncrementalCompilation);
  Impl.run(M, GetLI);
  return false;
}
#endif
