//===- ACPOMLInterface.cpp - AI-Enabled Continuous Program Optimization ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements an interface to the ML framework.
//
//===----------------------------------------------------------------------===//

#if defined(ENABLE_ACPO)
#include "llvm/Analysis/ACPOMLInterface.h"
#include "llvm/Analysis/ACPOModelRunner.h"
#include "llvm/Analysis/FIModelRunner.h"
#include "llvm/Analysis/TensorSpec.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

#include <ctime>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

using namespace llvm;

#define DEBUG_TYPE "acpo"

#define ACPO_ENV_VAR_DIR "ACPO_DIR"
#define ACPO_ML_PYTHON_INTERFACE_PY "MLInterface.py"
#define ACPO_PYTHON_EXECUTABLE "python"
#define ACPO_PIPE_PREFIX "ACPO_Pipe"

#define RESPONSE_MODEL_LOADED "Model loaded"
#define RESPONSE_ALREADY_IN_DICT "already in dict"
#define RESPONSE_FEATURE_SET "Feature set"
#define RESPONSE_FEATURES_INITIALIZED "Features initialized"
#define RESPONSE_FEATURES_SET "Features set"
#define RESPONSE_COMPLETED "Completed"
#define RESPONSE_ACTIVE "Active"
#define RESPONSE_ERROR "ERROR"

// Static variables

static std::shared_ptr<ACPOMLInterface> PersistentMLIF = nullptr;

// Class definitions

bool Model::registerFeature(std::string FeatureName, uint64_t FeatureID,
                            int Index) {
  auto Find1 = NameToID.find(FeatureName);
  if (Find1 != NameToID.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in registerFeature: Feature " << FeatureName
                      << " already exists\n");
    return false;
  }
  NameToID.insert(std::make_pair(FeatureName, FeatureID));
  IDToName.insert(std::make_pair(FeatureID, FeatureName));
  auto Find2 = IDToIndex.find(FeatureID);
  if (Find2 != IDToIndex.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in registerFeature: Feature with ID "
                      << FeatureID << " already exists\n");
    return false;
  }
  IDToIndex.insert(std::make_pair(FeatureID, Index));
  return true;
}

bool Model::registerInput(std::string InputName, std::string InputType) {
  auto Find = InputMap.find(InputName);
  if (Find != InputMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in registerInput: Input " << InputName
                      << " already exists\n");
    return false;
  }
  InputMap.insert(std::make_pair(InputName, InputType));
  return true;
}

bool Model::registerOutput(std::string OutputName, std::string OutputType) {
  auto Find = OutputMap.find(OutputName);
  if (Find != OutputMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in registerOutput: Output " << OutputName
                      << " already exists\n");
    return false;
  }
  OutputMap.insert(std::make_pair(OutputName, OutputType));
  return true;
}

int Model::getIndex(uint64_t FeatureID) const {
  auto Find = IDToIndex.find(FeatureID);
  assert(Find != IDToIndex.end());
  return Find->second;
}

int Model::getIndex(std::string FeatureName) const {
  auto Find = NameToID.find(FeatureName);
  assert(Find != NameToID.end());
  uint64_t ID = Find->second;
  return getIndex(ID);
}

std::string Model::getName(uint64_t FeatureID) const {
  auto Find = IDToName.find(FeatureID);
  assert(Find != IDToName.end());
  return Find->second;
}

bool Model::checkOutputExists(std::string OutputName) const {
  return (OutputMap.find(OutputName) != OutputMap.end());
}

std::string Model::getInputType(std::string InputName) const {
  auto Find = InputMap.find(InputName);
  assert(Find != InputMap.end());
  return Find->second;
}

std::string Model::getOutputType(std::string OutputName) const {
  auto Find = OutputMap.find(OutputName);
  assert(Find != OutputMap.end());
  return Find->second;
}

ACPOMLPythonInterface::ACPOMLPythonInterface() : NextID{0} {
  std::optional<std::string> Env = llvm::sys::Process::GetEnv(ACPO_ENV_VAR_DIR);
  if (!Env || *Env == "") {
    std::optional<std::string> LLVMDIROpt =
        llvm::sys::Process::GetEnv("LLVM_DIR");
    if (LLVMDIROpt) {
      Env = *LLVMDIROpt + "/acpo/";
    } else {
      return;
    }
  }

  int32_t PID = (int32_t) llvm::sys::Process::getProcessId();
  std::string ExecPython = "/usr/bin/python3";
  std::string
      PythonScript = *Env + "/" + std::string(ACPO_ML_PYTHON_INTERFACE_PY);
  std::string PIDStr = std::to_string(PID);
  std::string TimeStr = std::to_string(time(nullptr));
  std::string NameOut =
      *Env + "/" + ACPO_PIPE_PREFIX + "_CMD_" + PIDStr + "_" + TimeStr;
  std::string NameIn =
      *Env + "/" + ACPO_PIPE_PREFIX + "_RESP_" + PIDStr + "_" + TimeStr;
  StringRef Args[] = { ExecPython, PythonScript, NameOut, NameIn };

  // Start a process and don't wait for it to finish. We want it running in
  // tandem.
  std::string ErrMsg;
  SubProcess =
      sys::ExecuteNoWait(ExecPython, Args, std::nullopt, {}, 0, &ErrMsg);
  if (!SubProcess.Pid) {
    // Print out error message if the process fails to start.
    LLVM_DEBUG(dbgs() << ErrMsg << "\n");
    return;
  }
  // Yield to Python Process to set up pipes.
  const int PythonProcessStartupLatency = 100;
  usleep(PythonProcessStartupLatency);

  // Now link to named pipes created by the process we just started. Note that
  // because the creation of this file as a pipe was done elsewhere, the
  // interface here is simple.

  // First check that the response pipe has been created by attempting to open a
  // file for reading. If this is not successful, then sleep for 100us to allow
  // the ML interface the time to create named pipes and open the response pipe
  // for writing. Once that is done, the fopen call will pass here.

  // FIXME: Support library provides robust and portable APIs for opening files
  // and creating input/output streams. Use them instead of calling libc
  // functions.
  PipeIn = fopen(NameIn.c_str(), "r");
  if (PipeIn == nullptr) {
    do {
      usleep(100);
      PipeIn = fopen(NameIn.c_str(), "r");
    } while (PipeIn == nullptr);
  }

  // Once the response FIFO is created, then open the command FIFO for writing.
  // This will complete the handshake with the MLInterface in Python.
  PipeOut = fopen(NameOut.c_str(), "w");
  // Now open named pipes to the new process.
  setInitialized(true);
}

