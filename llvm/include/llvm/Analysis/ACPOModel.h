//===- ACPOModel.h - AI-Enabled Continuous Program Optimization -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(ENABLE_ACPO)
#ifndef LLVM_ANALYSIS_ACPOMODEL_H
#define LLVM_ANALYSIS_ACPOMODEL_H

#include "llvm/Analysis/ACPOMLInterface.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Type.h"
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>

namespace llvm {

class OptimizationRemarkEmitter;
class LLVMContext;

class ACPOAdvice {
public:
  struct FieldType {
    Type::TypeID T;
    Constant *Val;
  };

  ACPOAdvice() {}
  ACPOAdvice(std::unique_ptr<ACPOAdvice> &ResultFormat);
  virtual ~ACPOAdvice() {};

  Constant *getField(std::string name) {
    auto Search = FieldMap.find(name);
    if (Search == FieldMap.end()) {
      return nullptr;
    }
    return Search->second.Val;
  }

  void reserveField(std::string name, Type::TypeID &ID) {
    FieldMap[name].T = ID;
    FieldMap[name].Val = nullptr;
  }

  void addField(std::string name, Constant *Val) {
    assert(Val != nullptr);
    FieldMap[name].T = Val->getType()->getTypeID();
    FieldMap[name].Val = Val;
  }

  std::unordered_map<std::string, struct FieldType> &getFieldMap() {
    return FieldMap;
  }

private:
  std::unordered_map<std::string, struct FieldType> FieldMap;
};

class ACPOModel {
public:
  ACPOModel(OptimizationRemarkEmitter *OptReEm, bool UseML = true) :
      ORE(OptReEm), ShouldUseML(UseML) {
    ResultFormat = std::make_unique<ACPOAdvice>();
    assert(ResultFormat != nullptr);
  }

  ~ACPOModel() {}

  bool isForcedToStop() const { return ForceStop; }

  // This is a interface method to return result of estimation either via an ML
  // model or by employing a heuristic. The ML version should be implemented in
  // the getAdviceML, which can be overwritten when necessary. The non-ML
  // version should be implemented in getAdviceNoML and that should always be
  // overwritten (and it will be marked as pure (=0) to force the programmer
  // to do so).
  std::unique_ptr<ACPOAdvice> getAdvice();
  void addRequiredResultField(std::string name, Type::TypeID &ID);

  void setContextPtr(LLVMContext *C) { Context = C; }
  LLVMContext *getContextPtr() { return Context; }

  void setMLIF(std::shared_ptr<ACPOMLInterface> ML) { MLIF = ML; }
  std::shared_ptr<ACPOMLInterface> getMLIF() { return MLIF; }

protected:
  void addFeature(int64_t ID, Constant *Val);
  virtual void sendCustomFeatures() {}
  virtual void prepareModelInput();
  virtual bool runModel(std::unique_ptr<ACPOAdvice> &Result);

  virtual std::unique_ptr<ACPOAdvice> getAdviceML();
  virtual std::unique_ptr<ACPOAdvice> getAdviceNoML() = 0;

private:
  // Pointer to means of feedback propagation
  OptimizationRemarkEmitter *ORE;

  // We may need LLVMContext to set values of a Constant
  LLVMContext *Context = nullptr;

  // Specify expected format of the ACPOAdvice result.
  std::unique_ptr<ACPOAdvice> ResultFormat = nullptr;

  // Custom feature list.
  std::unordered_map<uint64_t, Constant *> CustomFeatureMap;

  // Interface to ML framework.
  std::shared_ptr<ACPOMLInterface> MLIF = nullptr;

  // Specify if ML infra is in use
  bool ShouldUseML = false;
  bool ForceStop = false;
};

} // namespace llvm

#endif // LLVM_ANALYSIS_ACPOMODEL_H
#endif // ENABLE_ACPO
