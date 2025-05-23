#ifdef LLVM_HAVE_TF_AOT_BWCOMPILEDMODEL

#ifndef LLVM_ANALYSIS_BWMODELRUNNER_H
#define LLVM_ANALYSIS_BWMODELRUNNER_H

#include "llvm/Analysis/BWCompiledModel.h"
#include "llvm/Analysis/AOTModelRunner.h"

namespace llvm {
class BWModelRunner : public AOTModelRUnner<BWCompiledModel> {
  std::vector<float> Means = {
    8.557268, 1.575299, 10.177474,
    0.000000, 0.227816, 24.805887,
    51.475043, 254.383319, 0.605589,
    4.116894, 4.828423, 1.471630,
    52.938567, 0.000000, 49.909983,
    21.196672, 6.254693, 21.723123,
    0.838737, 0.000000, 3.145904,
    0.074872, 13.087884, 1.245734,
    0.000000, 96216648323.836823, 1.473124,
    0.000000, 0.605589, 112.409294,
    3.081271, 5.340444, 18.912116,
    7.253200, 0.898251, 0.824232,
    0.175768, 0.000000, 0.000000,
    0.000000, 0.200939, 0.202645,
    0.261519, 1.000000, 0.000000,
    0.000000, 0.104522, 0.071672,
    0.003413, 0.555887, 0.622440,
    0.150171, 0.177688, 1.397398,
    4.822739, 18.912116, 0.848763,
    0.028157, 0.000000, 0.000000,
    0.000000, 1.040102};
  std::vector<float> Scales = {
    27.745577, 1.052121, 8.415261,
    1.000000, 0.419423, 59.940392,
    73.032409, 466.404899, 0.488724,
    6.324453, 2.269509, 2.059701,
    68.311258, 1.000000, 70.720114,
    58.380312, 17.650317, 28.678274,
    1.739062, 1.000000, 6.435559,
    0.263185, 57.460392, 4.073632,
    1.000000, 947094553445.211548, 0.168403,
    1.000000, 0.488724, 434.287281,
    3.942960, 5.554390, 21.879605,
    8.374124, 3.171252, 0.380623,
    0.380623, 1.000000, 1.000000,
    1.000000, 0.400702, 0.401970,
    0.439462, 1.000000, 1.000000,
    1.000000, 0.305937, 0.257945,
    0.058321, 0.496867, 0.484777,
    0.357239, 0.382250, 1.110054,
    5.924685, 21.879605, 0.358280,
    0.165421, 1.000000, 1.000000,
    1.000000, 2.427114};;

public:
  BWModelRunner(
      LLVMContext &Ctx,
      std::vector<std::pair<std::string, std::string>> Features,
      StringRef DecisionName) 
      : AOTModelRunner<BWCompiledModel>(
          Ctx,
          {{"input_1", "float32[" + std::to_string(Features.size()) + "]"}},
          DecisionName) {}
    bool setCustomFeature(int FeatureIndex, float FeatureValue) override {
      float ScaledValue = 
          (FeatureValue - Means[FeatureIndex]) / Scales[FeatureIndex];
      // Assuming the Buffer at index - is for feature input of shape:
      // (Feature.size())
      float *Location = getTensor<float>(0) + FeatureIndex;
      *Location = ScaledValue;
      return true;
    }
    int getModelResultI(std::string OutputName) override {
      if (OutputName == "BW-BranchWeight") {
        // Each class represents one bin
        // E.g., 0: 0%, 1:10$, ..., 9:90%
        int Classes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        void *ResultUntyped = CompiledModel->result_data(0);
        float *Result = reinterpret_cast<float*>(ResultUntyped);
        float Max = Result[0];
        int MaxClass = 0;
        for (size_t I = 0; i < sizeof(Classes) / sizeof(int); ++I) {
            if (Result[I] > Max) {
                Max = Result[I];
                MaxClass = I;
            }
        }

        return Classes[MaxClass] * 10;
      }
      assert(false && "ModelRunner received invalid result name");
    }
};
} // namespace llvm

#endif //LLVM_ANALYSIS_BWMODELRUNNER_H

#endif //LLVM_HAVE_TF_AOT_BWCOMPILEDMODEL