ACPOMLPythonInterface::~ACPOMLPythonInterface() {
  if (SubProcess.Pid)
    closeMLInterface();
  if (PipeIn)
    fclose(PipeIn);
  if (PipeOut)
    fclose(PipeOut);
  if (SubProcess.Pid) {
    // Wait for the MLInterface 3 seconds and kill it.
    sys::Wait(SubProcess, 3) ;
    SubProcess = sys::ProcessInfo{};
  }
  setInitialized(false);
}

uint64_t ACPOMLPythonInterface::assignID() {
  NextID++;
  return NextID - 1;
}

bool ACPOMLPythonInterface::loadModel(std::string ModelSpecFile) {
  sendCommand("LoadModel " + ModelSpecFile);
  std::string Response = getResponse();
  std::vector<std::string> Tokens = tokenize(Response);
  if (Tokens[0] != RESPONSE_MODEL_LOADED) {
    return false;
  }
  if (Tokens[1] == RESPONSE_ALREADY_IN_DICT) {
    LLVM_DEBUG(dbgs() << "loadModel: the model specified in " << ModelSpecFile
                      << " has already been loaded\n");
    return true;
  }
  std::string ModelName = Tokens[1];
  int NumFeatures = std::stoi(Tokens[2]);
  LLVM_DEBUG(dbgs() << "Registering features: " << NumFeatures << "\n");
  registerModel(ModelName, NumFeatures);
  auto ModelPtr = ModelMap.find(ModelName)->second;
  std::string FeatureName = "";
  for (int I = 0; I < NumFeatures; I++) {
    FeatureName = Tokens[I + 3];
    if (!registerFeature(ModelName, FeatureName, I)) {
      return false;
    }
  }
  int OutputStart = 3 + NumFeatures;
  int NumOutputs = std::stoi(Tokens[OutputStart]);
  ModelPtr->setNumOutputs(NumOutputs);
  OutputStart++;
  std::string OutputName;
  std::string OutputType;
  for (int I = 0; I < NumOutputs; I++) {
    std::istringstream IS(Tokens[OutputStart + I]);
    IS >> OutputName >> OutputType;
    if (!registerOutput(ModelName, OutputName, OutputType)) {
      return false;
    }
  }
  std::string Signature = Tokens[OutputStart + NumOutputs];
  ModelPtr->setSignature(Signature);
  return true;
}

bool ACPOMLPythonInterface::registerModel(std::string ModelName,
                                          int NumFeatures) {
  auto Find = ModelMap.find(ModelName);
  if (Find != ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "registerModel: Model " << ModelName
                      << " already exists\n");
    return false;
  }
  std::shared_ptr<Model> NewModel = std::make_shared<Model>(NumFeatures);
  ModelMap.insert(std::make_pair(ModelName, NewModel));
  return true;
}

bool ACPOMLPythonInterface::registerModel(std::string ModelName,
                                          int NumFeatures, int NumOutputs) {
  auto Find = ModelMap.find(ModelName);
  if (Find != ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "registerModel: Model " << ModelName
                      << " already exists\n");
    return false;
  }
  std::shared_ptr<Model> NewModel =
      std::make_shared<Model>(NumFeatures, NumOutputs);
  ModelMap.insert(std::make_pair(ModelName, NewModel));
  return true;
}

bool ACPOMLPythonInterface::registerFeature(std::string ModelName,
                                            std::string FeatureName,
                                            int Index) {
  auto Find = ModelMap.find(ModelName);
  assert(Find != ModelMap.end());
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in registerFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  uint64_t ID = assignID();
  return Find->second->registerFeature(FeatureName, ID, Index);
}

bool ACPOMLPythonInterface::registerOutput(std::string ModelName,
                                           std::string OutputName,
                                           std::string OutputType) {
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in registerOutput: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  return Find->second->registerOutput(OutputName, OutputType);
}

int ACPOMLPythonInterface::getNumLoadedModels() { return ModelMap.size(); }

bool ACPOMLPythonInterface::defineInputIR(std::string Filename) {
  return false;
}

bool ACPOMLPythonInterface::setCustomFeature(std::string ModelName,
                                             uint64_t FeatureID,
                                             int FeatureValue) {
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureID);
  sendCommand("SetCustomFeature " + std::to_string(Index) + " " +
              std::to_string(FeatureValue));
  std::string Response = getResponse();
  return (Response.find(RESPONSE_FEATURE_SET) == 0);
}

bool ACPOMLPythonInterface::setCustomFeature(std::string ModelName,
                                             uint64_t FeatureID,
                                             int64_t FeatureValue) {
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureID);
  sendCommand("SetCustomFeature " + std::to_string(Index) + " " +
              std::to_string(FeatureValue));
  std::string Response = getResponse();
  return (Response.find(RESPONSE_FEATURE_SET) == 0);
}

bool ACPOMLPythonInterface::setCustomFeature(std::string ModelName,
                                             uint64_t FeatureID,
                                             double FeatureValue) {
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureID);
  sendCommand("SetCustomFeature " + std::to_string(Index) + " " +
              std::to_string(FeatureValue));
  std::string Response = getResponse();
  return (Response.find(RESPONSE_FEATURE_SET) == 0);
}

