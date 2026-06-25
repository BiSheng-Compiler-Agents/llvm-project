//===- SimulatedAnnealing.cpp - The LLVM Modular Optimizer ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2024, Huawei Technologies Co., Ltd. All rights reserved.
//
//===----------------------------------------------------------------------===//
//
// Implements the simulated annealing algorithm for the Metamorphic Code
// Optimizer
//
// Requirements:
// export BISHENG_ACPO_DIR=$LLVM_DIR/acpo
// export IR2VEC_PATH=path/to/ir2vec
//===----------------------------------------------------------------------===//

#include "SimulatedAnnealing.h"
#include "IR2Score.h"
#include "PhaseOrder.h"
#include "llvm/Analysis/ModelDataCollector.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR2Vec/IR2Vec.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <llvm/IR/LLVMContext.h>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#undef DEBUG_TYPE
#define DEBUG_TYPE "protean"
#define BISHENG_INSTALL_DIR "BISHENG_INSTALL_DIR"

struct SharedMemoryData {
  size_t bitcode_size;
  bool ModLevelIPC;
  size_t ir2vec_dimension;
  size_t featureCount;
  size_t dataSize;
  size_t dataOffset;
};

void SimulatedAnnealingProtean::generatePermutationsWithRepetitions(
    std::string &Recipes, std::string &Current, int MaxSeqLength) {
  AllRecipesSet.insert(Current);
  if (Current.size() == MaxSeqLength) {
    return;
  }

  for (auto &R : Recipes) {
    Current.push_back(R);
    generatePermutationsWithRepetitions(Recipes, Current, MaxSeqLength);
    Current.pop_back();
  }
}
namespace llvm {
class ModelDataScoreCollector : public ModelDataCollector {
public:
  ModelDataScoreCollector(formatted_raw_ostream &OS, std::string OutputFileName)
      : ModelDataCollector(OS, OutputFileName) {}

  void collectFeatures(std::unique_ptr<Module> &M) {
    std::vector<std::string> keys;
    keys.push_back("proteanFeatureHeaders");
    keys.push_back("proteanFeatureValues");
    std::vector<std::string> headers;
    std::vector<std::string> values;
    for (auto key : keys) {
      // Get the module flag
      llvm::Metadata *flag = M->getModuleFlag(key);
      // Check if the module flag exists and is an array
      if (llvm::MDNode *arrayNode =
              llvm::dyn_cast_or_null<llvm::MDNode>(flag)) {
        // Iterate over array elements
        for (unsigned i = 0, e = arrayNode->getNumOperands(); i < e; ++i) {
          llvm::Metadata *element = arrayNode->getOperand(i);
          if (llvm::MDString *mdString =
                  llvm::dyn_cast<llvm::MDString>(element)) {
            llvm::StringRef stringValue = mdString->getString();
            if (key == "proteanFeatureHeaders") {
              headers.push_back(stringValue.str());
            } else {
              values.push_back(stringValue.str());
            }
          }
        }
      } else {
        llvm::outs() << "Module flag with key '" << key
                     << "' not found or not an array.\n";
      }
    }
    for (int i = 0; i < headers.size(); i++) {
      Features.insert(Features.end(), {std::make_pair(headers[i], values[i])});
      LLVM_DEBUG(llvm::dbgs() << "{" << headers[i] << " " << values[i] << "}");
    }
    LLVM_DEBUG(llvm::dbgs() << "\n");
  }
};
} // namespace llvm

std::vector<std::string> splitByChar(const std::string &Input, char SplitChar) {
  std::vector<std::string> Res;
  std::stringstream StrStream = std::stringstream(Input);
  std::string Line;
  while (std::getline(StrStream, Line, SplitChar)) {
    Res.push_back(Line);
  }
  return Res;
}

