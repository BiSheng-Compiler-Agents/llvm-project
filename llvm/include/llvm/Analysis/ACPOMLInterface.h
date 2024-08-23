//===- ACPOMLInterface.h - AI-Enabled Continuous Program Optimization -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(ENABLE_ACPO)
#ifndef LLVM_ANALYSIS_ACPOML_INTERFACE_H
#define LLVM_ANALYSIS_ACPOML_INTERFACE_H

#include "llvm/Analysis/ACPOModelRunner.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Program.h"

#include <cstddef>
#include <ios>
#include <memory>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace llvm {

class ACPOModelRunner;

// This is class for storing information about a model.
class Model {
public:
  // Constructors
  Model() : NumFeatures{1}, NumOutputs{1} {}
  Model(std::size_t NumFeatures) : NumFeatures{NumFeatures}, NumOutputs{1} {}
  Model(std::size_t NumFeatures, int NumOutputs)
      : NumFeatures{NumFeatures}, NumOutputs{NumOutputs} {}

  // Getters/Setters
  std::size_t getNumFeatures() const { return NumFeatures; }
  void setNumFeatures(int NumFeatures) { this->NumFeatures = NumFeatures; }

  int getNumOutputs() const { return NumOutputs; }
  void setNumOutputs(int NumOutputs) { this->NumOutputs = NumOutputs; }

  std::string getSignature() const { return Signature; }
  void setSignature(std::string Signature) { this->Signature = Signature; }

  // Register a feature into the NametoID and IDToIndex maps.
  bool registerFeature(std::string FeatureName, uint64_t FeatureID, int Index);

  // Register an input into the map.
  bool registerInput(std::string InputName, std::string InputType);

  // Register an output into the map.
  bool registerOutput(std::string OutputName, std::string OutputType);

  // Return the index of a feature within the feature list used by inference().
  int getIndex(uint64_t FeatureID) const;
  int getIndex(std::string FeatureName) const;

  // Return the name of a feature within the feature list used by inference().
  std::string getName(uint64_t FeatureID) const;

  // Return true if output name exists.
  bool checkOutputExists(std::string OutputName) const;

  // Return the type of an input given its name.
  std::string getInputType(std::string OutputName) const;

  // Return the type of an output given its name.
  std::string getOutputType(std::string OutputName) const;

private:
  std::size_t NumFeatures;
  int NumOutputs;
  std::string Signature;
  std::unordered_map<std::string, uint64_t> NameToID;
  std::unordered_map<uint64_t, std::string> IDToName;
  std::unordered_map<uint64_t, int> IDToIndex;

  // A map from input name to input type
  std::unordered_map<std::string, std::string> InputMap;

  // A map from output name to output type
  std::unordered_map<std::string, std::string> OutputMap;
};

// This is the base class to define an interface with an ML framework.
class ACPOMLInterface {
public:
  // Constructor/Destructor.
  ACPOMLInterface() {}
  virtual ~ACPOMLInterface() {}

  // Getters/Setters
  bool isInitialized() const { return Initialized; }
  void setInitialized(bool Val) { Initialized = Val; }

  // Interface methods.
  // Return the next available ID for a feature.
  virtual uint64_t assignID() = 0;

  // Load a model by reading from the specified file.
  // Return false if the operation failed.
  virtual bool loadModel(std::string ModelSpecFile) = 0;

  // Insert a new model into the model map.
  virtual bool registerModel(std::string ModelName, int NumFeatures) = 0;
  virtual bool registerModel(std::string ModelName, int NumFeatures,
                             int NumOutputs) = 0;

  // Register a new feature for a given model.
  virtual bool registerFeature(std::string ModelName, std::string FeatureName,
                               int Index) = 0;

  // Register a new output for a given model.
  virtual bool registerOutput(std::string ModelName, std::string OutputName,
                              std::string OutputType) = 0;

  // Specify how many models are currently live in ML framework memory.
  virtual int getNumLoadedModels() = 0;

  // Specify the input file to use as IR to be passed to the model (however
  // it is processed afterwards). Return false if the operation failed.
  virtual bool defineInputIR(std::string Filename) = 0;

