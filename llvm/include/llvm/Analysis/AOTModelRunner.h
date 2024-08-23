//===- AOTModelRunner.h - AI-Enabled Continuous Program Optimization ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(ENABLE_ACPO)
#ifndef LLVM_ANALYSIS_AOTMODEL_H
#define LLVM_ANALYSIS_AOTMODEL_H

#include "llvm/Analysis/ACPOModelRunner.h"
#include "llvm/Analysis/TensorSpec.h"

#define DEBUG_TYPE "acpo-aot"

namespace llvm {

template <class TGen> class AOTModelRunner : public ACPOModelRunner {
public:
  /// FeatureNames' type should be an indexed collection of std::string, like
  /// std::array or std::vector, that has a size() method.
  /// In the future, this method could be expanded to allow AOT models with
  /// multiple outputs, by taking in a vector of string pairs similar to the
  /// Features vector.
  /// The current implementation does work for AOT models with a single output
  /// which is a vector (or higher-dimensional tensor) of multiple values.
  AOTModelRunner(
      LLVMContext &Ctx,
      const std::vector<std::pair<std::string, std::string>> &Features,
      StringRef DecisionName, StringRef FeedPrefix = "feed_",
      StringRef FetchPrefix = "fetch_")
      : ACPOModelRunner(Ctx, Features.size()),
        CompiledModel(std::make_unique<TGen>()) {
    assert(CompiledModel && "The CompiledModel should be valid");

    for (size_t I = 0; I < Features.size(); ++I) {
      const int Index =
          CompiledModel->LookupArgIndex(FeedPrefix.str() + Features[I].first);
      void *Buffer = nullptr;
      if (Index >= 0) {
        Buffer = CompiledModel->arg_data(Index);
      } else {
        LLVM_DEBUG(dbgs() << "Warning: AOTModelRunner was unable to find the "
                             "feature "
                          << (FeedPrefix.str() + Features[I].first)
                          << " in the compiled model\n");
      }
      // The order of features passed to the model runner is important, it
      // determines their index
      TensorSpec Spec = makeSpec(Features[I].first, Features[I].second);
      setUpBufferForTensor(I, Spec, Buffer);
    }

    ResultIndex = CompiledModel->LookupResultIndex(FetchPrefix.str() +
                                                   DecisionName.str());
    assert(ResultIndex >= 0 && "Cannot find DecisionName in inlining model");
  }

  virtual ~AOTModelRunner() = default;

  static bool classof(const ACPOModelRunner *R) {
    return R->getKind() == ACPOModelRunner::Kind::Release;
  }

  bool setCustomFeature(int FeatureIndex, int FeatureValue) override {
    LLVM_DEBUG(dbgs() << "AOTModelRunner: setting int feature " << FeatureIndex
                      << " to " << FeatureValue << "\n");
    *getTensor<int>(FeatureIndex) = FeatureValue;
    return true;
  }
  bool setCustomFeature(int FeatureIndex, int64_t FeatureValue) override {
    LLVM_DEBUG(dbgs() << "AOTModelRunner: setting int64 feature "
                      << FeatureIndex << " to " << FeatureValue << "\n");
    *getTensor<int64_t>(FeatureIndex) = FeatureValue;
    return true;
  }
  bool setCustomFeature(int FeatureIndex, double FeatureValue) override {
    LLVM_DEBUG(dbgs() << "AOTModelRunner: setting double feature "
                      << FeatureIndex << " to " << FeatureValue << "\n");
    *getTensor<double>(FeatureIndex) = FeatureValue;
    return true;
  }
  bool setCustomFeature(int FeatureIndex, float FeatureValue) override {
    LLVM_DEBUG(dbgs() << "AOTModelRunner: setting float feature "
                      << FeatureIndex << " to " << FeatureValue << "\n");
    *getTensor<float>(FeatureIndex) = FeatureValue;
    return true;
  }
  bool setCustomFeature(int FeatureIndex, bool FeatureValue) override {
    // There are no bool tensors, so assume int for now
    LLVM_DEBUG(dbgs() << "AOTModelRunner: setting bool feature " << FeatureIndex
                      << " to " << FeatureValue << "\n");
    *getTensor<int>(FeatureIndex) = FeatureValue;
    return true;
  }

  bool runModel() override {
    CompiledModel->Run();
    return true;
  }

  int getModelResultI(std::string OutputName) override {
    void *Data = CompiledModel->result_data(ResultIndex);
    int Result = *reinterpret_cast<int *>(Data);
    LLVM_DEBUG(dbgs() << "Returning int model result " << OutputName << " = "
                      << Result << "\n");
    return Result;
  }

  int64_t getModelResultI64(std::string OutputName) override {
    void *Data = CompiledModel->result_data(ResultIndex);
    int64_t Result = *reinterpret_cast<int64_t *>(Data);
    LLVM_DEBUG(dbgs() << "Returning int64 model result " << OutputName << " = "
                      << Result << "\n");
    return Result;
  }

  float getModelResultF(std::string OutputName) override {
    void *Data = CompiledModel->result_data(ResultIndex);
    float Result = *reinterpret_cast<float *>(Data);
    LLVM_DEBUG(dbgs() << "Returning float model result " << OutputName << " = "
                      << Result << "\n");
    return Result;
  }

  double getModelResultD(std::string OutputName) override {
    void *Data = CompiledModel->result_data(ResultIndex);
    double Result = *reinterpret_cast<double *>(Data);
    LLVM_DEBUG(dbgs() << "Returning double model result " << OutputName << " = "
                      << Result << "\n");
    return Result;
  }

  bool getModelResultB(std::string OutputName) override {
    // Since there are no bool tensors, use int and return the corresponding
    // result
    void *Data = CompiledModel->result_data(ResultIndex);
    bool Result = (*reinterpret_cast<int *>(Data)) > 0;
    LLVM_DEBUG(dbgs() << "Returning bool model result " << OutputName << " = "
                      << Result << "\n");
    return Result;
  }

protected:
  std::unique_ptr<TGen> CompiledModel;

private:
  void *evaluateUntyped() override {
    CompiledModel->Run();
    return CompiledModel->result_data(ResultIndex);
  }

  llvm::TensorSpec makeSpec(std::string Name, std::string Type) {
    std::vector<int64_t> Shape{};
    // If the string is of the form "float32[7][8]", read the value in brackets
    // as the shape (read from left to right)
    size_t RightBracket = 0;
    size_t LeftBracket = 0;
    do {
      LeftBracket = Type.find("[", RightBracket + 1);
      if (LeftBracket == std::string::npos) {
        break;
      }
      RightBracket = Type.find("]", LeftBracket + 1);
      size_t Value = std::stol(
          Type.substr(LeftBracket + 1, RightBracket - LeftBracket - 1));
      Shape.push_back(Value);
    } while (RightBracket != std::string::npos);

    // Remove array indices to just get type
    if (Type.find("[") != std::string::npos) {
      Type = Type.substr(0, Type.find("["));
    }

    if (Shape.size() == 0)
      Shape.push_back(1); // Default shape is {1}

    if (Type == "int64") {
      return TensorSpec::createSpec<int64_t>(Name, Shape);
    }
    if (Type == "int32") {
      return TensorSpec::createSpec<int32_t>(Name, Shape);
    }
    if (Type == "int" || Type == "bool") {
      // There are no bool tensors, so assume int for now
      return TensorSpec::createSpec<int>(Name, Shape);
    }
    if (Type == "float64") {
      return TensorSpec::createSpec<double>(Name, Shape);
    }
    if (Type == "float32") {
      return TensorSpec::createSpec<float>(Name, Shape);
    }
    assert(false && "ACPO AOT: received unknown feature type");
  }

  int32_t ResultIndex = -1;
};

} // namespace llvm

#endif // LLVM_ANALYSIS_AOTMODEL_H
#endif // ENABLE_ACPO