bool ACPOMLPythonInterface::setCustomFeature(std::string ModelName,
                                             uint64_t FeatureID,
                                             float FeatureValue) {
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureID);
  sendCommand("SetCustomFeature " + std::to_string(Index) + " " +
              std::to_string(FeatureValue));
  std::string Response = getResponse();
  return (Response.find(RESPONSE_FEATURE_SET) == 0);
}

bool ACPOMLPythonInterface::setCustomFeature(std::string ModelName,
                                             uint64_t FeatureID,
                                             bool FeatureValue) {
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureID);
  std::string Command = "SetCustomFeature " + std::to_string(Index) + " ";
  Command += FeatureValue ? "1" : "0";
  sendCommand(Command);
  std::string Response = getResponse();
  return (Response.find(RESPONSE_FEATURE_SET) == 0);
}

bool ACPOMLPythonInterface::setCustomFeature(std::string ModelName,
                                             std::string FeatureName,
                                             int FeatureValue) {
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureName);
  sendCommand("SetCustomFeature " + std::to_string(Index) + " " +
              std::to_string(FeatureValue));
  std::string Response = getResponse();
  return (Response.find(RESPONSE_FEATURE_SET) == 0);
}

bool ACPOMLPythonInterface::setCustomFeature(std::string ModelName,
                                             std::string FeatureName,
                                             int64_t FeatureValue) {
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureName);
  sendCommand("SetCustomFeature " + std::to_string(Index) + " " +
              std::to_string(FeatureValue));
  std::string Response = getResponse();
  return (Response.find(RESPONSE_FEATURE_SET) == 0);
}

bool ACPOMLPythonInterface::setCustomFeature(std::string ModelName,
                                             std::string FeatureName,
                                             double FeatureValue) {
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureName);
  sendCommand("SetCustomFeature " + std::to_string(Index) + " " +
              std::to_string(FeatureValue));
  std::string Response = getResponse();
  return (Response.find(RESPONSE_FEATURE_SET) == 0);
}

bool ACPOMLPythonInterface::setCustomFeature(std::string ModelName,
                                             std::string FeatureName,
                                             float FeatureValue) {
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureName);
  sendCommand("SetCustomFeature " + std::to_string(Index) + " " +
              std::to_string(FeatureValue));
  std::string Response = getResponse();
  return (Response.find(RESPONSE_FEATURE_SET) == 0);
}

bool ACPOMLPythonInterface::setCustomFeature(std::string ModelName,
                                             std::string FeatureName,
                                             bool FeatureValue) {
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureName);
  std::string Command = "SetCustomFeature " + std::to_string(Index) + " ";
  Command += FeatureValue ? "1" : "0";
  sendCommand(Command);
  std::string Response = getResponse();
  std::vector<std::string> Tokens = tokenize(Response);
  return (Response.find(RESPONSE_FEATURE_SET) == 0);
}

bool ACPOMLPythonInterface::initializeFeatures(
    std::string ModelName,
    const std::vector<std::pair<uint64_t, std::string>> &FeatureValues) {
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in initializeFeatures: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  if (FeatureValues.size() > Find->second->getNumFeatures()) {
    LLVM_DEBUG(dbgs() << "ERROR in initializeFeatures: Invalid features\n");
    return false;
  }
  CurrentlyActiveModel = ModelName;
  std::string Command = "InitializeFeatures " + ModelName;
  for (const auto &Feature : FeatureValues) {
    uint64_t FeatureID = Feature.first;
    std::string FeatureValue = Feature.second;
    int Index = Find->second->getIndex(FeatureID);
    Command += " " + std::to_string(Index) + " " + FeatureValue;
  }
  sendCommand(Command);
  std::string Response = getResponse();
  return (Response.find(RESPONSE_FEATURES_INITIALIZED) == 0);
}

bool ACPOMLPythonInterface::initializeFeatures(
    std::string ModelName,
    const std::vector<std::pair<std::string, std::string>> &FeatureValues) {
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in initializeFeatures: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  if (FeatureValues.size() > Find->second->getNumFeatures()) {
    LLVM_DEBUG(dbgs() << "ERROR in initializeFeatures: Invalid features\n");
    return false;
  }
  CurrentlyActiveModel = ModelName;
  std::string Command = "InitializeFeatures " + ModelName;
  for (const auto &Feature : FeatureValues) {
    std::string FeatureName = Feature.first;
    std::string FeatureValue = Feature.second;
    int Index = Find->second->getIndex(FeatureName);
    Command += " " + std::to_string(Index) + " " + FeatureValue;
  }
  sendCommand(Command);
  std::string Response = getResponse();
  return (Response.find(RESPONSE_FEATURES_INITIALIZED) == 0);
}

bool ACPOMLPythonInterface::setCustomFeatures(
    std::string ModelName,
    const std::vector<std::pair<uint64_t, std::string>> &FeatureValues) {
  if (ModelName != CurrentlyActiveModel) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeatures: Model " << ModelName
                      << " has not been loaded or is not active\n");
    return false;
  }
  auto Find = ModelMap.find(ModelName);
  if (FeatureValues.size() > Find->second->getNumFeatures()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeatures: Invalid features\n");
    return false;
  }
  std::string Command = "SetCustomFeatures";
  for (const auto &Feature : FeatureValues) {
    uint64_t FeatureID = Feature.first;
    std::string FeatureValue = Feature.second;
    int Index = Find->second->getIndex(FeatureID);
    Command += " " + std::to_string(Index) + " " + FeatureValue;
  }
  sendCommand(Command);
  std::string Response = getResponse();
  return (Response.find(RESPONSE_FEATURES_SET) == 0);
}