  // Specify a custom feature for a model to use as input at the next model
  // invocation. Return false if the operation failed.
  virtual bool setCustomFeature(std::string ModelName, uint64_t FeatureID,
                                int FeatureValue) = 0;
  virtual bool setCustomFeature(std::string ModelName, uint64_t FeatureID,
                                int64_t FeatureValue) = 0;
  virtual bool setCustomFeature(std::string ModelName, uint64_t FeatureID,
                                double FeatureValue) = 0;
  virtual bool setCustomFeature(std::string ModelName, uint64_t FeatureID,
                                float FeatureValue) = 0;
  virtual bool setCustomFeature(std::string ModelName, uint64_t FeatureID,
                                bool FeatureValue) = 0;
  virtual bool setCustomFeature(std::string ModelName, std::string FeatureName,
                                int FeatureValue) = 0;
  virtual bool setCustomFeature(std::string ModelName, std::string FeatureName,
                                int64_t FeatureValue) = 0;
  virtual bool setCustomFeature(std::string ModelName, std::string FeatureName,
                                double FeatureValue) = 0;
  virtual bool setCustomFeature(std::string ModelName, std::string FeatureName,
                                float FeatureValue) = 0;
  virtual bool setCustomFeature(std::string ModelName, std::string FeatureName,
                                bool FeatureValue) = 0;

  // Replace all features with the given feature values.
  // Activate the specified model.
  virtual bool initializeFeatures(
      std::string ModelName,
      const std::vector<std::pair<uint64_t, std::string>> &FeatureValues) = 0;

  virtual bool
  initializeFeatures(std::string ModelName,
                     const std::vector<std::pair<std::string, std::string>>
                         &FeatureValues) = 0;

  // Set features with the specified feature values.
  // Not changing the currently active model.
  virtual bool setCustomFeatures(
      std::string ModelName,
      const std::vector<std::pair<uint64_t, std::string>> &FeatureValues) = 0;

  virtual bool
  setCustomFeatures(std::string ModelName,
                    const std::vector<std::pair<std::string, std::string>>
                        &FeatureValues) = 0;

  // Run a model with specified name. Return false if the execution was not
  // possible or an error was encountered.
  virtual bool runModel(std::string ModelName) = 0;

  // Return the type of an output within the model specified by the name.
  virtual std::string getOutputType(std::string ModelName,
                                    std::string OutputName) = 0;

  // Return model results, based on the output name.
  virtual int getModelResultI(std::string OutputName) = 0;
  virtual int64_t getModelResultI64(std::string OutputName) = 0;
  virtual float getModelResultF(std::string OutputName) = 0;
  virtual double getModelResultD(std::string OutputName) = 0;
  virtual bool getModelResultB(std::string OutputName) = 0;

  // Get status of the ML interface. Return zero if succeeded.
  virtual int getStatus() = 0;

  // Free up memory taken by a model.
  virtual bool releaseModel(std::string ModelName) = 0;

  // Close interface when done. Return false if the command was not successful.
  // In some cases this just requires a constructor for this class to be called,
  // but in others, additional steps that require feedback may be needed.
  virtual bool closeMLInterface() = 0;

  // Set a flag to invoke closeMLInterface when the instance of the class is
  // destroyed.
  void setCloseOnDestruction() { CloseOnDestruction = true; }

protected:
  bool CloseOnDestruction = false;

private:
  bool Initialized = false;
};

class ACPOMLPythonInterface : public ACPOMLInterface {
public:
  ACPOMLPythonInterface();
  virtual ~ACPOMLPythonInterface();

  // Interface methods.
  // Return the next available ID for a feature.
  virtual uint64_t assignID() override;

  // Load a model by reading from the specified file.
  // Return false if the operation failed.
  virtual bool loadModel(std::string ModelSpecFile) override;

  // Insert a new model into the model map.
  virtual bool registerModel(std::string ModelName, int NumFeatures) override;
  virtual bool registerModel(std::string ModelName, int NumFeatures,
                             int NumOutputs) override;

  // Register a new feature for a given model.
  virtual bool registerFeature(std::string ModelName, std::string FeatureName,
                               int Index) override;

  // Register a new output for a given model.
  virtual bool registerOutput(std::string ModelName, std::string OutputName,
                              std::string OutputType) override;

  // Specify how many models are currently live in ML framework memory.
  virtual int getNumLoadedModels() override;

  // Specify the input file to use as IR to be passed to the model (however
  // it is processed afterwards). Return false if the operation failed.
  virtual bool defineInputIR(std::string Filename) override;

