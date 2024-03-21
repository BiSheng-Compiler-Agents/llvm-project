#if defined(ENABLE_AUTOTUNER)
//===-- AutoTuning.h - Auto-Tuning-----------------------------------------===//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines Auto Tuning related functions, models and interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_AUTOTUNER_AUTOTUNING_H_
#define LLVM_AUTOTUNER_AUTOTUNING_H_

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Support/Casting.h"
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

// Options for AutoTuner incremental compilation.
enum AutoTuningCompileOpt {
  Inactive,    // Disabled incremental compilation.
  CoarseGrain, // For tuning LLVMParam.
  FineGrain,   // For tuning default code regions (Loop, CallSite, Function).
  Basic        // Same as CoarseGrain but can be applied for any code region.
               // Can be used with ImpactRanker.
};

namespace autotuning {
// Constant defintion for AutoTuner incremental compilation.
const std::string CompileOptionStart = "start";
const std::string CompileOptionEnd = "end";
const std::string CompileOptionUnknow = "unknown";
const std::string CompileOptionUnroll = "loop-unroll";
const std::string CompileOptionVectorize = "loop-vectorize";
const std::string CompileOptionInline = "inline";

class ParameterBase {
public:
  virtual ~ParameterBase() = default;
  enum ParameterKind {
    PK_PARAMETER,
  };
  ParameterKind getKind() const { return Kind; }

  explicit ParameterBase(ParameterKind K) : Kind(K) {}

private:
  const ParameterKind Kind;
};

template <typename T> class Parameter : public ParameterBase {
public:
  Parameter(const T &RHS) : ParameterBase(PK_PARAMETER), Value(RHS) {}
  const T &getValue() const { return Value; }
  void setValue(const T &RHS) { Value = RHS; }

  static bool classof(const ParameterBase *P) {
    return P->getKind() == PK_PARAMETER;
  }

private:
  T Value;
};

/// This class manages parameters of one codeRegion.
class ParameterManager {

public:
  // add a param into this ParameterManager
  template <typename T>
  void add(const std::string &ParamName, const T ParamValue) {
    std::shared_ptr<ParameterBase> Param =
        std::make_shared<Parameter<T>>(ParamValue);
    this->Parameters[ParamName] = Param;
  }

  // Look up the value of a parameter by name in this ParameterManager.
  // The found value will be assigned to the reference variable "Value".
  // Return true if the parameter exits in this ParameterManager,
  // and false otherwise.
  template <typename T>
  bool findByName(const std::string &ParamName, T &Value) const {
    auto Iterator = Parameters.find(ParamName);
    if (Iterator == Parameters.end()) {
      return false;
    }

    auto ParamPtr = llvm::dyn_cast<Parameter<T>>(Iterator->second.get());
    if (ParamPtr != nullptr) {
      Value = ParamPtr->getValue();
      return true;
    } else {
      return false;
    }
  }

private:
  std::unordered_map<std::string, std::shared_ptr<ParameterBase>> Parameters;
};

/// The debug location used to track a CodeRegion back to the source file.
struct SourceLocation {
  ///  The source file corresponding to this CodeRegion.
  std::string SourceFilePath;
  unsigned SourceLine = 0;
  unsigned SourceColumn = 0;

  bool operator==(const SourceLocation &CR) const {
    return (this->SourceFilePath == CR.SourceFilePath) &&
           (this->SourceLine == CR.SourceLine) &&
           (this->SourceColumn == CR.SourceColumn);
  };

  explicit operator bool() const {
    return !(SourceFilePath.empty() && SourceLine == 0 && SourceColumn == 0);
  }
};

enum CodeRegionType {
  CallSite,          // Code region for function inlining.
  Function,          // Used in AutoTuningDump pass for IR writing.
  LLVMParam,         // Compilation flags. Tuned individually for each module.
  Loop,              // Code region for loops.
  MachineBasicBlock, // Instruction scheduling code region.
  Other,             // Pass ordering code region.
  ProgramParam,      // Compilation flags. Tuned collectively for program.
  Switch,            // Tuning MinJumpTableEntries parameter for switch inst.
  Empty,             // Empty CodeRegion.
  Invalid            // Invalid CodeRegion.
};

enum HotnessType {
  Unknown,
  Cold,
  Hot,
};

/// DynamicOptions represent a map: Arg -> DynamicConfigs.
/// Where Arg is a tuning parameter on the associated CodeRegion.
/// And DynamicConfigs is the possible tuning values associated with Arg.
typedef std::map<std::string, std::vector<unsigned int>> DynamicOptions;

/// This class represents a region in source code including
/// its name, function name, type, debug location, and associated pass name.
class CodeRegion {

public:
  // Default constructor
  CodeRegion(const CodeRegionType Type = CodeRegionType::Other);
  ~CodeRegion() = default;
  // Concrete constructors
  CodeRegion(const std::string &Name, const std::string &FuncName,
             const CodeRegionType &Type, const llvm::DebugLoc &DL,
             const DynamicOptions DO = {});
  CodeRegion(const std::string &Name, const std::string &FuncName,
             const CodeRegionType &Type,
             const SourceLocation &Location = SourceLocation(),
             const DynamicOptions DO = {});
  CodeRegion(const std::string &Name, const std::string &FuncName,
             const std::string &PassName, const CodeRegionType &Type,
             const SourceLocation &Location = SourceLocation(),
             const unsigned int Invocation = 0);