SimulatedAnnealingProtean::SimulatedAnnealingProtean(
    int RngVal, CoolingType CoolingSchedule, double MaxTemperature,
    double MinTemperature, unsigned int MaxIterations, IRCostFunction CostType,
    std::string OutputFilename, bool ProteanOutputTable, bool UseProteanCollect,
    bool ModLevelIPC, bool UseAOTModel, unsigned int InitialSampleSize,
    double MutationRate, double CrossoverRate, unsigned int PopulationSize,
    CrossoverFunction CrossoverType, MutationFunction MutationType)
    : Generator{new PhaseOrderGeneratorBase(InitialSampleSize, PopulationSize,
                                            MutationRate, CrossoverRate,
                                            CrossoverType, MutationType)},
      RngVal(RngVal), CoolingSchedule(CoolingSchedule), CostType(CostType),
      OutputFilename(OutputFilename), MaxTemperature(MaxTemperature),
      MinTemperature(MinTemperature), MaxIterations(MaxIterations),
      ProteanOutputTable(ProteanOutputTable),
      UseProteanCollect(UseProteanCollect), ModLevelIPC(ModLevelIPC),
      UseAOTModel(UseAOTModel), InitialSampleSize(InitialSampleSize),
      MutationRate(MutationRate), CrossoverRate(CrossoverRate),
      PopulationSize(PopulationSize), CrossoverType(CrossoverType),
      MutationType(MutationType) {
  // Setting up the recipe string to 5
  std::string RecipeStr = "01234";
  std::string Current;
  generatePermutationsWithRepetitions(RecipeStr, Current, RecipeStr.size());
  AllRecipes =
      std::vector<std::string>(AllRecipesSet.begin(), AllRecipesSet.end());
  RandomEngine = std::default_random_engine{static_cast<unsigned long>(RngVal)};
  std::shuffle(std::begin(AllRecipes), std::end(AllRecipes), RandomEngine);
  LLVM_DEBUG(llvm::dbgs() << "Recipe size: " << AllRecipesSet.size() << '\n');
}

// Generate a random int from [Min, Max]
static int randInt(int Min, int Max) {
  std::random_device RD;
  std::mt19937 Gen(RD());
  std::uniform_int_distribution<> Distr(Min, Max);
  return Distr(Gen);
}

// Add a version to the /shm to support parallel compilation
static const char *appendToString(const char *original,
                                  const std::string &extra) {

  size_t NewLen = std::strlen(original) + extra.length() + 1;
  char *NewBuf = new char[NewLen];
  std::strcpy(NewBuf, original);
  std::strcat(NewBuf, extra.c_str());

  return NewBuf;
}

std::string formatDouble(double Num, int Precision) {
  if (Precision == 0)
    Precision -= 1;
  std::string D = std::to_string(Num);
  return D.substr(0, D.find_last_of(".") + Precision + 1);
}