bool ACPOMLPythonInterface::setCustomFeatures(
    std::string ModelName,
    const std::vector<std::pair<std::string, std::string>> &FeatureValues) {
  if (ModelName != CurrentlyActiveModel) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeatures: Model " << ModelName
                      << " has not been loaded or is not active\n");
    return false;
  }
  auto Find = ModelMap.find(ModelName);
  if (FeatureValues.size() > Find->second->getNumFeatures()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeatures: Invalid features\n");
    return false;
  }
  std::string Command = "SetCustomFeatures";
  for (const auto &Feature : FeatureValues) {
    std::string FeatureName = Feature.first;
    std::string FeatureValue = Feature.second;
    int Index = Find->second->getIndex(FeatureName);
    Command += " " + std::to_string(Index) + " " + FeatureValue;
  }
  sendCommand(Command);
  std::string Response = getResponse();
  return (Response.find(RESPONSE_FEATURES_SET) == 0);
}

bool ACPOMLPythonInterface::runModel(std::string ModelName) {
  if (ModelName != CurrentlyActiveModel) {
    LLVM_DEBUG(dbgs() << "ERROR in runModel: Model " << ModelName
                      << " is not active\n");
    return false;
  }
  sendCommand("RunModel");
  std::string Response = getResponse();
  return (Response.find(RESPONSE_COMPLETED) == 0);
}

std::string ACPOMLPythonInterface::getOutputType(std::string ModelName,
                                                 std::string OutputName) {
  auto Find = ModelMap.find(ModelName);
  assert(Find != ModelMap.end());
  return Find->second->getOutputType(OutputName);
}

int ACPOMLPythonInterface::getModelResultI(std::string OutputName) {
  auto Find = ModelMap.find(CurrentlyActiveModel);
  assert(Find->second->checkOutputExists(OutputName));
  sendCommand("GetModelOutput " + OutputName);
  std::string Response = getResponse();
  std::vector<std::string> Tokens = tokenize(Response);
  assert(Tokens.size() == 3);
  assert(Tokens[0] == OutputName);
  int Result = std::stoi(Tokens[2]);
  return Result;
}

int64_t ACPOMLPythonInterface::getModelResultI64(std::string OutputName) {
  auto Find = ModelMap.find(CurrentlyActiveModel);
  assert(Find->second->checkOutputExists(OutputName));
  sendCommand("GetModelOutput " + OutputName);
  std::string Response = getResponse();
  std::vector<std::string> Tokens = tokenize(Response);
  assert(Tokens.size() == 3);
  assert(Tokens[0] == OutputName);
  int64_t Result = std::stol(Tokens[2]);
  return Result;
}

float ACPOMLPythonInterface::getModelResultF(std::string OutputName) {
  auto Find = ModelMap.find(CurrentlyActiveModel);
  assert(Find->second->checkOutputExists(OutputName));
  sendCommand("GetModelOutput " + OutputName);
  std::string Response = getResponse();
  std::vector<std::string> Tokens = tokenize(Response);
  assert(Tokens.size() == 3);
  assert(Tokens[0] == OutputName);
  float Result = std::stof(Tokens[2]);
  return Result;
}

double ACPOMLPythonInterface::getModelResultD(std::string OutputName) {
  auto Find = ModelMap.find(CurrentlyActiveModel);
  assert(Find->second->checkOutputExists(OutputName));
  sendCommand("GetModelOutput " + OutputName);
  std::string Response = getResponse();
  std::vector<std::string> Tokens = tokenize(Response);
  assert(Tokens.size() == 3);
  assert(Tokens[0] == OutputName);
  double Result = std::stod(Tokens[2]);
  return Result;
}

bool ACPOMLPythonInterface::getModelResultB(std::string OutputName) {
  auto Find = ModelMap.find(CurrentlyActiveModel);
  assert(Find->second->checkOutputExists(OutputName));
  sendCommand("GetModelOutput " + OutputName);
  std::string Response = getResponse();
  std::vector<std::string> Tokens = tokenize(Response);
  assert(Tokens.size() == 3);
  assert(Tokens[0] == OutputName);
  return (Tokens[2] == "1");
}

int ACPOMLPythonInterface::getStatus() {
  sendCommand("GetStatus");
  std::string Response = getResponse();
  return Response.find(RESPONSE_ACTIVE) == 0;
}

bool ACPOMLPythonInterface::releaseModel(std::string ModelName) {
  sendCommand("ReleaseModel " + ModelName);
  std::string Response = getResponse();
  ModelMap.erase(ModelName);
  CurrentlyActiveModel = "";
  return true;
}

bool ACPOMLPythonInterface::closeMLInterface() {
  sendCommand("CloseMLInterface");
  std::string Response = getResponse();
  return true;
}

void ACPOMLPythonInterface::sendCommand(const std::string &Command) {
  fprintf(PipeOut,"%s\n", Command.c_str());
  fflush(PipeOut);
  usleep(1);
}

void ACPOMLPythonInterface::sendCommand(
    const std::vector<std::string> &Features) {
  for (auto I = Features.begin(); I != Features.end(); I++) {
    fprintf(PipeOut,"%s\n", I->c_str());
    fflush(PipeOut);
    usleep(1);
  }
}

std::string ACPOMLPythonInterface::getResponse() {
  std::string Response = "";
  char Letter = getc(PipeIn);
  while (Letter != '\n') {
    if (feof(PipeIn))
      assert(false && "ACPO pipeline is closed unexpectively.");

    Response += Letter;
    Letter = getc(PipeIn);
  }
  Response += '\n';
  if (Response.substr(0, 5) == RESPONSE_ERROR) {
    LLVM_DEBUG(dbgs() << Response);
    assert(false && "MLInterface reutrned error");
  }
  return Response;
}

std::vector<std::string>
ACPOMLPythonInterface::tokenize(const std::string &Line) {
  std::vector<std::string> Result;
  std::string Temp = Line;
  auto Loc = Temp.find(",");
  while (Loc != std::string::npos) {
    std::string Sub = Temp.substr(0, Loc);
    Result.push_back(Sub);
    Temp = Temp.substr(Loc + 1);
    Loc = Temp.find(",");
  }
  if (Temp.length() > 0)
    Result.push_back(Temp);

  return Result;
}