  // Specify a custom feature for a model to use as input at the next model
  // invocation. Return false if the operation failed.
  virtual bool setCustomFeature(std::string ModelName, uint64_t FeatureID,
                                int FeatureValue) override;
  virtual bool setCustomFeature(std::string ModelName, uint64_t FeatureID,
                                int64_t FeatureValue) override;
  virtual bool setCustomFeature(std::string ModelName, uint64_t FeatureID,
                                double FeatureValue) override;
  virtual bool setCustomFeature(std::string ModelName, uint64_t FeatureID,
                                float FeatureValue) override;
  virtual bool setCustomFeature(std::string ModelName, uint64_t FeatureID,
                                bool FeatureValue) override;

  virtual bool setCustomFeature(std::string ModelName, std::string FeatureName,
                                int FeatureValue) override;
  virtual bool setCustomFeature(std::string ModelName, std::string FeatureName,
                                int64_t FeatureValue) override;
  virtual bool setCustomFeature(std::string ModelName, std::string FeatureName,
                                double FeatureValue) override;
  virtual bool setCustomFeature(std::string ModelName, std::string FeatureName,
                                float FeatureValue) override;
  virtual bool setCustomFeature(std::string ModelName, std::string FeatureName,
                                bool FeatureValue) override;

  // Replace all features with the given feature values.
  // Activate the specified model.
  virtual bool
  initializeFeatures(std::string ModelName,
                     const std::vector<std::pair<uint64_t, std::string>>
                         &FeatureValues) override;

  virtual bool
  initializeFeatures(std::string ModelName,
                     const std::vector<std::pair<std::string, std::string>>
                         &FeatureValues) override;

  // Set features with the specified feature values.
  // Not changing the currently active model.
  virtual bool
  setCustomFeatures(std::string ModelName,
                    const std::vector<std::pair<uint64_t, std::string>>
                        &FeatureValues) override;

  virtual bool
  setCustomFeatures(std::string ModelName,
                    const std::vector<std::pair<std::string, std::string>>
                        &FeatureValues) override;

  // Run a model with the specified name. Return false if the execution was not
  // possible or an error was encountered.
  virtual bool runModel(std::string ModelName) override;

  // Return the type of an output within the model specified by the name.
  virtual std::string getOutputType(std::string ModelName,
                                    std::string OutputName) override;

  // Return model results, based on the output name.
  virtual int getModelResultI(std::string OutputName) override;
  virtual int64_t getModelResultI64(std::string OutputName) override;
  virtual float getModelResultF(std::string OutputName) override;
  virtual double getModelResultD(std::string OutputName) override;
  virtual bool getModelResultB(std::string OutputName) override;

  // Get status of the ML interface. Return zero if succeeded.
  virtual int getStatus() override;

  // Free up memory taken by a model.
  virtual bool releaseModel(std::string ModelName) override;

  // Close interface when done. Return false if the command was not successful.
  // In some cases this just requires a constructor for this class to be called,
  // but in others, additional steps that require feedback may be needed.
  virtual bool closeMLInterface() override;

protected:
  void sendCommand(const std::string &Command);
  void sendCommand(const std::vector<std::string> &Features);
  std::string getResponse();
  std::vector<std::string> tokenize(const std::string &Line);

private:
  llvm::sys::ProcessInfo SubProcess;
  FILE *PipeOut = nullptr;
  FILE *PipeIn = nullptr;

  uint64_t NextID;

  std::string CurrentlyActiveModel;

  // Mapping model names to their corresponding Model
  std::unordered_map<std::string, std::shared_ptr<Model>> ModelMap;
};

std::shared_ptr<ACPOMLInterface> createPersistentPythonMLIF();

class ACPOMLCPPInterface : public ACPOMLInterface {
public:
  ACPOMLCPPInterface();
  virtual ~ACPOMLCPPInterface();

  // Interface methods.
  // Return the next available ID for a feature.
  virtual uint64_t assignID() override;

  // Load a model by reading from the specified file.
  // Return false if the operation failed.
  // For ACPOMLCompiledInterface, loadCompiledModel() should be used instead.
  virtual bool loadModel(std::string ModelSpecFile) override;

  // Insert a new model into the model map.
  virtual bool registerModel(std::string ModelName, int NumFeatures) override;
  virtual bool registerModel(std::string ModelName, int NumFeatures,
                             int NumOutputs) override;

  // Register a new feature for a given model.
  virtual bool registerFeature(std::string ModelName, std::string FeatureName,
                               int Index) override;

  // Register a new output for a given model.
  virtual bool registerOutput(std::string ModelName, std::string OutputName,
                              std::string OutputType) override;

  // Specify how many models are currently live in ML framework memory.
  virtual int getNumLoadedModels() override;

