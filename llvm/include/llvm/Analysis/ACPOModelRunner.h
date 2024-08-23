//===- ACPOModelRunner.h - AI-Enabled Continuous Program Optimization -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(ENABLE_ACPO)
#ifndef LLVM_ANALYSIS_ACPOMODEL_H
#define LLVM_ANALYSIS_ACPOMODEL_H

#include "llvm/Analysis/MLModelRunner.h"

namespace llvm {

class ACPOModelRunner : public MLModelRunner {
public:
  virtual bool setCustomFeature(int FeatureIndex, int FeatureValue) = 0;
  virtual bool setCustomFeature(int FeatureIndex, int64_t FeatureValue) = 0;
  virtual bool setCustomFeature(int FeatureIndex, double FeatureValue) = 0;
  virtual bool setCustomFeature(int FeatureIndex, float FeatureValue) = 0;
  virtual bool setCustomFeature(int FeatureIndex, bool FeatureValue) = 0;

  virtual bool runModel() = 0;

  virtual int getModelResultI(std::string OutputName) = 0;
  virtual int64_t getModelResultI64(std::string OutputName) = 0;
  virtual float getModelResultF(std::string OutputName) = 0;
  virtual double getModelResultD(std::string OutputName) = 0;
  virtual bool getModelResultB(std::string OutputName) = 0;

protected:
  ACPOModelRunner(LLVMContext &Ctx, size_t NrInputs)
      : MLModelRunner(Ctx, MLModelRunner::Kind::Release, NrInputs) {}
};

} // namespace llvm

#endif // LLVM_ANALYSIS_ACPOMODEL_H
#endif // ENABLE_ACPO