std::shared_ptr<ACPOMLInterface> llvm::createPersistentPythonMLIF() {
  if (PersistentMLIF == nullptr) {
    PersistentMLIF = std::make_shared<ACPOMLPythonInterface>();

    if (!PersistentMLIF->isInitialized())
      PersistentMLIF = nullptr;
  }
  return PersistentMLIF;
}

ACPOMLCPPInterface::ACPOMLCPPInterface() { setInitialized(true); }

ACPOMLCPPInterface::~ACPOMLCPPInterface() {}

uint64_t ACPOMLCPPInterface::assignID() {
  NextID++;
  return NextID - 1;
}

bool ACPOMLCPPInterface::loadModel(std::string ModelSpecFile) {
  std::string ModelName = readModelParam(ModelSpecFile, "ModelName");
  // Check if the model is already in the dictionary
  if (RunnerMap.find(ModelName) != RunnerMap.end()) {
    LLVM_DEBUG(dbgs() << "loadModel: the compiled model '" << ModelName
                      << "' has already been loaded\n");
    return true;
  }
  std::vector<std::pair<std::string, std::string>> Features{};
  readFeatures(ModelSpecFile, Features);
  std::vector<std::pair<std::string, std::string>> Outputs{};
  readOutputs(ModelSpecFile, Outputs);

  LLVM_DEBUG(llvm::dbgs() << "Loading compiled model with name " << ModelName
                          << "\n");

  auto CreatorFunctionIterator = CreateModelRunnerMap.find(ModelName);
  if (CreatorFunctionIterator == CreateModelRunnerMap.end()) {
    LLVM_DEBUG(llvm::dbgs()
               << ("Could not find compiled model class for model '" +
                   ModelName + "'\n"));
    return false;
  }

  auto CreatorFunction = CreatorFunctionIterator->second;

  std::string OutputKey = readModelParam(ModelSpecFile, "OutputKey");
  auto ModelRunner = CreatorFunction(Features, OutputKey);

  registerModel(ModelName, Features.size());
  RunnerMap.insert(std::make_pair(ModelName, std::move(ModelRunner)));
  auto ModelPtr = ModelMap.find(ModelName)->second;
  for (size_t I = 0; I < Features.size(); I++) {
    if (!registerFeature(ModelName, Features[I].first, I)) {
      return false;
    }
    if (!ModelPtr->registerInput(Features[I].first, Features[I].second)) {
      return false;
    }
  }

  ModelPtr->setNumOutputs(Outputs.size());
  for (size_t I = 0; I < Outputs.size(); I++) {
    if (!registerOutput(ModelName, Outputs[I].first, Outputs[I].second)) {
      return false;
    }
  }

  LLVM_DEBUG(llvm::dbgs() << "Model " << ModelName
                          << " was successfully loaded\n");

  // We do not need to set signature here because it is already given to make
  // the precompiled model
  return true;
}

bool ACPOMLCPPInterface::registerModel(std::string ModelName, int NumFeatures) {
  auto Find = ModelMap.find(ModelName);
  if (Find != ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "registerModel: Model " << ModelName
                      << " already exists\n");
    return false;
  }
  std::shared_ptr<Model> NewModel = std::make_shared<Model>(NumFeatures);
  ModelMap.insert(std::make_pair(ModelName, NewModel));
  return true;
}

bool ACPOMLCPPInterface::registerModel(std::string ModelName, int NumFeatures,
                                       int NumOutputs) {
  auto Find = ModelMap.find(ModelName);
  if (Find != ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "registerModel: Model " << ModelName
                      << " already exists\n");
    return false;
  }
  std::shared_ptr<Model> NewModel =
      std::make_shared<Model>(NumFeatures, NumOutputs);
  ModelMap.insert(std::make_pair(ModelName, NewModel));
  return true;
}

bool ACPOMLCPPInterface::registerFeature(std::string ModelName,
                                         std::string FeatureName, int Index) {
  auto Find = ModelMap.find(ModelName);
  assert(Find != ModelMap.end());
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in registerFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  uint64_t ID = assignID();
  return Find->second->registerFeature(FeatureName, ID, Index);
}

bool ACPOMLCPPInterface::registerOutput(std::string ModelName,
                                        std::string OutputName,
                                        std::string OutputType) {
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in registerOutput: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  return Find->second->registerOutput(OutputName, OutputType);
}

int ACPOMLCPPInterface::getNumLoadedModels() { return ModelMap.size(); }

bool ACPOMLCPPInterface::defineInputIR(std::string Filename) { return false; }

bool ACPOMLCPPInterface::setCustomFeature(std::string ModelName,
                                          uint64_t FeatureID,
                                          int FeatureValue) {
  LLVM_DEBUG(
      dbgs()
      << "ACPOMLCPPInterface: setting custom feature of type int in model "
      << ModelName << "\n");
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureID);
  std::shared_ptr<llvm::ACPOModelRunner> Runner =
      RunnerMap.find(ModelName)->second;
  return Runner->setCustomFeature(Index, FeatureValue);
}

bool ACPOMLCPPInterface::setCustomFeature(std::string ModelName,
                                          uint64_t FeatureID,
                                          int64_t FeatureValue) {
  LLVM_DEBUG(
      dbgs()
      << "ACPOMLCPPInterface: setting custom feature of type double in model "
      << ModelName << "\n");
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureID);
  std::shared_ptr<llvm::ACPOModelRunner> Runner =
      RunnerMap.find(ModelName)->second;
  return Runner->setCustomFeature(Index, FeatureValue);
}

bool ACPOMLCPPInterface::setCustomFeature(std::string ModelName,
                                          uint64_t FeatureID,
                                          double FeatureValue) {
  LLVM_DEBUG(
      dbgs()
      << "ACPOMLCPPInterface: setting custom feature of type double in model "
      << ModelName << "\n");
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureID);
  std::shared_ptr<llvm::ACPOModelRunner> Runner =
      RunnerMap.find(ModelName)->second;
  return Runner->setCustomFeature(Index, FeatureValue);
}