  bool operator==(const CodeRegion &CR) const;
  inline bool operator!=(const CodeRegion &CR) const { return !(*this == CR); };

  explicit operator bool() const {
    return !(Name.empty() && FuncName.empty() && PassName.empty());
  }

  static std::string getTypeAsString(CodeRegionType CRType);
  static std::string getHotnessAsString(HotnessType Hotness);
  const std::string &getName() const { return Name; }
  const std::string &getFuncName() const { return FuncName; }
  const CodeRegionType &getType() const { return Type; }
  const std::string &getFileName() const { return Location.SourceFilePath; }
  const std::string &getTypeAsString() const { return StringType; }
  const SourceLocation &getSourceLoc() const { return Location; }
  const std::string &getPassName() const { return PassName; }
  unsigned getSize() const { return Size; };
  void setPassName(const std::string &NewPassName);
  void setSize(unsigned Size) { this->Size = Size; };
  void setHotness(HotnessType NewHotness) const { this->Hotness = NewHotness; }
  HotnessType getHotness() const { return this->Hotness; }
  std::string getHotnessAsString() const { return getHotnessAsString(Hotness); }
  bool isCold() const { return this->Hotness == Cold; }
  bool isHot() const { return this->Hotness == Hot; }
  std::uint64_t getHash() const { return this->Hash; }
  void setHash(std::uint64_t Hash) { this->Hash = Hash; }
  DynamicOptions getAutoTunerOptions() const { return this->AutoTunerOptions; }
  void setInvocation(unsigned int Invocation) { this->Invocation = Invocation; }
  unsigned int getInvocation() const { return this->Invocation; }

  /// Add dynamic config options with Code Region for AutoTuner to tune instead
  /// of using static config options.
  void addAutoTunerOptions(const std::string ParamName,
                           std::vector<unsigned int> Options) const {
    this->AutoTunerOptions.insert(
        std::pair<std::string, std::vector<unsigned int>>(ParamName, Options));
  }
  static CodeRegion getInvalidInstance();
  static CodeRegion getEmptyInstance();
  void setBaselineConfig(std::map<std::string, std::string> Value) const {
    this->BaselineConfig = Value;
  };
  std::map<std::string, std::string> getBaselineConfig() const {
    return this->BaselineConfig;
  }

private:
  /// Name of the code region.
  /// For most of cases it's set to the name of a header basic block.
  std::string Name;
  /// Function name of this code region if any.
  std::string FuncName;
  /// Name of the pass which this code region is associated.
  std::string PassName;
  /// Type of this code region. Options are other, function, loop,
  /// and machine basic block.
  CodeRegionType Type;
  /// Source Location.
  SourceLocation Location;
  std::string StringType;
  /// Structural hash for the CodeRegion.
  std::uint64_t Hash = 0;
  /// Configs values passed to AutoTuner for dynamic setting of search space
  /// for code regions.
  mutable DynamicOptions AutoTunerOptions;
  /// Configuration values passed to AutoTuner for generating the same binary
  /// as the baseline.
  mutable std::map<std::string, std::string> BaselineConfig;

  /// Record the order of invocation of an optimization pass during the whole
  /// compilation pipeline. It is used to differentiate multiple invocations of
  /// a same optimization pass.
  /// Currently, Loop Unroll pass is invoked twice during the compilation
  /// pipeline. 'Invocation' helps to relate a code region with the invocation
  /// of Loop Unroll pass where the code region is generated.
  mutable unsigned int Invocation;

  /// Size of this code region. Usually it refers to the number of instructions
  /// but could be different based on implementations.
  unsigned Size = 0;
  mutable HotnessType Hotness = Unknown;

