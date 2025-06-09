#if defined(ENABLE_ACPO)

#ifdef LLVM_HAVE_TF_AOT_AI4CMEMOPCOMPILEDMODEL

#ifndef LLVM_ANALYSIS_AI4CMEMOPMODELRUNNER_H
#define LLVM_ANALYSIS_AI4CMEMOPMODELRUNNER_H

#include "llvm/Analysis/AI4CMEMOPCompiledModel.h"
#include "llvm/Analysis/AOTModelRunner.h"

namespace llvm {
class AI4CMEMOPModelRunner : public AOTModelRunner<AI4CMEMOPCompiledModel> {
  std::vector<float> Means = {5.902094114865505,      1.9211618257261411,
                              11.605809128630705,     0.0,
                              0.17012448132780084,    29.585062240663902,
                              66.91701244813278,      314.3526970954357,
                              0.5311203319502075,     4.153526970954357,
                              5.242383737287086,      1.5767634854771784,
                              71.06639004149378,      0.0,
                              66.86721991701245,      23.286307053941908,
                              9.244813278008298,      27.502074688796682,
                              1.3941908713692945,     0.0,
                              3.211618257261411,      0.024896265560165973,
                              12.556016597510373,     1.4688796680497926,
                              2.2994485024434755e+17, 1.5308973833539498e+17,
                              1.484186989637826,      0.0,
                              0.5311203319502075,     61.61799606544843,
                              0.5269709543568465,     0.29045643153526973,
                              0.04564315352697095,    0.0,
                              1.8506224066390042,     1.4190871369294606,
                              0.946058091286307,      1.004149377593361,
                              4.394190871369295,      0.17842323651452283,
                              12.348547717842324,     1.0788381742738589,
                              1.9170124481327802,     1.0290456431535269,
                              1.7178423236514522,     0.9336099585062241,
                              10.946058091286307};
  std::vector<float> Scales = {10.320994966903598,     1.0769213741084371,
                               12.785422175115425,     1.0,
                               0.37574212191441586,    89.78282941276281,
                               103.94904473562026,     596.798404979162,
                               0.4990305851742043,     6.4090572264124965,
                               2.6100173708575176,     3.088480723013504,
                               109.24510752282497,     1.0,
                               103.95354354673327,     67.94906698858647,
                               20.696839217519198,     49.92194109972631,
                               2.1476145863960108,     1.0,
                               6.5267427741688,        0.155808990502229,
                               49.316216255834725,     4.396347313306036,
                               2.0452401653902395e+18, 1.6734645469172273e+18,
                               0.2550799453207434,     1.0,
                               0.4990305851742043,     116.57277158707657,
                               0.49927203769195905,    0.45397300901602894,
                               0.20870998074621255,    1.0,
                               1.533339596437842,      1.1642535392303865,
                               1.4494221987807236,     1.5093015210423568,
                               3.53497748777652,       0.5664470355711709,
                               9.105814564701344,      0.26948602292331114,
                               2.2886516432701987,     1.3917311833516468,
                               2.0661400638678655,     1.5738234218680085,
                               53.640199004027394};

public:
  AI4CMEMOPModelRunner(
      LLVMContext &Ctx,
      std::vector<std::pair<std::string, std::string>> Features,
      StringRef DecisionName)
      : AOTModelRunner<AI4CMEMOPCompiledModel>(
            Ctx,
            {{"input_1", "float32[" + std::to_string(Features.size()) + "]"}},
            DecisionName) {}
  bool setCustomFeature(int FeatureIndex, float FeatureValue) override {
    float ScaledValue =
        (FeatureValue - Means[FeatureIndex]) / Scales[FeatureIndex];
    // Assuming the Buffer at index 0 is for feature input of shape:
    // (Feature.size())
    float *Location = getTensor<float>(0) + FeatureIndex;
    *Location = ScaledValue;
    return true;
  }
  int getModelResultI(std::string OutputName) override {
    if (OutputName == "OPT") {
      int Classes[] = {0, 1};
      void *ResultUntyped = CompiledModel->result_data(0);
      float *Result = reinterpret_cast<float *>(ResultUntyped);
      float Max = Result[0];
      int MaxClass = 0;
      for (size_t I = 0; I < sizeof(Classes) / sizeof(int); ++I) {
        if (Result[I] > Max) {
          Max = Result[I];
          MaxClass = I;
        }
      }

      return Classes[MaxClass];
    }
    assert(false && "ModelRunner received invalid result name");
  }
};
} // namespace llvm

#endif // LLVM_ANALYSIS_AI4CMEMOPMODELRUNNER_H
#endif // LLVM_HAVE_TF_AOT_AI4CMEMOPCOMPILEDMODEL
#endif //Enable_ACPO