bool ACPOMLCPPInterface::setCustomFeature(std::string ModelName,
                                          uint64_t FeatureID,
                                          float FeatureValue) {
  LLVM_DEBUG(
      dbgs()
      << "ACPOMLCPPInterface: setting custom feature of type float in model "
      << ModelName << "\n");
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureID);
  std::shared_ptr<llvm::ACPOModelRunner> Runner =
      RunnerMap.find(ModelName)->second;
  return Runner->setCustomFeature(Index, FeatureValue);
}

bool ACPOMLCPPInterface::setCustomFeature(std::string ModelName,
                                          uint64_t FeatureID,
                                          bool FeatureValue) {
  LLVM_DEBUG(
      dbgs()
      << "ACPOMLCPPInterface: setting custom feature of type bool in model "
      << ModelName << "\n");
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureID);
  std::shared_ptr<llvm::ACPOModelRunner> Runner =
      RunnerMap.find(ModelName)->second;
  return Runner->setCustomFeature(Index, FeatureValue);
}

bool ACPOMLCPPInterface::setCustomFeature(std::string ModelName,
                                          std::string FeatureName,
                                          int FeatureValue) {
  LLVM_DEBUG(
      dbgs()
      << "ACPOMLCPPInterface: setting custom feature of type int in model "
      << ModelName << "\n");
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureName);
  std::shared_ptr<llvm::ACPOModelRunner> Runner =
      RunnerMap.find(ModelName)->second;
  return Runner->setCustomFeature(Index, FeatureValue);
}

bool ACPOMLCPPInterface::setCustomFeature(std::string ModelName,
                                          std::string FeatureName,
                                          int64_t FeatureValue) {
  LLVM_DEBUG(
      dbgs()
      << "ACPOMLCPPInterface: setting custom feature of type int64 in model "
      << ModelName << "\n");
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureName);
  std::shared_ptr<llvm::ACPOModelRunner> Runner =
      RunnerMap.find(ModelName)->second;
  return Runner->setCustomFeature(Index, FeatureValue);
}

bool ACPOMLCPPInterface::setCustomFeature(std::string ModelName,
                                          std::string FeatureName,
                                          double FeatureValue) {
  LLVM_DEBUG(
      dbgs()
      << "ACPOMLCPPInterface: setting custom feature of type double in model "
      << ModelName << "\n");
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureName);
  std::shared_ptr<llvm::ACPOModelRunner> Runner =
      RunnerMap.find(ModelName)->second;
  return Runner->setCustomFeature(Index, FeatureValue);
}

bool ACPOMLCPPInterface::setCustomFeature(std::string ModelName,
                                          std::string FeatureName,
                                          float FeatureValue) {
  LLVM_DEBUG(
      dbgs()
      << "ACPOMLCPPInterface: setting custom feature of type float in model "
      << ModelName << "\n");
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureName);
  std::shared_ptr<llvm::ACPOModelRunner> Runner =
      RunnerMap.find(ModelName)->second;
  return Runner->setCustomFeature(Index, FeatureValue);
}

bool ACPOMLCPPInterface::setCustomFeature(std::string ModelName,
                                          std::string FeatureName,
                                          bool FeatureValue) {
  LLVM_DEBUG(
      dbgs()
      << "ACPOMLCPPInterface: setting custom feature of type bool in model "
      << ModelName << "\n");
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeature: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  int Index = Find->second->getIndex(FeatureName);
  std::shared_ptr<llvm::ACPOModelRunner> Runner =
      RunnerMap.find(ModelName)->second;
  return Runner->setCustomFeature(Index, FeatureValue);
}

bool ACPOMLCPPInterface::initializeFeatures(
    std::string ModelName,
    const std::vector<std::pair<uint64_t, std::string>> &FeatureValues) {
  LLVM_DEBUG(dbgs() << "Initializing features for model " << ModelName
                    << " using feature IDs\n");
  auto Find = ModelMap.find(ModelName);
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in initializeFeatures: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  if (FeatureValues.size() > Find->second->getNumFeatures()) {
    LLVM_DEBUG(dbgs() << "ERROR in initializeFeatures: Invalid features\n");
    return false;
  }
  CurrentlyActiveModel = ModelName;
  for (const auto &Feature : FeatureValues) {
    uint64_t FeatureID = Feature.first;
    std::string FeatureValue = Feature.second;

    std::string FeatureType =
        getInputType(ModelName, Find->second->getName(FeatureID));
    if (FeatureType == "int64") {
      int64_t Value = std::stoi(FeatureValue);
      setCustomFeature(ModelName, FeatureID, Value);
    } else if (FeatureType == "int32") {
      int32_t Value = std::stoi(FeatureValue);
      setCustomFeature(ModelName, FeatureID, Value);
    } else if (FeatureType == "int") {
      int Value = std::stoi(FeatureValue);
      setCustomFeature(ModelName, FeatureID, Value);
    } else if (FeatureType == "float64") {
      double Value = std::stod(FeatureValue);
      setCustomFeature(ModelName, FeatureID, Value);
    } else if (FeatureType == "float32") {
      float Value = std::stof(FeatureValue);
      setCustomFeature(ModelName, FeatureID, Value);
    } else {
      LLVM_DEBUG(dbgs() << "ERROR in initializeFeatures: Invalid feature type "
                        << FeatureType << "\n");
      return false;
    }
  }
  return true;
}