  /// A boolean flag to record if a CR is initialized or not.
  /// It should only be set to true by initContainer().
  /// We only add initialized CR to TuningOpps.
  bool Initialized = false;

  friend class AutoTuningEngine;
};

/// This class is an interface for classes representing code regions in LLVM
/// (eg. Loop, Function and MachineBasicBlock) to inherit
/// so that auto-tuning can be enabled on them.
/// A Container must contain a CodeRegion.
class Container {

public:
  Container() {}
  virtual ~Container(){};

  /// Abstract method for derived classes to overwrite
  virtual void initCodeRegion() = 0;
  virtual uint64_t computeStructuralHash() = 0;

  /// Get the Container's CodeRegion.
  const CodeRegion &getCodeRegion() const;
  /// Set the Container's CodeRegion.
  void setCodeRegion(const CodeRegion &NewCR);
  /// This method is to look up the value of a parameter that corresponds to an
  /// Container. The parameter being looked up is stored in a ParameterManager.
  template <typename T>
  bool lookUpParams(const std::string &ParamsName, T &Value) const;

  /// Check if the code region is being tuned by config file.
  bool requiresIRDump(bool IsFunctionIR = false) const;

private:
  CodeRegion CR;
  friend class AutoTuningEngine;
};
} // end namespace autotuning

namespace std {
template <>
// Implement hash for CodeRegion data type in std namespace. Only using common
// attributes (with and without using 'OmitAutotuningMetadata' flag) of
// CodeRegion. Remaining attributes are compared in overloaded == function.
struct hash<autotuning::CodeRegion> {
  std::size_t operator()(const autotuning::CodeRegion &CR) const {
    return llvm::hash_combine(CR.getPassName(), CR.getType());
  }
};
} // namespace std

namespace llvm {
// Forward Decleration.
class CallBase;

typedef autotuning::CodeRegion CodeRegion;
template <> struct DenseMapInfo<CodeRegion> {
  static bool isEqual(const CodeRegion &LHS, const CodeRegion &RHS) {
    return LHS == RHS;
  }
  static inline CodeRegion getEmptyKey() {
    return autotuning::CodeRegion::getEmptyInstance();
  }
  static inline CodeRegion getTombstoneKey() {
    return autotuning::CodeRegion::getInvalidInstance();
  }
  // Implement hash for CodeRegion data type in llvm namespace. Only using
  // common attributes (with and without using 'OmitAutotuningMetadata' flag)
  // of CodeRegion. Remaining attributes are compared in overloaded ==
  // function.
  static unsigned getHashValue(const CodeRegion &CR) {
    return llvm::hash_combine(CR.getPassName(), CR.getType());
  }
};
} // namespace llvm

namespace autotuning {
using namespace llvm;
typedef std::unordered_map<CodeRegion, ParameterManager> LookUpTable;
typedef llvm::SetVector<CodeRegion> CodeRegions;

/// Structure to store information of CallSite code regions which is used to
/// get a different SourceLocation for multiple callsites (same callee) in a
/// function when these callsites have same SourceLocation due to inlining.
struct CallSiteLocation {
  llvm::CallBase *CB;
  llvm::Function *Caller;
  llvm::Function *Callee;
  SourceLocation SrcLoc;
};

class AutoTuningEngine {
public:
  AutoTuningEngine() { Enabled = false; }
  ~AutoTuningEngine() {}

  /// Initialize the Container for auto-tuning.
  void initContainer(Container *Container, const std::string &PassName,
                     const StringRef FuncName = "", bool AddOpportunity = true,
                     unsigned int Invocation = 0);

  /// Initialize auto-tuning. This method should only be called in the main
  /// function.
  /// \return Error::success() on success or the related Error otherwise.
  llvm::Error init(const std::string &ModuleID);

  /// Finalize auto-tuning. This method should only be called in the main
  /// function.
  /// \return Error::success() on success or the related Error otherwise.
  llvm::Error finalize();

  /// Return the number of tuning configuration used for this compilation.
  llvm::Expected<int> getConfigNumber();

  void enable() { Enabled = true; }
  void disable() { Enabled = false; }
  bool isEnabled() const { return Enabled; }
  bool isMLEnabled() const { return MLEnabled; }
  bool isDumpEnabled() const { return DumpEnabled; }
  bool isGenerateOutput() const { return GenerateOutput; }
  bool isParseInput() const { return ParseInput; }
  bool isTuningAllowedForType(CodeRegionType CRType) const {
    return (CodeRegionFilterTypes.count(CRType) > 0);
  }
  bool isThinLTOTuning() const;