  // Specify the input file to use as IR to be passed to the model (however
  // it is processed afterwards). Return false if the operation failed.
  virtual bool defineInputIR(std::string Filename) override;

  // Specify a custom feature for a model to use as input at the next model
  // invocation. Return false if the operation failed.
  virtual bool setCustomFeature(std::string ModelName, uint64_t FeatureID,
                                int FeatureValue) override;
  virtual bool setCustomFeature(std::string ModelName, uint64_t FeatureID,
                                int64_t FeatureValue) override;
  virtual bool setCustomFeature(std::string ModelName, uint64_t FeatureID,
                                double FeatureValue) override;
  virtual bool setCustomFeature(std::string ModelName, uint64_t FeatureID,
                                float FeatureValue) override;
  virtual bool setCustomFeature(std::string ModelName, uint64_t FeatureID,
                                bool FeatureValue) override;

  virtual bool setCustomFeature(std::string ModelName, std::string FeatureName,
                                int FeatureValue) override;
  virtual bool setCustomFeature(std::string ModelName, std::string FeatureName,
                                int64_t FeatureValue) override;
  virtual bool setCustomFeature(std::string ModelName, std::string FeatureName,
                                double FeatureValue) override;
  virtual bool setCustomFeature(std::string ModelName, std::string FeatureName,
                                float FeatureValue) override;
  virtual bool setCustomFeature(std::string ModelName, std::string FeatureName,
                                bool FeatureValue) override;

  // Replace all features with the given feature values.
  // Activate the specified model.
  virtual bool
  initializeFeatures(std::string ModelName,
                     const std::vector<std::pair<uint64_t, std::string>>
                         &FeatureValues) override;

  virtual bool
  initializeFeatures(std::string ModelName,
                     const std::vector<std::pair<std::string, std::string>>
                         &FeatureValues) override;

  // Set features with the specified feature values.
  // Not changing the currently active model.
  virtual bool
  setCustomFeatures(std::string ModelName,
                    const std::vector<std::pair<uint64_t, std::string>>
                        &FeatureValues) override;

  virtual bool
  setCustomFeatures(std::string ModelName,
                    const std::vector<std::pair<std::string, std::string>>
                        &FeatureValues) override;

  // Run a model with the specified name. Return false if the execution was not
  // possible or an error was encountered.
  virtual bool runModel(std::string ModelName) override;

  // Return the type of an input within the model specified by the name.
  virtual std::string getInputType(std::string ModelName,
                                   std::string InputName);

  // Return the type of an output within the model specified by the name.
  virtual std::string getOutputType(std::string ModelName,
                                    std::string OutputName) override;

  // Return model results, based on the output name.
  virtual int getModelResultI(std::string OutputName) override;
  virtual int64_t getModelResultI64(std::string OutputName) override;
  virtual float getModelResultF(std::string OutputName) override;
  virtual double getModelResultD(std::string OutputName) override;
  virtual bool getModelResultB(std::string OutputName) override;

  // Get status of the ML interface. Return zero if succeeded.
  virtual int getStatus() override;

  // Free up memory taken by a model.
  virtual bool releaseModel(std::string ModelName) override;

  // Close interface when done. Return false if the command was not successful.
  // In some cases this just requires a constructor for this class to be called,
  // but in others, additional steps that require feedback may be needed.
  virtual bool closeMLInterface() override;

private:
  uint64_t NextID;

  std::string CurrentlyActiveModel;

  // Mapping model names to their corresponding Model
  std::unordered_map<std::string, std::shared_ptr<Model>> ModelMap;

  // Mapping model names to their corresponding Runner
  std::unordered_map<std::string, std::shared_ptr<ACPOModelRunner>> RunnerMap;

  std::string readModelParam(std::string FilePath, std::string Param);

  void readFeatures(std::string FilePath,
                    std::vector<std::pair<std::string, std::string>> &Features);
  void readOutputs(std::string FilePath,
                   std::vector<std::pair<std::string, std::string>> &Outputs);

  typedef std::unique_ptr<ACPOModelRunner> (*CreateModelRunnerFunction)(
      std::vector<std::pair<std::string, std::string>>,
      StringRef); // function pointer type
  const static std::unordered_map<std::string, CreateModelRunnerFunction>
      CreateModelRunnerMap;
};

std::shared_ptr<ACPOMLInterface> createPersistentCompiledMLIF();

} // namespace llvm

#endif // LLVM_ANALYSIS_ACPOML_INTERFACE_H
#endif // ENABLE_ACPO
