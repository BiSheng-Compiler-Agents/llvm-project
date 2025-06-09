#if defined(ENABLE_ACPO)

#ifndef LLVM_TRANSFORMS_ACPOAI4CMEMOPMODEL_H
#define LLVM_TRANSFORMS_ACPOAI4CMEMOPMODEL_H

#include "llvm/Analysis/ACPOBWModel.h"
#include "llvm/Analysis/DumpFeature.h"
#include "llvm/Analysis/FunctionPropertiesAnalysis.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/InlineSizeEstimatorAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include <map>

namespace llvm {
class ACPOAI4CMEMOPModel : public ACPOModel {
public:
  ACPOAI4CMEMOPModel(LLVMContext *Context, OptimizationRemarkEmitter *ORE);
  ~ACPOAI4CMEMOPModel();
  void setMLCustomFeatures(std::vector<std::pair<std::string, std::string>> FeatureValues);
  static void clearCache();
protected:
  // Interface to run the MLInference/default advisor and get advice from the
  // model/default advisor
  virtual std::unique_ptr<ACPOAdvice> getAdviceML() override;
  virtual std::unique_ptr<ACPOAdvice> getAdviceNoML() override;
private:
  std::vector<std::pair<std::string, std::string>> CustomFeatureValues;
  int64_t ShouldOPT = 0;
};
} // end namespace llvm
#endif
#endif // Enable_ACPO