  /// Convert a pass-name to CodeRegionType.
  CodeRegionType convertPassToType(std::string Pass);

  /// First sets BaselineConfig value for the CR then
  /// add a tuning opportunity into the TuningOpps list.
  void addOpportunity(const CodeRegion &OppCR,
                      std::map<std::string, std::string> BaselineConfig = {});
  bool hasOpportunities() const { return TuningOpps.empty(); }

  bool shouldRunOptPass(std::string FileName, std::string Pass);

  /// Insert all of the callsites of a function in CallSiteLocs vector.
  void insertCallSiteLoc(CallSiteLocation Loc);

  /// Update CallSiteLocs vector with new callsites (if any) which get available
  /// due to inlining.
  void updateCallSiteLocs(llvm::CallBase *CB, llvm::CallBase *Ptr,
                          llvm::Function *F, unsigned int Line);

  /// Clean up the CallSiteLocs vector by keeping the callsite if there are
  /// multiple calls to same callee. This cleaning will be perform before
  /// inlining any callsite.
  void cleanCallSiteLoc();

  /// clear the CallSiteLocs vector.
  void clearCallSiteLocs();

  /// Return the SourceLocation::SourceLine (if available).
  std::optional<unsigned int> getCallSiteLoc(llvm::CallBase *CB);

  template <typename T>
  bool lookUpGlobalParams(const std::string &ParamsName, T &Value) const;
  /// A map storing llvm parameters.
  std::unordered_map<std::string, std::string> LLVMParams;
  /// A map storing program parameters.
  std::unordered_map<std::string, std::string> ProgramParams;

private:
  std::string ModuleID;
  /// This boolean indicates if the auto-tuning mode is enabled.
  /// It will be set to true if the any of the following command line options
  /// (auto-tuning-input, auto-tuning-result and auto-tuning-opp) is specified.
  bool Enabled;
  /// This boolean indicates if the ML guidance feature is enabled in
  /// Autotuner. It will be set to true if -fautotune-rank is specified.
  bool MLEnabled;
  /// This boolean indicates if the IR dumping is enabled or not. IR dumping
  /// is enabled for ML guidance feature. It can also be enabled with command
  /// line compiler flag 'enable-autotuning-dump'.
  bool DumpEnabled = false;
  /// This boolean indicates if compiler is parsing/using 'config.yaml' file
  /// generated by AutoTuner and use the configuration values instead of
  /// determining with compiler heuristic.
  bool ParseInput;
  /// This boolean indicates if compiler is creating/generating opportunity
  /// file(s) which will be consumed by AutoTuner to create the search space.
  bool GenerateOutput;
  /// A map of filename and set of optimization passes; an optimization pass
  /// will be added to this set if a CodeRegion belongs to the optimization
  /// pass.
  std::unordered_map<std::string, std::unordered_set<std::string>> OppPassList;

  /// Vector to store all of the duplicate calls in a function and the calls
  /// which get available due to inlining.
  SmallVector<CallSiteLocation, 10> CallSiteLocs;

  /// A set to store the code region types that will be tuned in current
  /// autotuning flow. This will be populated with code region types based on
  /// 'auto-tuning-type-filter' for -fautotune-generate and the types will be
  /// extracted from config.yaml in case of -fautotune.
  /// This set is used to apply type-based filtering prior to creating/
  /// initializing a code region.
  std::unordered_set<CodeRegionType> CodeRegionFilterTypes;

  // A statically initialized map used to convert 'pass-name' to
  // 'CodeRegionType'.
  std::unordered_map<std::string, CodeRegionType> PTTMap;

  /// A map of CodeRegion and ParameterManager to keep track of all the
  /// parameters of code regions loaded from input config file.
  LookUpTable ParamTable;
  /// A list of CodeRegions as tuning opportunities
  CodeRegions TuningOpps;
  /// A ParameterManager for global parameters.
  ParameterManager GlobalParams;

  /// Apply filters for CodeRegions.
  void applyOppFilters(CodeRegions &CRs);

  /// Apply function name filter for CodeRegions.
  bool applyFunctionFilter(std::string FuncName);

  friend class Container;
  friend class CodeRegion;
  friend class AutoTuningRemarkManager;
};

extern class AutoTuningEngine Engine; // AutoTuning Engine

} // end namespace autotuning

#endif /* LLVM_AUTOTUNER_AUTOTUNING_H_ */
#endif