int createSharedMemory(const char *shm_name, size_t shm_size,
                       void **shared_memory, bool ModLevelIPC) {
  int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
  if (shm_fd == -1) {
    perror("shm_open");
    return -1;
  }

  // Set the size of the shared memory object
  if (ftruncate(shm_fd, shm_size) == -1) {
    perror("ftruncate");
    return -1;
  }

  *shared_memory =
      mmap(nullptr, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (*shared_memory == MAP_FAILED) {
    perror("mmap");
    return -1;
  }

  SharedMemoryData *header = (SharedMemoryData *)*shared_memory;
  header->dataSize = shm_size - sizeof(SharedMemoryData);
  header->ModLevelIPC = ModLevelIPC;
  header->dataOffset = sizeof(SharedMemoryData);
  header->ir2vec_dimension = IR2VEC_DIMENSION;

  close(shm_fd);
  return 0;
}

llvm::Expected<std::unique_ptr<llvm::Module>>
readModuleFromSharedMemory(const char *shm_name, llvm::LLVMContext &Context) {
  int shm_fd = shm_open(shm_name, O_RDONLY, 0666);
  if (shm_fd == -1) {
    perror("shm_open");
    return llvm::make_error<llvm::StringError>("shm_open failed",
                                               llvm::inconvertibleErrorCode());
  }

  void *shared_memory =
      mmap(nullptr, sizeof(SharedMemoryData), PROT_READ, MAP_SHARED, shm_fd, 0);

  if (shared_memory == MAP_FAILED) {
    llvm::errs() << "FAILED\n";
    perror("mmap");
    close(shm_fd);
    return llvm::make_error<llvm::StringError>("mmap failed",
                                               llvm::inconvertibleErrorCode());
  }

  SharedMemoryData *shared_data =
      static_cast<SharedMemoryData *>(shared_memory);
  size_t bitcode_size = shared_data->bitcode_size;

  munmap(shared_memory, sizeof(SharedMemoryData));

  // Remap the shared memory with the correct size.
  shared_memory = mmap(nullptr, sizeof(SharedMemoryData) + bitcode_size,
                       PROT_READ, MAP_SHARED, shm_fd, 0);
  if (shared_memory == MAP_FAILED) {
    perror("mmap");
    close(shm_fd);
    return llvm::make_error<llvm::StringError>("mmap failed",
                                               llvm::inconvertibleErrorCode());
  }

  shared_data = static_cast<SharedMemoryData *>(shared_memory);

  const int MAX_BITCODE_SIZE = 200000;
  if (bitcode_size > MAX_BITCODE_SIZE) {
    llvm::errs() << "Error: Bitcode size exceeds maximum.\n";
    close(shm_fd);
    return llvm::make_error<llvm::StringError>("mmap failed",
                                               llvm::inconvertibleErrorCode());
  }

  std::vector<char> bitcode_buffer(bitcode_size);
  memcpy(bitcode_buffer.data(), (char *)shared_data + shared_data->dataOffset,
         bitcode_size);

  llvm::MemoryBufferRef bufferRef(
      llvm::StringRef(bitcode_buffer.data(), bitcode_size), "shared_memory");
  llvm::Expected<std::unique_ptr<llvm::Module>> moduleOrErr =
      llvm::parseBitcodeFile(bufferRef, Context);

  munmap(shared_memory, sizeof(SharedMemoryData) + bitcode_size);
  close(shm_fd);

  return moduleOrErr;
}

void readFeaturesFromSharedMemory(
    const char *ShmName,
    std::vector<std::pair<std::string, std::string>> &Features) {
  int ShmFD = shm_open(ShmName, O_RDWR, 0666);
  if (ShmFD == -1) {
    perror("shm_open");
    return;
  }

  SharedMemoryData *SharedData =
      (SharedMemoryData *)mmap(nullptr, sizeof(SharedMemoryData),
                               PROT_READ | PROT_WRITE, MAP_SHARED, ShmFD, 0);
  if (SharedData == MAP_FAILED) {
    perror("mmap (reader)");
    close(ShmFD);
    return;
  }
  size_t ShmSize = sizeof(SharedMemoryData) + SharedData->dataSize;

  munmap(SharedData, 0);
  SharedData = (SharedMemoryData *)mmap(
      nullptr, ShmSize, PROT_READ | PROT_WRITE, MAP_SHARED, ShmFD, 0);
  if (SharedData == MAP_FAILED) {
    perror("mmap (remap reader)");
    return;
  }

  char *Ptr =
      (char *)SharedData + SharedData->dataOffset + IR2VEC_EMBEDDING_SIZE;
  char *EndPtr = (char *)SharedData + ShmSize;

  for (size_t i = 0; i < SharedData->featureCount; ++i) {
    size_t KeySize = strlen(Ptr) + 1;
    std::string Key(Ptr);
    Ptr += KeySize;

    if (Ptr + sizeof(float) > EndPtr) {
      llvm::errs() << "Buffer overflow prevented while copying value for key\n";
      break;
    }
    float FloatValue;
    memcpy(&FloatValue, Ptr, sizeof(float));
    Ptr += sizeof(float);

    std::stringstream ss;
    ss << std::fixed << std::setprecision(6) << FloatValue;
    std::string Value = ss.str();

    Features.push_back(std::make_pair(Key, Value));
  }

  munmap(SharedData, ShmSize);
  close(ShmFD);
}

void readIR2VecFromSharedMemory(
    const char *ShmName,
    std::vector<std::pair<std::string, std::string>> &Features) {
  int ShmFD = shm_open(ShmName, O_RDWR, 0666);
  if (ShmFD == -1) {
    perror("shm_open");
    return;
  }

  SharedMemoryData *SharedData =
      (SharedMemoryData *)mmap(nullptr, sizeof(SharedMemoryData),
                               PROT_READ | PROT_WRITE, MAP_SHARED, ShmFD, 0);
  if (SharedData == MAP_FAILED) {
    perror("mmap (reader)");
    close(ShmFD);
    return;
  }
  size_t ShmSize = sizeof(SharedMemoryData) + SharedData->dataSize;

  munmap(SharedData, 0);
  SharedData = (SharedMemoryData *)mmap(
      nullptr, ShmSize, PROT_READ | PROT_WRITE, MAP_SHARED, ShmFD, 0);
  if (SharedData == MAP_FAILED) {
    perror("mmap (remap reader)");
    return;
  }

  double *Ptr = (double *)((char *)SharedData + SharedData->dataOffset);

  for (size_t I = 0; I < IR2VEC_DIMENSION; ++I) {
    double Value;
    memcpy(&Value, Ptr, sizeof(double));
    Ptr += 1; // pointer arithmetic on double ptrs automatically works
    Features.push_back(
        std::make_pair("IR2Vec_" + std::to_string(I + 1),
                       std::to_string(static_cast<float>(Value))));
  }

  munmap(SharedData, ShmSize);
  close(ShmFD);
}

void cleanupSharedMemory(const char *shm_name, size_t shm_size,
                         void *shared_memory) {
  if (shared_memory != nullptr) {
    munmap(shared_memory, shm_size);
    shm_unlink(shm_name);
  }
}

void SimulatedAnnealingProtean::run() {
  SimulatedAnnealingProtean::State S = Generator->generateRecipe("01234");
  SimulatedAnnealingProtean::State SNew = S;
  setCurState(SNew);
  setFinalState(SNew);

  llvm::LLVMContext Context;
  std::unique_ptr<llvm::IR2ScoreModel> IRModel =
      std::make_unique<llvm::IR2ScoreModel>(&Context, UseAOTModel, nullptr,
                                            true, UseProteanCollect);

  std::unordered_set<std::string> ExploredRecipes;
  for (int Iteration = 0; Iteration < this->MaxIterations; ++Iteration) {
    double Temp = temperature(Iteration);
    if (Iteration != 0) {
      SNew = Generator->generateRecipeGenetic(
          AllRecipes, PhaseOrderGeneratorBase::recipesToString(S), Iteration,
          Temp);
    }
    std::string SNewStr = PhaseOrderGeneratorBase::recipesToString(SNew);
    if (ExploredRecipes.find(SNewStr) != ExploredRecipes.end()) {
      LLVM_DEBUG(llvm::dbgs() << "Skipping " << SNewStr << "...\n");
      continue;
    }
    ExploredRecipes.insert(SNewStr);

    setCurState(SNew);
    LLVM_DEBUG(llvm::dbgs()
               << "Iteration " << Iteration << " Temperature:" << Temp << "\n");
    int CacheSize = CachedCosts.size();
    int Limit = 100;
    if (CacheSize >= Limit) {
      double LastCost = CachedCosts[CacheSize - 1];
      bool Changing = false;
      for (int i = CacheSize - 1; i >= std::max(CacheSize - Limit, 0); i--) {
        if (CachedCosts[i] != LastCost) {
          Changing = true;
        }
      }
      if (!Changing) {
        break;
      }
    }

    if (Temp <= 0.1) {
      break;
    }

    const int MAX_BITCODE_SIZE = 200000;

    // Append PID when naming shared memory object to enable parallel
    // compilation.
    llvm::sys::Process::Pid PPid = llvm::sys::Process::getProcessId();
    LLVM_DEBUG(llvm::dbgs() << "Parent PID: " << PPid << "\n");
    const std::string ShmBase = "/shm_" + std::to_string(PPid);
    const char *shm_name = ShmBase.c_str();
    LLVM_DEBUG(llvm::dbgs() << "Shared memory char name: " << shm_name << "\n");

    void *shared_memory = nullptr;
    size_t shm_size;
    if (ModLevelIPC)
      shm_size = sizeof(SharedMemoryData) + MAX_BITCODE_SIZE;
    else
      shm_size = IRModel->getModelFeaturesSize() + IR2VEC_EMBEDDING_SIZE + sizeof(SharedMemoryData);

    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    if (createSharedMemory(shm_name, shm_size, &shared_memory, ModLevelIPC) !=
        0) {
      LLVM_DEBUG(llvm::dbgs() << "Shared memory initialization error.\n");
      return;
    }

    // Fork a new child process and compile with the new recipe.
    // The parent process waits for the child process to finish and then
    // continue the  Simulated Annealing main loop.
    pid_t Pid = fork();
    if (Pid != 0) {
      int Wstatus;
      waitpid(Pid, &Wstatus, 0);
      if (!WIFEXITED(Wstatus)) {
        LLVM_DEBUG(llvm::dbgs()
                   << "Recipe exited unexpected: "
                   << PhaseOrderGeneratorBase::recipesToString(SNew) << "\n");
      } else {
        LLVM_DEBUG(llvm::dbgs()
                   << "Child exited with: " << WEXITSTATUS(Wstatus) << "\n\n");
      }
    } else {
      return;
    }

    // Accept or reject the new state.
    double P = probabilityOfNewState(S, SNew, Temp);
    if (P == -1) {
      continue;
    }

    std::uniform_real_distribution<double> Dist(0, 1);
    double Random = Dist(RandomEngine);
    if (ProteanOutputTable) {
      std::stringstream ss;
      ss << std::setw(9) << Iteration << std::setw(20)
         << PhaseOrderGeneratorBase::recipesToString(S) << std::setw(20)
         << PhaseOrderGeneratorBase::recipesToString(SNew) << std::setw(20)
         << PhaseOrderGeneratorBase::recipesToString(getFinalState())
         << std::setw(20) << formatDouble(cost(S), 6) << std::setw(20)
         << formatDouble(cost(SNew), 6) << std::setw(20)
         << formatDouble(cost(getFinalState()), 6) << std::setw(20)
         << (P >= Random ? "Y" : "N") << std::setw(20) << formatDouble(Temp, 3)
         << "\n";
      llvm::dbgs() << ss.str();
    }
    if (P >= Random) {
      LLVM_DEBUG(llvm::dbgs() << "New state accepted\n");
      S = SNew;
    }

    if (CostType == IRCostFunction::IRAnalysis) {
      if (cost(getFinalState()) < cost(S)) {
        setFinalState(S);
      }
    } else {
      if (cost(getFinalState()) > cost(S)) {
        setFinalState(S);
      }
    }

    if (getFinalState() != SNew) {
      std::string NewOutputFilename = OutputFilename;
      NewOutputFilename.insert(
          NewOutputFilename.find_last_of("."),
          "-" + PhaseOrderGeneratorBase::recipesToString(SNew));
      std::remove(NewOutputFilename.c_str());
    }

    cleanupSharedMemory(shm_name, shm_size, shared_memory);
  }
  LLVM_DEBUG(llvm::dbgs() << "Explored Recipes Size: " << ExploredRecipes.size()
                          << "\n");
  LLVM_DEBUG(llvm::dbgs() << "Recipes Set:");
  for (auto &Recipe : ExploredRecipes) {
    LLVM_DEBUG(llvm::dbgs() << " " << Recipe);
  }
  LLVM_DEBUG(llvm::dbgs() << "\n");
  LLVM_DEBUG(llvm::dbgs() << "Finished SA\n");
  setFinished(true);
}

SimulatedAnnealingProtean::State
SimulatedAnnealingProtean::neighbour(SimulatedAnnealingProtean::State &S) {
  return Generator->generateRecipe(AllRecipes, 0);
}

double SimulatedAnnealingProtean::temperature(int Iteration) {
  // Generate new temperature based on cooling schedule
  switch (CoolingSchedule) {
  case CoolingType::Geometric:
    CoolingRate = pow(MinTemperature / MaxTemperature, 1.0 / (MaxIterations));
    return MaxTemperature * pow(CoolingRate, Iteration);
  case CoolingType::Linear:
    CoolingRate = (MinTemperature + MaxTemperature) / MaxIterations;
    return this->MaxTemperature - this->CoolingRate * Iteration;
  default:
    llvm_unreachable("Not a valid cooling schedule");
  }
}

double SimulatedAnnealingProtean::irAnalysisCost(
    const SimulatedAnnealingProtean::State &S, std::string OutputFilename) {
  static std::set<std::string> previousRecipes;
  static std::set<std::string> previousModuleIdentifiers;
  std::string RecipeStr = PhaseOrderGeneratorBase::recipesToString(S);
  llvm::LLVMContext Context;
  llvm::SMDiagnostic Err;
  std::vector<std::pair<std::string, std::string>> Features;
  std::unique_ptr<llvm::Module> M;

  if (previousRecipes.find(RecipeStr) == previousRecipes.end()) {
    LLVM_DEBUG(llvm::dbgs() << "Recipe changed\n");
    previousRecipes.insert(RecipeStr);
  }

  float Cost = 1;
  std::optional<std::string> BishengAcpoDir =
      llvm::sys::Process::GetEnv("BISHENG_ACPO_DIR");
  if (!BishengAcpoDir) {
    llvm::errs() << "Please Export BISHENG_ACPO_DIR\n";
    return -1;
  }

  // Append PID when naming shared memory object to enable parallel
  // compilation.
  llvm::sys::Process::Pid Pid = getpid();
  const std::string ShmBase = "/shm_" + std::to_string(Pid);
  const char *ShmName = ShmBase.c_str();
  LLVM_DEBUG(llvm::dbgs() << "Parent PID (from the child): " << getpid() << " "
                          << ShmName << "\n");

  if (ModLevelIPC) {
    // Read the module from memory
    std::error_code EC;
    std::string SAModelFile = "Simulated-annealing-model";
    llvm::raw_fd_ostream RawOS(SAModelFile, EC, llvm::sys::fs::CD_OpenAlways,
                                llvm::sys::fs::FA_Write,
                                llvm::sys::fs::OF_Append);
    llvm::formatted_raw_ostream OS(RawOS);
    llvm::ModelDataScoreCollector MDC(OS, SAModelFile);

    LLVM_DEBUG(llvm::dbgs() << "Parsing module\n");
    llvm::Expected<std::unique_ptr<llvm::Module>> moduleOrErr =
        readModuleFromSharedMemory(ShmName, Context);

    if (moduleOrErr) {
      M = std::move(moduleOrErr.get());
      LLVM_DEBUG(llvm::dbgs() << "Module read from shared memory.\n");
    } else {
      llvm::errs() << "Error reading module: "
                    << llvm::toString(moduleOrErr.takeError()) << "\n";
    }

    if (previousModuleIdentifiers.find(M->getModuleIdentifier()) ==
        previousModuleIdentifiers.end()) {
      // New identifier found
      LLVM_DEBUG(llvm::dbgs() << "Module Identifier changed\n");
      previousModuleIdentifiers.insert(M->getModuleIdentifier());
    }

    if (UseProteanCollect) {
      MDC.collectFeatures(M);
      Features = MDC.getFeatures();
    } else {
      auto ir2vec = IR2Vec::Embeddings(*M, IR2Vec::IR2VecMode::FlowAware, IR2VEC_DIMENSION);
      IR2Vec::Vector prgmVec = ir2vec.getProgramVector();
      for (int i = 0; i < prgmVec.size(); ++i) {
        std::string Feature = "IR2Vec_";
        Feature += std::to_string(i + 1);
        Features.push_back({Feature, std::to_string(prgmVec[i])});
      }
    }
  } else {
    if (UseProteanCollect) {
      readFeaturesFromSharedMemory(ShmName, Features);
    } else {
      readIR2VecFromSharedMemory(ShmName, Features);
    }
  }

  std::unique_ptr<llvm::IR2ScoreModel> IRModel =
      std::make_unique<llvm::IR2ScoreModel>(&Context, UseAOTModel, nullptr,
                                            true, UseProteanCollect);
  IRModel->setMLCustomFeatures(Features);
  LLVM_DEBUG(llvm::dbgs() << "\nFeatures:\n");
  for (auto P : Features)
    LLVM_DEBUG(llvm::dbgs() << "{" << P.first << ", " << P.second << "}");
  LLVM_DEBUG(llvm::dbgs() << "\n");
  std::unique_ptr<llvm::ACPOAdvice> Score = IRModel->getAdvice();
  if (!Score) {
    LLVM_DEBUG(llvm::dbgs() << "\nScore is null\n");
    llvm::errs() << "Model is null, please recompile with correct model specified\n";
    return -1;
  } else {
    LLVM_DEBUG(llvm::dbgs() << "Score is generated by the ML model successfully!\n");
  }
  llvm::Constant *Val = Score->getField("IRSCORE");
  assert(Val != nullptr);
  assert(llvm::isa<llvm::ConstantFP>(Val));
  llvm::ConstantFP *IRScore = llvm::dyn_cast<llvm::ConstantFP>(Val);
  auto &APCost = IRScore->getValueAPF();
  Cost = APCost.convertToFloat();

  if (!Cost) {
    LLVM_DEBUG(llvm::dbgs() << "Cost is null\n");
  } else {
    LLVM_DEBUG(llvm::dbgs() << "Cost returned as " + std::to_string(Cost) + "\n");
  }
  CostMap[RecipeStr] = Cost;
  return Cost;
}

double SimulatedAnnealingProtean::fileSizeCost(
    const SimulatedAnnealingProtean::State &S, std::string OutputFilename) {
  std::string RecipeStr = PhaseOrderGeneratorBase::recipesToString(S);
  uint64_t Size;
  std::error_code EC = llvm::sys::fs::file_size(OutputFilename, Size);
  if (EC) {
    llvm::errs() << EC.message() << '\n';
    return -1;
  }
  CostMap[RecipeStr] = Size;
  return Size;
}

double
SimulatedAnnealingProtean::mcaCost(const SimulatedAnnealingProtean::State &S,
                                   std::string OutputFilename) {
  std::optional<std::string> LLVMDIROpt =
      llvm::sys::Process::GetEnv("LLVM_DIR");
  if (!LLVMDIROpt) {
    llvm::errs() << "Please Export LLVM_DIR to your Install Directory\n";
    return -1.0;
  }
  std::string RecipeStr = PhaseOrderGeneratorBase::recipesToString(S);
  // Converts output generated by child to assembly, then runs llvm-mca
  // to collect information about cycles taken
  if (instructionCountCost(S, OutputFilename) < 1.0) {
    return 0;
  }
  std::string Command = "(" + *LLVMDIROpt + "/bin/llc -o - " + OutputFilename +
                        " | llvm-mca ) 2>/dev/null";
  char Buffer[128];
  std::string McaResult = "";

  // Open pipe to read output from llvm-mca
  std::FILE *Pipe = popen(Command.c_str(), "r");
  if (!Pipe) {
    return -1;
  }

  // Read until end of process
  while (!feof(Pipe)) {
    if (fgets(Buffer, 128, Pipe) != nullptr)
      McaResult += Buffer;
  }
  pclose(Pipe);
  std::istringstream McaStream(McaResult);
  double Instructions, Cycles;
  for (std::string Line; std::getline(McaStream, Line);) {
    // Loop until we find the line with the amount of cycles taken
    if (Line.find("Cycles:") != std::string::npos) {
      int LastSpace = Line.find_last_of(" ");
      Cycles = std::stod(Line.substr(LastSpace, Line.size()));
      CostMap[RecipeStr] = Cycles;
      return Cycles;
    }
  }
  return -1.0;
}

double SimulatedAnnealingProtean::instructionCountCost(
    const SimulatedAnnealingProtean::State &S, std::string OutputFilename) {
  std::string RecipeStr = PhaseOrderGeneratorBase::recipesToString(S);
  llvm::LLVMContext Context;
  llvm::SMDiagnostic Err;
  std::unique_ptr<llvm::Module> M =
      parseIRFile(OutputFilename, Err, Context, {});
  if (!M) {
    llvm::errs() << "Could not calculate cost, invalid IR\n";
    return -1;
  }
  int Instructions = 0;
  for (auto &F : *M) {
    for (auto &BB : F) {
      Instructions += BB.size();
    }
  }
  CostMap[RecipeStr] = Instructions;
  return Instructions;
}

double
SimulatedAnnealingProtean::cost(const SimulatedAnnealingProtean::State &S) {
  // perform IR analysis to determine cost of current state
  std::string RecipeStr = PhaseOrderGeneratorBase::recipesToString(S);
  // Check if cost has previously been calculated, if so return that value
  if (CostMap.find(RecipeStr) != CostMap.end()) {
    return CostMap[RecipeStr];
  }
  std::string NewOutputFilename = OutputFilename;
  assert(NewOutputFilename.find_last_of(".") != std::string::npos);
  NewOutputFilename.insert(NewOutputFilename.find_last_of("."),
                           "-" + PhaseOrderGeneratorBase::recipesToString(S));
  switch (CostType) {
  case IRCostFunction::IRAnalysis:
    return irAnalysisCost(S, NewOutputFilename);
  case IRCostFunction::InstCount:
    return instructionCountCost(S, NewOutputFilename);
  case IRCostFunction::FileSize:
    return fileSizeCost(S, NewOutputFilename);
  case IRCostFunction::MCA:
    return mcaCost(S, NewOutputFilename);
  default:
    llvm_unreachable("Not a valid cost function");
  }
}

double SimulatedAnnealingProtean::probabilityOfNewState(
    SimulatedAnnealingProtean::State &S, SimulatedAnnealingProtean::State &SNew,
    double Temperature) {
  double CurrentCost = cost(S);
  double NewCost = cost(SNew);

  CachedCosts.push_back(NewCost);
  Generator->updateBestRecipes(PhaseOrderGeneratorBase::recipesToString(SNew),
                               NewCost);

  if ((CurrentCost == -1) || (NewCost == -1)) {
    return -1;
  }
  double Diff = 100.0 * (NewCost - CurrentCost) / CurrentCost;
  LLVM_DEBUG(llvm::dbgs() << "Cost of new state: " << cost(SNew) << "\n");
  LLVM_DEBUG(llvm::dbgs() << "Cost of current state: " << cost(S) << "\n");

  // If new state is worse than old, use equation below to calculate
  // probability of accepting new state
  if (CostType == IRCostFunction::IRAnalysis) {
    if (Diff <= 0)
      return exp(1 * Diff / Temperature);
  } else {
    if (Diff >= 0)
      return exp(-1 * Diff / Temperature);
  }

  // If new state is better than old, accept new state
  return 1.0;
}
