//===- ModelDataCollector.cpp - Data collector for ML model  --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the collection and dumping of data for the ML models
//
//===----------------------------------------------------------------------===//

#if defined(ENABLE_ACPO)
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ModelDataCollector.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace llvm;

#define DEBUG_TYPE "model-data-collector"

// Defined in 'lib/IR/AsmWriter.cpp'
extern cl::opt<std::string> UnnamedVariablePrefix;

static cl::opt<std::string> IRFileDirectory(
    "IR-file-directory", cl::Hidden,
    cl::desc("Name of a directory to store IR files."));

cl::opt<std::string>
    ACPOModelFile("acpo-dump-file", cl::init("-"), cl::Hidden,
                  cl::desc("Name of a file to store feature data in."));

std::string ModelDataCollector::getDumpOptionAsString(DumpOption DO) {
  switch (DO) {
    case DumpOption::loop:
      return "loop";
    case DumpOption::function:
      return "function";
    case DumpOption::before:
      return "before";
    case DumpOption::after:
      return "after";
    default:
      return "";
  }
}

std::vector<std::pair<std::string, std::string>> ModelDataCollector::getFeatures() {
  return Features;
}

StringMap<std::string> ModelDataCollector::getIRFileNameMap() {
  return IRFileNames;
}

std::string ModelDataCollector::getOutputFileName() { return OutputFileName; }

bool ModelDataCollector::isEmptyOutputFile() {
  if (OutputFileName.empty())
    return false;

  if (!sys::fs::exists(OutputFileName))
    return true;

  uint64_t Size;
  std::error_code EC = sys::fs::file_size(OutputFileName, Size);
  if (EC) {
    llvm::errs() << "Cannot get file size: " << EC.message() << "\n";
    assert(false && "Cannot get file size.");
  }

  if (Size == 0)
    return true;

  return false;
}

void ModelDataCollector::collectFeatures(Loop *L, const std::string &ModuleName,
                        const std::string &FuncName, const std::string &LoopName) {
}

void ModelDataCollector::collectFeatures() {
  for (auto &FeatureCollectInfo : FeatureCollectInfos) {
    ACPOCollectFeatures::FeatureValueMap FeatureMap;

    if (FeatureCollectInfo->FeaturesInfo.get()) {
      FeatureMap = FeatureCollectInfo->FeatureCollector->getFeaturesPair(
          *FeatureCollectInfo->FeaturesInfo.get());
    } else if (FeatureCollectInfo->RegisteredScopes.get()) {
      FeatureCollectInfo->FeatureCollector->setGlobalFeatureInfo(
          *FeatureCollectInfo->GlobalInfo.get());
      FeatureMap = FeatureCollectInfo->FeatureCollector->getFeaturesPair(
          *FeatureCollectInfo->RegisteredScopes.get());
    } else if (FeatureCollectInfo->RegisteredGroupIDs.get()) {
      FeatureCollectInfo->FeatureCollector->setGlobalFeatureInfo(
          *FeatureCollectInfo->GlobalInfo.get());
      FeatureMap = FeatureCollectInfo->FeatureCollector->getFeaturesPair(
          *FeatureCollectInfo->RegisteredGroupIDs.get());
    } else {
      outs() << "No Features are collected, since the given "
                "FeatureCollectInfo is invalid.\n";
      return;
    }
    for (auto const &[key, val] : FeatureMap) {
      std::string FeatureName;

      if (FeatureCollectInfo->Prefix != "")
        FeatureName += FeatureCollectInfo->Prefix + "_";

      FeatureName += ACPOCollectFeatures::getFeatureName(key);

      if (FeatureCollectInfo->Postfix != "")
        FeatureName += "_" + FeatureCollectInfo->Postfix;

      Features.insert(Features.end(), {std::make_pair(FeatureName, val)});
    }
  }
}

void ModelDataCollector::registerFeature(ACPOCollectFeatures::FeaturesInfo Info,
                                         std::string Pre, std::string Post) {
  std::unique_ptr<ModelDataCollector::FeatureCollectInfo> tmp =
      std::make_unique<ModelDataCollector::FeatureCollectInfo>();
  tmp->FeaturesInfo.reset(new ACPOCollectFeatures::FeaturesInfo{Info});
  tmp->FeatureCollector.reset(new ACPOCollectFeatures{});
  tmp->Prefix = Pre;
  tmp->Postfix = Post;

  FeatureCollectInfos.push_back(std::move(tmp));
}

