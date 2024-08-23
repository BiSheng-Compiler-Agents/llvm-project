//===- ModelDataCollector.h - Data collector for ML model -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MODELDATACOLLECTOR_H
#define LLVM_ANALYSIS_MODELDATACOLLECTOR_H

#if defined(ENABLE_ACPO)
#include "llvm/ADT/StringMap.h"
#include "llvm/Analysis/ACPOCollectFeatures.h"
#include "llvm/Analysis/LoopInfo.h"
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
  //std::string generateIRFileName(autotuning::CodeRegion CR);
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

  virtual void collectFeatures();

  // FeatureCollectInfo contains the information of registered feature.
  struct FeatureCollectInfo {
    std::unique_ptr<ACPOCollectFeatures::FeaturesInfo> FeaturesInfo;
    std::unique_ptr<ACPOCollectFeatures::Scopes> RegisteredScopes;
    std::unique_ptr<ACPOCollectFeatures::GroupIDs> RegisteredGroupIDs;
    std::unique_ptr<ACPOCollectFeatures::FeatureInfo> GlobalInfo;
    std::unique_ptr<ACPOCollectFeatures> FeatureCollector;
    std::string Prefix;
    std::string Postfix;
  };

  void registerFeature(ACPOCollectFeatures::FeaturesInfo, std::string = "",
                       std::string = "");
  void registerFeature(ACPOCollectFeatures::Scopes,
                       ACPOCollectFeatures::FeatureInfo, std::string = "",
                       std::string = "");
  void registerFeature(ACPOCollectFeatures::GroupIDs,
                       ACPOCollectFeatures::FeatureInfo, std::string = "",
                       std::string = "");
  void resetRegisteredFeatures();

protected:
  // Collected features
  std::vector<std::pair<std::string, std::string>> Features;
  // NOTE: OutputFileName being empty (null) is treated as stdout
  std::string OutputFileName;
  std::vector<std::unique_ptr<FeatureCollectInfo>> FeatureCollectInfos;

private:
  // Stream for dumping training data
  formatted_raw_ostream &Out;
  StringMap<std::string> IRFileNames;
};
} // namespace llvm

#endif // ENABLE_ACPO
#endif // LLVM_ANALYSIS_MODELDATACOLLECTOR_H
