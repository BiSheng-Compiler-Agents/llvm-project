#if defined(ENABLE_ACPO)

#include "llvm/Transforms/Instrumentation/ACPOAI4CFHModel.h"

using namespace llvm;

#define DEBUG_TYPE "acpo-ai4c-fh";

ACPOAI4CFHModel::ACPOAI4CFHModel(LLVMContext *Context, OptimizationRemarkEmitter *ORE) : ACPOModel(ORE, true) {
  setContextPtr(Context);
  setMLIF(createPersistentCompiledMLIF());
}

ACPOAI4CFHModel::~ACPOAI4CFHModel() {}

void ACPOAI4CFHModel::setMLCustomFeatures(std::vector<std::pair<std::string, std::string>> FeatureValues) {
  CustomFeatureValues = FeatureValues;
}

std::unique_ptr<ACPOAdvice> ACPOAI4CFHModel::getAdviceML() {
  std::shared_ptr<ACPOMLInterface> MLIF =  getMLIF();
  // Generate result.
  std::unique_ptr<ACPOAdvice> Advice = std::make_unique<ACPOAdvice>();

  if (!MLIF->loadModel("model-ai4cfh.acpo") || !MLIF->initializeFeatures("AI4CFH", CustomFeatureValues)) {
    outs() << "Model not loaded or features not initialized."
           << "Did you export BISHENG_ACPO_DIR to $LLVM_DIR/acpo ?\n"
           << "Falling back to default advisor. \n";
    return nullptr;
  }
  bool ModelRunOK = MLIF->runModel("AI4CFH");
  Hotness = MLIF->getModelResultI("FH");
  Advice->addField("FH", ConstantInt::get(Type::getInt64Ty(*(getContextPtr())), (int64_t)Hotness));

  return Advice;
}

std::unique_ptr<ACPOAdvice> ACPOAI4CFHModel::getAdviceNoML() {return nullptr;}
#endif // Enable_ACPP