void ModelDataCollector::registerFeature(
    ACPOCollectFeatures::Scopes ScopeVec,
    ACPOCollectFeatures::FeatureInfo GlobalInfo, std::string Pre,
    std::string Post) {
  std::unique_ptr<ModelDataCollector::FeatureCollectInfo> tmp =
      std::make_unique<ModelDataCollector::FeatureCollectInfo>();
  tmp->RegisteredScopes.reset(new ACPOCollectFeatures::Scopes{ScopeVec});
  tmp->FeatureCollector.reset(new ACPOCollectFeatures{});
  tmp->GlobalInfo.reset(new ACPOCollectFeatures::FeatureInfo{GlobalInfo});
  tmp->Prefix = Pre;
  tmp->Postfix = Post;

  FeatureCollectInfos.push_back(std::move(tmp));
}

void ModelDataCollector::registerFeature(
    ACPOCollectFeatures::GroupIDs GroupIDVec,
    ACPOCollectFeatures::FeatureInfo GlobalInfo, std::string Pre,
    std::string Post) {
  std::unique_ptr<ModelDataCollector::FeatureCollectInfo> tmp =
      std::make_unique<ModelDataCollector::FeatureCollectInfo>();
  tmp->RegisteredGroupIDs.reset(new ACPOCollectFeatures::GroupIDs{GroupIDVec});
  tmp->FeatureCollector.reset(new ACPOCollectFeatures{});
  tmp->GlobalInfo.reset(new ACPOCollectFeatures::FeatureInfo{GlobalInfo});
  tmp->Prefix = Pre;
  tmp->Postfix = Post;

  FeatureCollectInfos.push_back(std::move(tmp));
}

void ModelDataCollector::resetRegisteredFeatures() {
  FeatureCollectInfos.clear();
  Features.clear();
}

std::string ModelDataCollector::demangleName(const std::string &Name) {
  ItaniumPartialDemangler D;
  if (!D.partialDemangle(Name.c_str()))
    return D.getFunctionBaseName(nullptr, nullptr);

  return Name;
}

void ModelDataCollector::setFeatures(
                std::vector<std::pair<std::string, std::string>> NewFeatures) {
  Features = NewFeatures;
}

void ModelDataCollector::addFeatures(
                std::vector<std::pair<std::string, std::string>> NewFeatures) {
  Features.insert(Features.end(), NewFeatures.begin(), NewFeatures.end());
}

void ModelDataCollector::setIRFileNameMap(StringMap<std::string> IRFileNameMap) {
  IRFileNames = IRFileNameMap;
}

void ModelDataCollector::printRow(bool printHeader) {
  // Print the IR file names first
  for (const auto &P : IRFileNames) {
    if (printHeader)
      Out << P.getKey();
    else
      Out << P.getValue();

    Out << ",";
  }

  for (unsigned I = 0, E = Features.size(); I != E; ++I ) {
    // First value does not get a comma
    if (I)
      Out << ",";

    if (printHeader)
      Out << Features.at(I).first;
    else
      Out << Features.at(I).second;
  }

  Out << "\n";
}

/*std::string ModelDataCollector::generateIRFileName(autotuning::CodeRegion CR) {
  // File name = source_location + pass_name + coderegion_type + hash,
  // where source_location = file_name + func_name + loop_name
  //                         + line_number + column_number
  std::string IRFileName =
      sys::path::filename(StringRef(CR.getFileName())).str() + "_"
      + demangleName(CR.getFuncName()) + "_"
      + CR.getName() + "_"
      + std::to_string(CR.getSourceLoc().SourceLine) + "_"
      + std::to_string(CR.getSourceLoc().SourceColumn) + "_"
      + CR.getPassName() + "_"
      + CR.getTypeAsString() + "_"
      + std::to_string(CR.getHash()) + ".ll";
  return IRFileName;
}*/

std::string ModelDataCollector::getIRFileName(StringRef Key) {
  if (IRFileNames.count(Key))
    return IRFileNames.find(Key)->second;

  return "None";
}

