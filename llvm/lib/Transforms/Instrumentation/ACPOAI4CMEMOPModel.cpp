#if defined(ENABLE_ACPO)

#include "llvm/Transforms/Instrumentation/ACPOAI4CMEMOPModel.h"

using namespace llvm;

#define DEBUG_TYPE "acpo-ai4c-memop"

ACPOAI4CMEMOPModel::ACPOAI4CMEMOPModel(LLVMContext *Context, OptimizationRemarkEmitter *ORE) : ACPOModel(ORE, true) {
  setContextPtr(Context);
  setMLIF(createPersistentCompiledMLIF());
}

ACPOAI4CMEMOPModel::~ACPOAI4CMEMOPModel() {}

void ACPOAI4CMEMOPModel::setMLCustomFeatures(
    std::vector<std::pair<std::string, std::string>> FeatureValues) {
  CustomFeatureValues = FeatureValues;
}

std::unique_ptr<ACPOAdvice> ACPOAI4CMEMOPModel::getAdviceML() {
  std::shared_ptr<ACPOMLInterface> MLIF = getMLIF();
  // Generate result.
  std::unique_ptr<ACPOAdvice> Advice = std::make_unique<ACPOAdvice>();

  if (!MLIF->loadModel("model-ai4cmemop.acpo") || !MLIF->initializeFeatures("AI4CMEMOP", CustomFeatureValues)) {
    outs() << "Model not loaded or features not initialized."
           << "Did you export BISHENG_ACPO_DIR to $LLVM_DIR/acpo ?\n"
           << "Falling back to default advisor. \n";
    return nullptr;
  }
  bool ModelRunOK = MLIF->runModel("AI4CMEMOP");
  ShouldOPT = MLIF->getModelResultI("OPT");
  Advice->addField("OPT", ConstantInt::get(Type::getInt64Ty(*(getContextPtr())), (int64_t)ShouldOPT));

  return Advice;
}

std::unique_ptr<ACPOAdvice> ACPOAI4CMEMOPModel::getAdviceNoML() {
  return nullptr;
}
#endif //Enable_ACPO