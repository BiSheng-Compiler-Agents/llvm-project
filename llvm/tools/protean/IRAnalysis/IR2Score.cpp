//===- IR2ScoreModel.cpp - Protean Compiler                      //-------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2024. Huawei Technologies Co., Ltd. All rights reserved.
//
//===----------------------------------------------------------------------===//
//
// This file implements the interface between ACPO and ML-guided optimizations.
// It delegates decision making to inference with a pre-trained model.
//
//===----------------------------------------------------------------------===//

#include "IR2Score.h"
#include "llvm/IR/LLVMContext.h"

using namespace llvm;

#define DEBUG_TYPE "ir2score"

IR2ScoreModel::IR2ScoreModel(LLVMContext *Context, bool UseAOT,
                             OptimizationRemarkEmitter *ORE, bool UseML)
    : ACPOModel(ORE, UseML) {
  setContextPtr(Context);
  if (UseAOT) {
    setMLIF(createPersistentCompiledMLIF());
  } else {
    setMLIF(createPersistentPythonMLIF());
  }
  UseAOTModel = UseAOT;
  std::shared_ptr<ACPOMLInterface> MLIF = getMLIF();
  MLIF->setSimulatedAnnealing(true);
}

IR2ScoreModel::~IR2ScoreModel() {}
void IR2ScoreModel::setMLCustomFeatures(
    std::vector<std::pair<std::string, std::string>> FeatureValues) {
  CustomFeatureValues = FeatureValues;
}

void IR2ScoreModel::setContext(LLVMContext *Context) { setContextPtr(Context); }

void IR2ScoreModel::sendCustomFeatures() {
  // Get an ACPOMLInterface to communicate with the Python side
  std::shared_ptr<ACPOMLInterface> MLIF = getMLIF();
  MLIF->initializeFeatures("IR2SCORE", CustomFeatureValues);
}

void IR2ScoreModel::setProteanCollect(bool ProteanCollect) {
  UseProteanCollect = ProteanCollect;
}

size_t IR2ScoreModel::getModelFeaturesSize() {
  std::string ModelFile;
  if (UseProteanCollect) {
    ModelFile = "model-ir2scoreprotean.acpo";
  } else {
    ModelFile = "model-ir2scoreir2vec.acpo";
  }

  std::shared_ptr<llvm::ACPOMLCPPInterface> CppInterface =
      std::make_shared<llvm::ACPOMLCPPInterface>();
  return CppInterface->getModelFeaturesSize(ModelFile);
}

std::unique_ptr<ACPOAdvice> IR2ScoreModel::getAdviceML() {
  // get performance results from module metadata
  // Get module flags
  std::shared_ptr<ACPOMLInterface> MLIF = getMLIF();
  std::unique_ptr<ACPOAdvice> Score = std::make_unique<ACPOAdvice>();
  assert(MLIF != nullptr);

  std::string ModelFile;
  std::string OutputName;
  if (UseProteanCollect) {
    ModelFile = "model-ir2scoreprotean.acpo";
    OutputName = "IRSCOREPROTEAN";
  } else {
    ModelFile = "model-ir2scoreir2vec.acpo";
    OutputName = "IRSCORE";
  }
  LLVM_DEBUG(llvm::dbgs() << "Loading model from IR2ScoreModel..\n");
  if (!MLIF->loadModel(ModelFile)) {
    LLVM_DEBUG(llvm::dbgs()
               << "Model not loaded. "
               << "Did you export BISHENG_ACPO_DIR to $LLVM_DIR/acpo ?\n"
               << "Falling back to default advisor. \n");
    return NULL;
  }
  LLVM_DEBUG(llvm::dbgs() << "Loading features for IR2ScoreModel..\n");
  if (!MLIF->initializeFeatures("IR2SCORE", CustomFeatureValues)) {
    LLVM_DEBUG(llvm::dbgs()
               << "Features not initialized. "
               << "Did you export BISHENG_ACPO_DIR to $LLVM_DIR/acpo ?\n"
               << "Falling back to default advisor. \n");
    return NULL;
  }
  bool ModelRunOK = MLIF->runModel("IR2SCORE");
  assert(ModelRunOK);
  float IRScore = MLIF->getModelResultF(OutputName);
  LLVM_DEBUG(llvm::dbgs() << "Found IRScore: " << IRScore << '\n');
  assert(getContextPtr() != nullptr);
  Score->addField(
      "IRSCORE",
      ConstantFP::get(Type::getFloatTy(*(getContextPtr())), (float)IRScore));
  return Score;
}

std::unique_ptr<ACPOAdvice> IR2ScoreModel::getAdviceNoML() {
  // Use the advisor used by default inlining
  return NULL;
}
