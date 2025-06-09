#if defined(ENABLE_ACPO)
#ifndef LLVM_TRANSFORMS_ACPOAI4CFHMODEL_H
#define LLVM_TRANSFORMS_ACPOAI4CFHMODEL_H

#include "llvm/Analysis/ACPOBWModel.h"
#include "llvm/Analysis/DumpFeature.h"
#include "llvm/Analysis/FunctionPropertiesAnalysis.h"
#include "llvm/Analysis/InlineSizeEstimatorAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include <map>

namespace llvm {
class ACPOAI4CFHModel : public ACPOModel {
public:
  ACPOAI4CFHModel(LLVMContext *Context, OptimizationRemarkEmitter *ORE);
  ~ACPOAI4CFHModel();
  void setMLCustomFeatures(std::vector<std::pair<std::string, std::string>> FeatureValues);
  static void clearCache();
protected:
  // Interface to run the MLInterface/default advisor and get advice from the
  // odel/default advisor
  virtual std::unique_ptr<ACPOAdvice> getAdviceML() override;
  virtual std::unique_ptr<ACPOAdvice> getAdviceNoML() override;
private:
  std::vector<std::pair<std::string, std::string>> CustomFeatureValues;
  int64_t Hotness = 0;
};
} // end namespace llvm

#endif
#endif //Enable_ACPO
