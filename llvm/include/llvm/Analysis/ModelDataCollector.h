//===- ModelDataCollector.h - Data collector for ML model -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2021-2022. Huawei Technologies Co., Ltd. All rights reserved.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MODELDATACOLLECTOR_H
#define LLVM_ANALYSIS_MODELDATACOLLECTOR_H

#include "llvm/ADT/StringMap.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ProteanCollectFeatures.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <vector>

namespace llvm {
class ModelDataCollector {
public:
  enum DumpOption { function, loop, before, after };

  ModelDataCollector(formatted_raw_ostream &OS, std::string OutputFileName = "")
      : OutputFileName(OutputFileName), Out(OS) {}

  ~ModelDataCollector() {}

  std::string getDumpOptionAsString(DumpOption DO);
  std::string getIRFileName(StringRef Key);
  std::string getOutputFileName();
  bool isEmptyOutputFile();
  std::string demangleName(const std::string &Name);
  std::vector<std::pair<std::string, std::string>> getFeatures();
  std::unique_ptr<raw_ostream>
  createFile(const Twine &FilePath, const Twine &FileName, std::error_code &EC);
  StringMap<std::string> getIRFileNameMap();
  void
  setFeatures(std::vector<std::pair<std::string, std::string>> NewFeatures);
  void setIRFileNameMap(StringMap<std::string> IRFileNameMap);
  void
  addFeatures(std::vector<std::pair<std::string, std::string>> NewFeatures);

  // Print out the features
  void printRow(bool printHeader = false);
  void setOutput(std::string Output);

  // Create the directory structure and store IR files in their corresponding
  // directory
  void writeIR(Loop *L, Function *F, std::string NewIRFileName,
               std::string PassName, DumpOption DumpBeforeOrAfter,
               bool PrintLoop, bool PrintFunction,
               bool OverwriteIRFile = false);

  // Print the loop IR to a file
  void createIRFileForLoop(Loop *L, const Twine &IRFilePath,
                           const Twine &NewIRFileName, bool OverwriteIRFile);

  // Print the function IR to a file
  void createIRFileForFunction(Function *F, const Twine &IRFilePath,
                               const Twine &NewIRFileName,
                               bool OverwriteIRFile);

  virtual void collectFeatures(Loop *L, const std::string &ModuleName,
                               const std::string &FuncName,
                               const std::string &LoopName);

  virtual void proteanCollectFeatures();

  // FeatureCollectInfo contains the information of registered feature.
  struct ProteanFeatureCollectInfo {
    std::unique_ptr<ProteanCollectFeatures::FeaturesInfo> FeaturesInfo;
    std::unique_ptr<ProteanCollectFeatures::Scopes> RegisteredScopes;
    std::unique_ptr<ProteanCollectFeatures::GroupIDs> RegisteredGroupIDs;
    std::unique_ptr<ProteanCollectFeatures::FeatureInfo> GlobalInfo;
    std::unique_ptr<ProteanCollectFeatures> FeatureCollector;
    std::string Prefix;
    std::string Postfix;
  };

  void registerFeature(ProteanCollectFeatures::FeaturesInfo, std::string = "",
                       std::string = "");
  void registerFeature(ProteanCollectFeatures::Scopes,
                       ProteanCollectFeatures::FeatureInfo, std::string = "",
                       std::string = "");
  void registerFeature(ProteanCollectFeatures::GroupIDs,
                       ProteanCollectFeatures::FeatureInfo, std::string = "",
                       std::string = "");
  void resetRegisteredFeatures();

protected:
  // Collected features
  std::vector<std::pair<std::string, std::string>> Features;
  // NOTE: OutputFileName being empty (null) is treated as stdout
  std::string OutputFileName;
  std::vector<std::unique_ptr<ProteanFeatureCollectInfo>>
      ProteanFeatureCollectInfos;

private:
  // Stream for dumping training data
  formatted_raw_ostream &Out;
  StringMap<std::string> IRFileNames;
};
} // namespace llvm

#endif // LLVM_ANALYSIS_MODELDATACOLLECTOR_H