bool ACPOMLCPPInterface::initializeFeatures(
    std::string ModelName,
    const std::vector<std::pair<std::string, std::string>> &FeatureValues) {
  auto Find = ModelMap.find(ModelName);
  LLVM_DEBUG(dbgs() << "Initializing features for model " << ModelName
                    << " using feature names\n");
  if (Find == ModelMap.end()) {
    LLVM_DEBUG(dbgs() << "ERROR in initializeFeatures: Model " << ModelName
                      << " has not been loaded\n");
    return false;
  }
  if (FeatureValues.size() > Find->second->getNumFeatures()) {
    LLVM_DEBUG(dbgs() << "ERROR in initializeFeatures: Invalid features\n");
    return false;
  }
  CurrentlyActiveModel = ModelName;
  for (const auto &Feature : FeatureValues) {
    std::string FeatureName = Feature.first;
    std::string FeatureValue = Feature.second;

    std::string FeatureType = getInputType(ModelName, FeatureName);
    if (FeatureType == "int64") {
      int64_t Value = std::stol(FeatureValue);
      setCustomFeature(ModelName, FeatureName, Value);
    } else if (FeatureType == "int32") {
      int32_t Value = std::stoi(FeatureValue);
      setCustomFeature(ModelName, FeatureName, Value);
    } else if (FeatureType == "int") {
      int Value = std::stoi(FeatureValue);
      setCustomFeature(ModelName, FeatureName, Value);
    } else if (FeatureType == "float64") {
      double Value = std::stod(FeatureValue);
      setCustomFeature(ModelName, FeatureName, Value);
    } else if (FeatureType == "float32") {
      float Value = std::stof(FeatureValue);
      setCustomFeature(ModelName, FeatureName, Value);
    } else {
      LLVM_DEBUG(dbgs() << "ERROR in initializeFeatures: Invalid feature type "
                        << FeatureType << "\n");
      return false;
    }
  }
  return true;
}

bool ACPOMLCPPInterface::setCustomFeatures(
    std::string ModelName,
    const std::vector<std::pair<uint64_t, std::string>> &FeatureValues) {
  if (ModelName != CurrentlyActiveModel) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeatures: Model " << ModelName
                      << " has not been loaded or is not active\n");
    return false;
  }
  auto Find = ModelMap.find(ModelName);
  if (FeatureValues.size() > Find->second->getNumFeatures()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeatures: Invalid features\n");
    return false;
  }
  std::string Command = "SetCustomFeatures";
  for (const auto &Feature : FeatureValues) {
    uint64_t FeatureID = Feature.first;
    std::string FeatureValue = Feature.second;

    std::string FeatureType =
        getInputType(ModelName, Find->second->getName(FeatureID));
    if (FeatureType == "int64") {
      int64_t Value = std::stol(FeatureValue);
      setCustomFeature(ModelName, FeatureID, Value);
    } else if (FeatureType == "int32") {
      int32_t Value = std::stoi(FeatureValue);
      setCustomFeature(ModelName, FeatureID, Value);
    } else if (FeatureType == "int") {
      int Value = std::stoi(FeatureValue);
      setCustomFeature(ModelName, FeatureID, Value);
    } else if (FeatureType == "float64") {
      double Value = std::stod(FeatureValue);
      setCustomFeature(ModelName, FeatureID, Value);
    } else if (FeatureType == "float32") {
      float Value = std::stof(FeatureValue);
      setCustomFeature(ModelName, FeatureID, Value);
    } else {
      LLVM_DEBUG(dbgs() << "ERROR in setCustomFeatures: Invalid feature type "
                        << FeatureType << "\n");
      return false;
    }
  }
  return true;
}

bool ACPOMLCPPInterface::setCustomFeatures(
    std::string ModelName,
    const std::vector<std::pair<std::string, std::string>> &FeatureValues) {
  if (ModelName != CurrentlyActiveModel) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeatures: Model " << ModelName
                      << " has not been loaded or is not active\n");
    return false;
  }
  auto Find = ModelMap.find(ModelName);
  if (FeatureValues.size() > Find->second->getNumFeatures()) {
    LLVM_DEBUG(dbgs() << "ERROR in setCustomFeatures: Invalid features\n");
    return false;
  }
  std::string Command = "SetCustomFeatures";
  for (const auto &Feature : FeatureValues) {
    std::string FeatureName = Feature.first;
    std::string FeatureValueStr = Feature.second;

    std::string FeatureType = getInputType(ModelName, FeatureName);
    if (FeatureType == "int64") {
      int64_t FeatureValue = std::stoi(FeatureValueStr);
      setCustomFeature(ModelName, FeatureName, FeatureValue);
    } else if (FeatureType == "int32") {
      int32_t FeatureValue = std::stoi(FeatureValueStr);
      setCustomFeature(ModelName, FeatureName, FeatureValue);
    } else if (FeatureType == "int") {
      int FeatureValue = std::stoi(FeatureValueStr);
      setCustomFeature(ModelName, FeatureName, FeatureValue);
    } else if (FeatureType == "float64") {
      double FeatureValue = std::stod(FeatureValueStr);
      setCustomFeature(ModelName, FeatureName, FeatureValue);
    } else if (FeatureType == "float32") {
      float FeatureValue = std::stof(FeatureValueStr);
      setCustomFeature(ModelName, FeatureName, FeatureValue);
    } else {
      LLVM_DEBUG(dbgs() << "ERROR in setCustomFeatures: Invalid feature type "
                        << FeatureType << "\n");
      return false;
    }
  }
  return true;
}

bool ACPOMLCPPInterface::runModel(std::string ModelName) {
  if (ModelName != CurrentlyActiveModel) {
    LLVM_DEBUG(dbgs() << "ERROR in runModel: Model " << ModelName
                      << " is not active\n");
    return false;
  }
  std::shared_ptr<llvm::ACPOModelRunner> Runner =
      RunnerMap.find(CurrentlyActiveModel)->second;
  return Runner->runModel();
}

std::string ACPOMLCPPInterface::getInputType(std::string ModelName,
                                             std::string InputName) {
  auto Find = ModelMap.find(ModelName);
  assert(Find != ModelMap.end());
  return Find->second->getInputType(InputName);
}

std::string ACPOMLCPPInterface::getOutputType(std::string ModelName,
                                              std::string OutputName) {
  auto Find = ModelMap.find(ModelName);
  assert(Find != ModelMap.end());
  return Find->second->getOutputType(OutputName);
}

