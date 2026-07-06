//===- ModelDataCollector.cpp - Data collector for ML model  --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2021-2022. Huawei Technologies Co., Ltd. All rights reserved.
//
//===----------------------------------------------------------------------===//
//
// This file implements the collection and dumping of data for the ML models
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ModelDataCollector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace llvm;

#define DEBUG_TYPE "model-data-collector"

static cl::opt<std::string> InputFile("mdc-input", cl::Hidden,
                               cl::desc("Specify the input file"));

static cl::opt<std::string> OutputOppDir(
    "mdc-opp", cl::Hidden,
    cl::desc("Specify the output directory of tuning opportunities"));

static cl::opt<std::string> UnnamedVariablePrefix(
    "unnamed-var-prefix", cl::Hidden,
    cl::desc("Specify the prefix added to unnamed variables"), cl::init(""));

static cl::opt<std::string>
    IRFileDirectory("IR-file-directory", cl::Hidden,
                    cl::desc("Name of a directory to store IR files."));

cl::opt<std::string>
    ProteanModelFile("protean-dump-file", cl::init("-"), cl::Hidden,
                     cl::desc("Name of a file to store feature data in."));
cl::opt<std::string> ProteanLoopModelFile(
    "protean-loop-dump-file", cl::init("-"), cl::Hidden,
    cl::desc("Name of a file to store loop feature data in."));

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

std::vector<std::pair<std::string, std::string>>
ModelDataCollector::getFeatures() {
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
                                         const std::string &FuncName,
                                         const std::string &LoopName) {}

void ModelDataCollector::proteanCollectFeatures() {
  for (auto &FeatureCollectInfo : ProteanFeatureCollectInfos) {
    ProteanCollectFeatures::FeatureValueMap FeatureMap;
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

      FeatureName += ProteanCollectFeatures::getFeatureName(key);
      if (FeatureCollectInfo->Postfix != "")
        FeatureName += "_" + FeatureCollectInfo->Postfix;

      Features.insert(Features.end(), {std::make_pair(FeatureName, val)});
    }
  }
}

void ModelDataCollector::registerFeature(
    ProteanCollectFeatures::FeaturesInfo Info, std::string Pre,
    std::string Post) {
  std::unique_ptr<ModelDataCollector::ProteanFeatureCollectInfo> tmp =
      std::make_unique<ModelDataCollector::ProteanFeatureCollectInfo>();
  tmp->FeaturesInfo.reset(new ProteanCollectFeatures::FeaturesInfo{Info});
  tmp->FeatureCollector.reset(new ProteanCollectFeatures{});
  tmp->Prefix = Pre;
  tmp->Postfix = Post;

  ProteanFeatureCollectInfos.push_back(std::move(tmp));
}

void ModelDataCollector::registerFeature(
    ProteanCollectFeatures::Scopes ScopeVec,
    ProteanCollectFeatures::FeatureInfo GlobalInfo, std::string Pre,
    std::string Post) {
  std::unique_ptr<ModelDataCollector::ProteanFeatureCollectInfo> tmp =
      std::make_unique<ModelDataCollector::ProteanFeatureCollectInfo>();
  tmp->RegisteredScopes.reset(new ProteanCollectFeatures::Scopes{ScopeVec});
  tmp->FeatureCollector.reset(new ProteanCollectFeatures{});
  tmp->GlobalInfo.reset(new ProteanCollectFeatures::FeatureInfo{GlobalInfo});
  tmp->Prefix = Pre;
  tmp->Postfix = Post;

  ProteanFeatureCollectInfos.push_back(std::move(tmp));
}

void ModelDataCollector::registerFeature(
    ProteanCollectFeatures::GroupIDs GroupIDVec,
    ProteanCollectFeatures::FeatureInfo GlobalInfo, std::string Pre,
    std::string Post) {
  std::unique_ptr<ModelDataCollector::ProteanFeatureCollectInfo> tmp =
      std::make_unique<ModelDataCollector::ProteanFeatureCollectInfo>();
  tmp->RegisteredGroupIDs.reset(
      new ProteanCollectFeatures::GroupIDs{GroupIDVec});
  tmp->FeatureCollector.reset(new ProteanCollectFeatures{});
  tmp->GlobalInfo.reset(new ProteanCollectFeatures::FeatureInfo{GlobalInfo});
  tmp->Prefix = Pre;
  tmp->Postfix = Post;

  ProteanFeatureCollectInfos.push_back(std::move(tmp));
}

void ModelDataCollector::resetRegisteredFeatures() {
  ProteanFeatureCollectInfos.clear();
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

void ModelDataCollector::setIRFileNameMap(
    StringMap<std::string> IRFileNameMap) {
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

  for (unsigned I = 0, E = Features.size(); I != E; ++I) {
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

void ModelDataCollector::setOutput(std::string Output) { Out << Output; }

std::string ModelDataCollector::getIRFileName(StringRef Key) {
  if (IRFileNames.count(Key))
    return IRFileNames.find(Key)->second;

  return "None";
}

std::unique_ptr<raw_ostream>
ModelDataCollector::createFile(const Twine &FilePath, const Twine &FileName,
                               std::error_code &EC) {
  if (std::error_code EC = sys::fs::create_directories(FilePath))
    errs() << "Error creating directory: " << FilePath << ": " << EC.message()
           << "\n";

  return std::make_unique<raw_fd_ostream>((FilePath + "/" + FileName).str(),
                                          EC);
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
    L->print(*OS);
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

  F->print(*OS);
}

void ModelDataCollector::writeIR(Loop *L, Function *F,
                                 std::string NewIRFileName,
                                 std::string PassName,
                                 DumpOption DumpBeforeOrAfter, bool PrintLoop,
                                 bool PrintFunction, bool OverwriteIRFile) {
  // Create base directory first
  SmallString<256> IRFilePath;
  // First priority is autotune_datadir if it is specified
  if (OutputOppDir.getNumOccurrences() > 0) {
    Twine BaseDir(OutputOppDir);
    BaseDir.toVector(IRFilePath);
    sys::path::append(IRFilePath, "../ir");
  } else if (InputFile.getNumOccurrences() > 0) {
    // Second priority is the directory containing config.yaml
    // file when using the -fautotune option
    Twine BaseDir(InputFile);
    BaseDir.toVector(IRFilePath);
    // InputFile contains the config file name so we need to remove that
    sys::path::remove_filename(IRFilePath);
    sys::path::append(IRFilePath, "ir");
  } else if (IRFileDirectory.getNumOccurrences() > 0) {
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
  std::string SubDir =
      getDumpOptionAsString(DumpBeforeOrAfter) + "_" + PassName;
  if (L && PrintLoop) {
    createIRFileForLoop(L,
                        Twine(IRFilePath) + "/" + SubDir + "_" +
                            getDumpOptionAsString(DumpOption::loop),
                        Twine(NewIRFileName), OverwriteIRFile);
    // Add IR file name for summary data file
    IRFileNames.insert(std::pair<std::string, std::string>(
        getDumpOptionAsString(DumpBeforeOrAfter) +
            getDumpOptionAsString(DumpOption::loop),
        NewIRFileName));
  }

  if (F && PrintFunction) {
    createIRFileForFunction(F,
                            Twine(IRFilePath) + "/" + SubDir + "_" +
                                getDumpOptionAsString(DumpOption::function),
                            Twine(NewIRFileName), OverwriteIRFile);
    // Add IR file name for summary data file
    IRFileNames.insert(std::pair<std::string, std::string>(
        getDumpOptionAsString(DumpBeforeOrAfter) +
            getDumpOptionAsString(DumpOption::function),
        NewIRFileName));
  }
}