std::unique_ptr<raw_ostream>
ModelDataCollector::createFile(const Twine &FilePath,
                               const Twine &FileName,
                               std::error_code &EC) {
  if (std::error_code EC = sys::fs::create_directories(FilePath))
    errs() << "Error creating directory: " << FilePath << ": "
           << EC.message() << "\n";

  return std::make_unique<raw_fd_ostream>((FilePath + "/" + FileName).str(), EC);
}

void ModelDataCollector::createIRFileForLoop(Loop *L, const Twine &IRFilePath,
                                             const Twine &IRFileName,
                                             bool OverwriteIRFile) {
  if (!OverwriteIRFile && sys::fs::exists(IRFilePath + "/" + IRFileName))
    return;

  // Write IR to file
  std::error_code EC;
  auto OS = createFile(IRFilePath, Twine(IRFileName), EC);
  if (EC) {
    errs() << "Error creating loop IR file: " << IRFileName << ": "
            << EC.message() << "\n";
    return;
  }

  // Print loop wrapped in function if -unnamed-var-prefix is set by user
  if (UnnamedVariablePrefix.getNumOccurrences() > 0) {
    SmallVector<BasicBlock *, 8> ExitBlocks;
    L->getExitBlocks(ExitBlocks);
    // May need to move this code out of Loop data structure in LLVM. Will see.
    L->printWithFunctionWrapper(*OS, L->getHeader()->getParent(),
                                L->getBlocks(), L->getHeader(), ExitBlocks,
                                /*AAW*/ nullptr,
                                /*ShouldPreserveUseListOrder*/ false,
                                /*IsForDebug*/ false);
  } else {
    L->print(*OS, /*Depth*/ 0, /*Verbose*/ true);
  }
}

void ModelDataCollector::createIRFileForFunction(Function *F,
                                                 const Twine &IRFilePath,
                                                 const Twine &IRFileName,
                                                 bool OverwriteIRFile) {
  if (!OverwriteIRFile && sys::fs::exists(IRFilePath + "/" + IRFileName))
    return;

  // Write IR to file
  std::error_code EC;
  auto OS = createFile(IRFilePath, Twine(IRFileName), EC);
  if (EC) {
    errs() << "Error creating function IR file: " << IRFileName << ": "
            << EC.message() << "\n";
    return;
  }
  // May need to investigate this print function change.
  F->print(*OS, /*AAW*/ nullptr, /*ShouldPreserveUseListOrder*/ false,
           /*IsForDebug*/ false);
}

void ModelDataCollector::writeIR(Loop *L, Function *F,
                                 std::string NewIRFileName,
                                 std::string PassName,
                                 DumpOption DumpBeforeOrAfter, bool PrintLoop,
                                 bool PrintFunction, bool OverwriteIRFile) {
  // Create base directory first
  SmallString<256> IRFilePath;
  if (IRFileDirectory.getNumOccurrences() > 0) {
    // Third priority is the directory specified by
    // the -IR-file-directory option
    Twine BaseDir(IRFileDirectory);
    BaseDir.toVector(IRFilePath);
  } else {
    // No directory specified
    return;
  }

  if (getDumpOptionAsString(DumpBeforeOrAfter).empty())
    return;

  // Create sub-directories to store corresponding IR files.
  // Directory name = before/after + pass_name + coderegion_type
  std::string SubDir = getDumpOptionAsString(DumpBeforeOrAfter)
                                  + "_" + PassName;
  if (L && PrintLoop) {
    createIRFileForLoop(L,
                        Twine(IRFilePath) + "/" + SubDir + "_" +
                            getDumpOptionAsString(DumpOption::loop),
                        Twine(NewIRFileName), OverwriteIRFile);
    // Add IR file name for summary data file
    IRFileNames.insert(std::pair<std::string, std::string> (
                        getDumpOptionAsString(DumpBeforeOrAfter)
                        + getDumpOptionAsString(DumpOption::loop),
                        NewIRFileName));
  }

  if (F && PrintFunction) {
    createIRFileForFunction(F,
                            Twine(IRFilePath) + "/" + SubDir + "_" +
                                getDumpOptionAsString(DumpOption::function),
                            Twine(NewIRFileName), OverwriteIRFile);
    // Add IR file name for summary data file
    IRFileNames.insert(std::pair<std::string, std::string> (
                        getDumpOptionAsString(DumpBeforeOrAfter)
                        + getDumpOptionAsString(DumpOption::function),
                        NewIRFileName));
  }
}
#endif // ENABLE_ACPO