int ACPOMLCPPInterface::getModelResultI(std::string OutputName) {
  std::shared_ptr<llvm::ACPOModelRunner> Runner =
      RunnerMap.find(CurrentlyActiveModel)->second;
  return Runner->getModelResultI(OutputName);
}

int64_t ACPOMLCPPInterface::getModelResultI64(std::string OutputName) {
  std::shared_ptr<llvm::ACPOModelRunner> Runner =
      RunnerMap.find(CurrentlyActiveModel)->second;
  return Runner->getModelResultI64(OutputName);
}

float ACPOMLCPPInterface::getModelResultF(std::string OutputName) {
  std::shared_ptr<llvm::ACPOModelRunner> Runner =
      RunnerMap.find(CurrentlyActiveModel)->second;
  return Runner->getModelResultF(OutputName);
}

double ACPOMLCPPInterface::getModelResultD(std::string OutputName) {
  std::shared_ptr<llvm::ACPOModelRunner> Runner =
      RunnerMap.find(CurrentlyActiveModel)->second;
  return Runner->getModelResultD(OutputName);
}

bool ACPOMLCPPInterface::getModelResultB(std::string OutputName) {
  std::shared_ptr<llvm::ACPOModelRunner> Runner =
      RunnerMap.find(CurrentlyActiveModel)->second;
  return Runner->getModelResultB(OutputName);
}

int ACPOMLCPPInterface::getStatus() { return 1; }

bool ACPOMLCPPInterface::releaseModel(std::string ModelName) {
  ModelMap.erase(ModelName);
  RunnerMap.erase(ModelName);
  CurrentlyActiveModel = "";
  return true;
}

bool ACPOMLCPPInterface::closeMLInterface() { return true; }

std::string ACPOMLCPPInterface::readModelParam(std::string FilePath,
                                               std::string Param) {
  std::optional<std::string> Env = llvm::sys::Process::GetEnv(ACPO_ENV_VAR_DIR);
  if (!Env || *Env == "") {
    std::optional<std::string> LLVMDIROpt =
        llvm::sys::Process::GetEnv("LLVM_DIR");
    if (LLVMDIROpt) {
      Env = *LLVMDIROpt + "/acpo/";
    } else {
      return "";
    }
  }

  FilePath = *Env + "/" + FilePath;

  std::ifstream FileStream{FilePath};

  std::string Line;
  while (std::getline(FileStream, Line)) {
    if (Line.rfind(Param, 0) == 0) {
      return Line.substr(Param.size() + 1);
    }
  }
  return "";
}

void ACPOMLCPPInterface::readFeatures(
    std::string FilePath,
    std::vector<std::pair<std::string, std::string>> &Features) {
  std::string Line = readModelParam(FilePath, "Features");
  while (!Line.empty()) {
    // This reads the features, assuming each feature is written as
    // {feature_name, feature_type}
    size_t LeftBracket = Line.find("{");
    size_t Comma = Line.find(",", LeftBracket);
    size_t Space = Line.find(" ", Comma);
    size_t RightBracket = Line.find("}", Space);
    if (LeftBracket == Line.size() || Comma == Line.size() ||
        Space == Line.size() || RightBracket == Line.size()) {
      break;
    }
    std::string Feature = Line.substr(LeftBracket + 1, Comma - LeftBracket - 1);
    std::string Type = Line.substr(Space + 1, RightBracket - Space - 1);

    Features.emplace_back(std::make_pair(Feature, Type));
    int oldLength = Line.size();
    Line = Line.substr(RightBracket + 1);
    int newLength = Line.size();
    if (oldLength == newLength)
      break;
  }
}

void ACPOMLCPPInterface::readOutputs(
    std::string FilePath,
    std::vector<std::pair<std::string, std::string>> &Outputs) {
  std::string Line = readModelParam(FilePath, "Outputs");
  while (!Line.empty()) {
    // This reads the features, assuming each feature is written as
    // {feature_name, feature_type}
    size_t LeftBracket = Line.find("{");
    size_t Comma = Line.find(",", LeftBracket);
    size_t Space = Line.find(" ", Comma);
    size_t RightBracket = Line.find("}", Space);
    if (LeftBracket == Line.size() || Comma == Line.size() ||
        Space == Line.size() || RightBracket == Line.size()) {
      break;
    }
    std::string Output = Line.substr(LeftBracket + 1, Comma - LeftBracket - 1);
    std::string Type = Line.substr(Space + 1, RightBracket - Space - 1);

    Outputs.emplace_back(std::make_pair(Output, Type));
    int oldLength = Line.size();
    Line = Line.substr(RightBracket + 1);
    int newLength = Line.size();
    if (oldLength == newLength)
      break;
  }
}

std::shared_ptr<ACPOMLInterface> llvm::createPersistentCompiledMLIF() {
  if (PersistentMLIF == nullptr) {
    PersistentMLIF = std::make_shared<ACPOMLCPPInterface>();
    if (!PersistentMLIF->isInitialized())
      PersistentMLIF = nullptr;
  }
  return PersistentMLIF;
}

#ifdef LLVM_HAVE_TF_AOT_FICOMPILEDMODEL
std::unique_ptr<ACPOModelRunner>
createFI(std::vector<std::pair<std::string, std::string>> Inputs,
         StringRef Decision) {
  // Context does not ever seem to be used in the model runner,
  // so for now just create an empty context object
  LLVMContext Ctx;
  return std::make_unique<FIModelRunner>(Ctx, Inputs, Decision);
}
#endif

// Generate map using ifdefs for now, in the future we could have this
// automatically populate using macros
const std::unordered_map<std::string,
                         ACPOMLCPPInterface::CreateModelRunnerFunction>
    ACPOMLCPPInterface::CreateModelRunnerMap = {
#ifdef LLVM_HAVE_TF_AOT_FICOMPILEDMODEL
        {"FI", createFI},
#endif
};
#endif
