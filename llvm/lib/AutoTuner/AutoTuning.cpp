#if defined(ENABLE_AUTOTUNER)
//===-- AutoTuning.cpp - Auto-Tuning --------------------------------------===//
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

#include "llvm/AutoTuner/AutoTuning.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/AutoTuner/AutoTuningRemarkManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Process.h"

// Enable debug messages for AutoTuning.
#define DEBUG_TYPE "autotuning"

using namespace llvm;

// defined in 'lib/Remarks/YAMLRemarkParser.cpp'.
extern cl::opt<bool> OmitAutotuningMetadata;

// -auto-tuning-input - Command line option to specify the input file.
static cl::opt<std::string> InputFile("auto-tuning-input", cl::Hidden,
                                      cl::desc("Specify the input file"));

// -auto-tuning-opp - Command line option to specify the output directory of
// tuning opportunities.
static cl::opt<std::string> OutputOppDir(
    "auto-tuning-opp", cl::Hidden,
    cl::desc("Specify the output directory of tuning opportunities"));

static cl::opt<std::string>
    RemarksPasses("auto-tuning-pass-filter", cl::Hidden,
                  cl::desc("Only dump auto-tuning remarks from passes whose "
                           "names match the given regular expression"),
                  cl::value_desc("regex"));

static cl::opt<std::string>
    ProjectDir("autotuning-project-dir", cl::Hidden, cl::init(""),
               cl::desc("Specify project base dir to make code region name "
                        "relative to base dir. This operation will only be "
                        "applied for coarse-grain code regions."));

// -auto-tuning-config-id - Command line option to specify the config number
// being used for compilation. Required only for ML guidance feature.
static cl::opt<int> CFGNumber(
    "auto-tuning-config-id", cl::Hidden,
    cl::desc(
        "Specify the auto-tuning configuration ID used in this compilation."));

static cl::opt<std::string> OutputFormat(
    "auto-tuning-remark-format", cl::Hidden,
    cl::desc("The format used for auto-tuning remarks (default: YAML)"),
    cl::value_desc("format"), cl::init("yaml"));

// AutoTuner incremental compilation options.
cl::opt<AutoTuningCompileOpt> AutoTuningCompileMode(
    "auto-tuning-compile-mode", cl::Hidden, cl::init(Inactive),
    cl::desc("AutoTuner: Choose incremental compilation mode."),
    cl::values(clEnumVal(Inactive,
                         "AutoTuner: Disable incremental compilation."),
               clEnumVal(CoarseGrain, "AutoTuner: Enable incremental "
                                      "compilation for coarse grain tuning."),
               clEnumVal(FineGrain, "AutoTuner: Enable incremental compilation "
                                    "for fine grain tuning."),
               clEnumVal(Basic, "AutoTuner: Enable incremental compilation for "
                                "any kind of code region.")));

static cl::opt<bool>
    EnableAutoTuningDump("enable-autotuning-dump", cl::Hidden, cl::init(false),
                         cl::desc("Enable AutoTuningDump Pass"));

static cl::opt<bool>
    ThinLTOTuning("autotuning-thin-lto", cl::Hidden, cl::init(false),
                  cl::desc("AutoTuner enabled in ThinLTO mode."));

namespace autotuning {

static cl::list<CodeRegionType> AutotuningOutputFilter(
    "auto-tuning-type-filter", cl::Hidden, cl::CommaSeparated,
    cl::desc(
        "Select types of code regions to dump auto-tuning opportunities for:"),
    cl::values(clEnumVal(LLVMParam, "LLVMParam code regions only"),
               clEnumVal(ProgramParam, "ProgramParam code regions only"),
               clEnumVal(CallSite, "CallSite code regions only"),
               clEnumVal(Function, "Function code regions only"),
               clEnumVal(Loop, "Loop code regions only"),
               clEnumVal(MachineBasicBlock,
                         "Machine basic block code regions only"),
               clEnumVal(Switch, "Switch code regions only"),
               clEnumVal(Other, "All other types of code regions")));

static cl::list<std::string> AutotuningFunctionFilter(
    "auto-tuning-function-filter", cl::Hidden, cl::CommaSeparated,
    cl::desc("Apply code region filtering based on function names"));

static const cl::opt<bool> ExcludeColdCodeRegion(
    "auto-tuning-exclude-cold", cl::Hidden, cl::init(true),
    cl::desc("Use profile data to prune cold code regions from auto-tuning"));

static const cl::opt<bool> CodeRegionMatchingWithHash(
    "auto-tuning-code-region-matching-hash", cl::Hidden, cl::init(true),
    cl::desc("Use IR hashing to match the Code Regions"));

static const cl::opt<bool> HotCodeRegionOnly(
    "auto-tuning-hot-only", cl::Hidden, cl::init(false),
    cl::desc(
        "Use profile data to include hot code regions only from auto-tuning"));

static const cl::opt<unsigned>
    SizeThreshold("auto-tuning-size-threshold", cl::Hidden, cl::init(0),
                  cl::desc("Prune small code regions from auto-tuning with a "
                           "size smaller than the threshold"));

static inline const std::string generateName(const std::string &Name) {
  if (Name.empty())
    return "unnamed";
  else
    return Name;
}

//===----------------------------------------------------------------------===//
// CodeRegion implementation
CodeRegion::CodeRegion(const CodeRegionType Type) : Type(Type) {}

CodeRegion::CodeRegion(const std::string &Name, const std::string &FuncName,
                       const CodeRegionType &Type, const DebugLoc &DL,
                       const DynamicOptions DO) {
  this->Name = generateName(Name);
  this->FuncName = generateName(FuncName);
  this->Type = Type;
  this->StringType = getTypeAsString(Type);
  if (DL) {
    StringRef File = DL->getFilename();
    unsigned Line = DL->getLine();
    unsigned Col = DL->getColumn();
    this->Location = SourceLocation{File.str(), Line, Col};
  }
  this->AutoTunerOptions = DO;
}

CodeRegion::CodeRegion(const std::string &Name, const std::string &FuncName,
                       const CodeRegionType &Type,
                       const SourceLocation &Location,
                       const DynamicOptions DO) {
  this->Name = generateName(Name);
  this->FuncName = generateName(FuncName);
  this->Type = Type;
  this->StringType = getTypeAsString(Type);
  this->Location = Location;
  this->AutoTunerOptions = DO;
}

CodeRegion::CodeRegion(const std::string &Name, const std::string &FuncName,
                       const std::string &PassName, const CodeRegionType &Type,
                       const SourceLocation &Location,
                       const unsigned int Invocation)
    : CodeRegion(Name, FuncName, Type, Location) {
  this->PassName = generateName(PassName);
  this->Invocation = Invocation;
}

bool CodeRegion::operator==(const CodeRegion &CodeRegion) const {
  bool IsEqual = false;
  if (OmitAutotuningMetadata)
    IsEqual = (this->getHash() == CodeRegion.getHash()) &&
              (this->Type == CodeRegion.getType()) &&
              (this->PassName == CodeRegion.getPassName());
  else {
    IsEqual = (this->Type == CodeRegion.getType()) &&
              (this->Name == CodeRegion.getName()) &&
              (this->PassName == CodeRegion.getPassName()) &&
              (this->FuncName == CodeRegion.getFuncName()) &&
              (this->Location == CodeRegion.getSourceLoc());
    if (CodeRegionMatchingWithHash)
      IsEqual = IsEqual && (this->getHash() == CodeRegion.getHash());
  }

  if (autotuning::Engine.ParseInput)
    IsEqual = IsEqual && this->getInvocation() == CodeRegion.getInvocation();

  if (autotuning::Engine.GenerateOutput)
    IsEqual =
        IsEqual && this->getBaselineConfig() == CodeRegion.getBaselineConfig();

  return IsEqual;
}

std::string CodeRegion::getTypeAsString(CodeRegionType CRType) {
  switch (CRType) {
  case autotuning::CodeRegionType::MachineBasicBlock:
    return "machine_basic_block";
  case autotuning::CodeRegionType::Loop:
    return "loop";
  case autotuning::CodeRegionType::Function:
    return "function";
  case autotuning::CodeRegionType::CallSite:
    return "callsite";
  case autotuning::CodeRegionType::LLVMParam:
    return "llvm-param";
  case autotuning::CodeRegionType::ProgramParam:
    return "program-param";
  case autotuning::CodeRegionType::Switch:
    return "switch";
  default:
    return "other";
  }
}

std::string CodeRegion::getHotnessAsString(HotnessType Hotness) {
  switch (Hotness) {
  case autotuning::HotnessType::Cold:
    return "cold";
  case autotuning::HotnessType::Hot:
    return "hot";
  default:
    return "unknown";
  }
}

void CodeRegion::setPassName(const std::string &NewPassName) {
  this->PassName = generateName(NewPassName);
}

/* static */
autotuning::CodeRegion CodeRegion::getInvalidInstance() {
  static autotuning::CodeRegion Invalid =
      CodeRegion(autotuning::CodeRegionType::Invalid);
  return Invalid;
}

/* static */
autotuning::CodeRegion CodeRegion::getEmptyInstance() {
  static autotuning::CodeRegion Empty =
      CodeRegion(autotuning::CodeRegionType::Empty);
  return Empty;
}

//===----------------------------------------------------------------------===//
// Container implementation
//

const CodeRegion &Container::getCodeRegion() const { return CR; }

void Container::setCodeRegion(const CodeRegion &NewCR) { this->CR = NewCR; }

template <typename T>
bool Container::lookUpParams(const std::string &ParamsName, T &Value) const {
  bool Found = false;
  auto ConfigMapIterator = Engine.ParamTable.find(CR);
  if (ConfigMapIterator != Engine.ParamTable.end()) {
    ParameterManager InputParams = ConfigMapIterator->second;
    Found = InputParams.findByName(ParamsName, Value);
    if (Found) {
      LLVM_DEBUG(dbgs() << ParamsName << " is set for the CodeRegion: \n"
                        << "  Name: " << CR.getName() << "\n"
                        << "  FuncName: " << CR.getFuncName() << "\n"
                        << "  PassName: " << CR.getPassName() << "\n"
                        << "  Type: " << CR.getTypeAsString() << "\n"
                        << "  Hash: " << CR.getHash() << "\n"
                        << "\n");
    }
  }
  return Found;
}

bool Container::requiresIRDump(bool IsFunctionIR) const {
  auto findBaselineRegion = [&]() -> bool {
    for (auto &entry : Engine.TuningOpps)
      if (!IsFunctionIR) {
        if (CR.getSourceLoc() == entry.getSourceLoc())
          return true;
      } else {
        if (CR.getFileName() == entry.getFileName() &&
            CR.getFuncName() == entry.getFuncName())
          return true;
      }
    return false;
  };
  auto findNonBaselineRegion = [&]() {
    for (auto &entry : Engine.ParamTable)
      if (!IsFunctionIR) {
        if (CR.getSourceLoc() == entry.first.getSourceLoc())
          return true;
      } else {
        if (CR.getFileName() == entry.first.getFileName() &&
            CR.getFuncName() == entry.first.getFuncName())
          return true;
      }
    return false;
  };

  if (CFGNumber == -1)
    return findBaselineRegion();
  else
    return findNonBaselineRegion();
}

template bool Container::lookUpParams<int>(const std::string &ParamsName,
                                           int &Value) const;
template bool Container::lookUpParams<bool>(const std::string &ParamsName,
                                            bool &Value) const;
template bool
Container::lookUpParams<std::string>(const std::string &ParamsName,
                                     std::string &Value) const;
template bool Container::lookUpParams<std::vector<std::string>>(
    const std::string &ParamsName, std::vector<std::string> &Value) const;

static unsigned int count(SmallVector<CallSiteLocation, 10> CallSiteLocs,
                          CallSiteLocation Loc) {
  unsigned int Count = 0;
  for (unsigned int Idx = 0; Idx < CallSiteLocs.size(); ++Idx) {
    if (Loc.Caller == CallSiteLocs[Idx].Caller &&
        Loc.Callee == CallSiteLocs[Idx].Callee)
      Count++;
  }
  return Count;
}

bool AutoTuningEngine::isThinLTOTuning() const { return ThinLTOTuning; }

CodeRegionType AutoTuningEngine::convertPassToType(std::string PassName) {
  auto Search = PTTMap.find(PassName);
  if (Search == PTTMap.end())
    llvm_unreachable(
        "AutoTuningEngine: Invalid/unsupported optimization pass provided.\n");
  return Search->second;
}

void AutoTuningEngine::insertCallSiteLoc(CallSiteLocation Loc) {
  CallSiteLocs.emplace_back(Loc);
}

// If a function has multiple calls to same callee, then insert all the calls in
// the CallSiteLocs vector which get available due to inlining of such calls.
// It will use "Original Call Line No + New Call Line No" instead of using
// "DebugLoc Line No".
void AutoTuningEngine::updateCallSiteLocs(llvm::CallBase *OldCB,
                                          llvm::CallBase *NewCB,
                                          llvm::Function *Callee,
                                          unsigned int Line) {
  for (unsigned int Idx = 0; Idx < CallSiteLocs.size(); ++Idx) {
    if (OldCB == CallSiteLocs[Idx].CB) {
      CallSiteLocation Loc = CallSiteLocs[Idx];
      Loc.CB = NewCB;
      Loc.Callee = Callee;
      Loc.SrcLoc.SourceLine = Loc.SrcLoc.SourceLine + Line;
      CallSiteLocs.emplace_back(Loc);
      break;
    }
  }
}

void AutoTuningEngine::cleanCallSiteLoc() {
  unsigned int Size = CallSiteLocs.size();
  unsigned int Idx = 0;
  for (unsigned int I = 0; I < Size; ++I) {
    CallSiteLocation Loc = CallSiteLocs[Idx];
    unsigned int Count = count(CallSiteLocs, Loc);
    if (Count == 1) {
      CallSiteLocs.erase(CallSiteLocs.begin() + Idx);
      continue;
    }
    Idx++;
  }
}

void AutoTuningEngine::clearCallSiteLocs() { CallSiteLocs.clear(); }

std::optional<unsigned int>
AutoTuningEngine::getCallSiteLoc(llvm::CallBase *CB) {
  for (unsigned int Idx = 0; Idx < CallSiteLocs.size(); ++Idx) {
    if (CB == CallSiteLocs[Idx].CB)
      return CallSiteLocs[Idx].SrcLoc.SourceLine;
  }
  return std::nullopt;
}

void AutoTuningEngine::addOpportunity(
    const CodeRegion &OppCR,
    std::map<std::string, std::string> BaselineConfig) {
  if (!OppCR.Initialized)
    return;

  OppCR.setBaselineConfig(BaselineConfig);
  if (!TuningOpps.contains(OppCR))
    TuningOpps.insert(OppCR);
  else if (OppCR.getHotness() != Unknown) {
    // If OppCR already exists in TuningOpps with unknown hotness,
    // then update it if the current hotness is hot/cold.
    auto OppI = find(TuningOpps, OppCR);
    if (OppI->getHotness() == Unknown)
      OppI->setHotness(OppCR.getHotness());
  }
}

void AutoTuningEngine::applyOppFilters(CodeRegions &CRs) {
  CodeRegions NewCRs;
  for (CodeRegion CR : CRs) {
    if (AutotuningOutputFilter.getNumOccurrences() > 0) {
      bool IsMatched = false;
      for (auto CRType : AutotuningOutputFilter) {
        if (CRType == CR.getType()) {
          IsMatched = true;
          break;
        }
      }
      // Filter out the CodeRegion if its type fails to match any types
      // specified from the command line.
      if (!IsMatched)
        continue;
    }
    if (SizeThreshold.getNumOccurrences() > 0 && CR.getSize() < SizeThreshold)
      continue;
    if (ExcludeColdCodeRegion && CR.isCold()) {
      LLVM_DEBUG(dbgs() << "Skip CodeRegion with cold function "
                        << CR.getFuncName() << "\n");
      continue;
    }
    if (HotCodeRegionOnly && !CR.isHot()) {
      LLVM_DEBUG(dbgs() << "Skip CodeRegion with " << CR.getHotnessAsString()
                        << " function " << CR.getFuncName() << "\n");
      continue;
    }
    NewCRs.insert(CR);
    LLVM_DEBUG(dbgs() << "CodeRegion added as an tuning opportunity: \n"
                      << "  Name: " << CR.getName() << "\n"
                      << "  FuncName: " << CR.getFuncName() << "\n"
                      << "  PassName: " << CR.getPassName() << "\n"
                      << "  Type: " << CR.getTypeAsString() << "\n"
                      << "  Size: " << CR.getSize() << "\n"
                      << "  Hotness: " << CR.getHotnessAsString() << "\n"
                      << "  Hash:   " << CR.getHash() << "\n"
                      << "  Location:   " << CR.getSourceLoc().SourceFilePath
                      << "; " << CR.getSourceLoc().SourceLine << "; "
                      << CR.getSourceLoc().SourceColumn << "\n\n");
  }
  if (AutotuningOutputFilter.getNumOccurrences() == 0 ||
      std::find(AutotuningOutputFilter.begin(), AutotuningOutputFilter.end(),
                Other) != AutotuningOutputFilter.end()) {
    // Add an empty CodeRegion with ModuleID as an tuning opportunity.
    // It could be used to represent a module level code region.
    autotuning::CodeRegion GlobalCR =
        CodeRegion(ModuleID, "none", "all", Other);
    GlobalCR.setHash(llvm::hash_combine(ModuleID, Other));
    NewCRs.insert(GlobalCR);
    LLVM_DEBUG(dbgs() << "Module added as an tuning opportunity: \n"
                      << "  Name: " << GlobalCR.getName() << "\n"
                      << "  Hash: " << GlobalCR.getHash() << "\n"
                      << "\n");
  }

  // Include LLVMParam as an tuning opportunity only if it is specified with
  // -auto-tuning-type-filter.
  if (std::find(AutotuningOutputFilter.begin(), AutotuningOutputFilter.end(),
                LLVMParam) != AutotuningOutputFilter.end())
    NewCRs.insert(CodeRegion(ModuleID, "none", "none", LLVMParam));

  if (std::find(AutotuningOutputFilter.begin(), AutotuningOutputFilter.end(),
                ProgramParam) != AutotuningOutputFilter.end())
    NewCRs.insert(CodeRegion(ModuleID, "none", "none", ProgramParam));

  CRs = NewCRs;
}

bool AutoTuningEngine::applyFunctionFilter(std::string FuncName) {
  if (AutotuningFunctionFilter.getNumOccurrences() == 0)
    return true;

  for (std::string FunctionFilter : AutotuningFunctionFilter)
    if (FuncName == FunctionFilter)
      return true;

  return false;
}

void AutoTuningEngine::initContainer(Container *Container,
                                     const std::string &PassName,
                                     const StringRef FuncName,
                                     bool AddOpportunity,
                                     unsigned int Invocation) {
  if (Enabled) {
    if (!isTuningAllowedForType(convertPassToType(PassName)) &&
        !(isGenerateOutput() &&
          AutotuningOutputFilter.getNumOccurrences() == 0))
      return;

    if (!applyFunctionFilter(FuncName.str()))
      return;

    // The attributes of a Container could potentially change overtime even with
    // the same pass if the associated pass is invoked multiple times at
    // different places in the pipeline. Therefore, we need to initCodeRegion
    // every time when this function is called to ensure the CodeRegion with the
    // latest information will be added as tuning opportunities.
    Container->initCodeRegion();
    if (Container->CR.getType() == autotuning::CodeRegionType::Invalid)
      return;

    uint64_t hash = Container->computeStructuralHash();
    CodeRegion &OppCR = Container->CR;
    if (GenerateOutput) {
      if (OppCR.getSize() < SizeThreshold)
        return;
      if (ExcludeColdCodeRegion && OppCR.isCold()) {
        LLVM_DEBUG(dbgs() << "Skip CodeRegion with cold function "
                          << OppCR.getFuncName() << "\n");
        return;
      }
      if (HotCodeRegionOnly && !OppCR.isHot()) {
        LLVM_DEBUG(dbgs() << "Skip CodeRegion with "
                          << OppCR.getHotnessAsString() << " function "
                          << OppCR.getFuncName() << "\n");
        return;
      }
    }
    OppCR.setPassName(PassName);
    OppCR.setHash(hash);
    OppCR.setInvocation(Invocation);
    OppCR.Initialized = true;
    if (AddOpportunity)
      addOpportunity(OppCR);
  }
}

bool AutoTuningEngine::shouldRunOptPass(std::string Filename,
                                        std::string Pass) {
  return OppPassList.count(Filename) ? OppPassList[Filename].count(Pass)
                                     : false;
}

Error AutoTuningEngine::init(const std::string &Module) {
  ParseInput = false;
  if (std::optional<std::string> MaybePath =
          llvm::sys::Process::GetEnv("AUTOTUNE_INPUT")) {
    InputFile = *MaybePath;
    ParseInput = true;
  } else if (InputFile.getNumOccurrences() > 0) {
    ParseInput = true;
  }

  GenerateOutput = false;
  if (OutputOppDir.getNumOccurrences() > 0)
    GenerateOutput = true;

  // Invocation of any of the following command line options
  // (auto-tuning-input and auto-tuning-opp) or env variable
  // AUTOTUNE_ALL_INPUT can enable auto-tuning mode.
  if (ParseInput || GenerateOutput) {
    Enabled = true;
    // Generate absolute path and remove the base directory (if available).
    // A relative path will be used as (coarse-grain) code region name.
    llvm::SmallString<128> ModuleVec = StringRef(Module);
    llvm::sys::fs::make_absolute(ModuleVec);
    if (ProjectDir.size() && ModuleVec.startswith(ProjectDir))
      ModuleID = ModuleVec.substr(ProjectDir.size()).str();
    else
      ModuleID = std::string(ModuleVec);
  }

  // Initialization of map to be used for pass-name to CodeRegionType
  // conversion.
  PTTMap = {{"loop-unroll", Loop},
            {"loop-vectorize", Loop},
            {"inline", CallSite},
            {"machine-scheduler", MachineBasicBlock},
            {"switch-lowering", Switch},
            {"autotuning-dump", Function}};

  if (ParseInput) {
    // Currently we only support yaml format for input.
    if (Error E = AutoTuningRemarkManager::read(*this, InputFile, "yaml")) {
      errs() << "Error parsing auto-tuning input.\n";
      return E;
    } else {
      LLVM_DEBUG(dbgs() << "AutoTuningEngine is initialized.\n"
                        << " Size of ParamTable: " << this->ParamTable.size()
                        << "\n");
      if (LLVMParams.size())
        LLVM_DEBUG(dbgs() << "AutoTuner: LLVMParams applied.");
      if (ProgramParams.size())
        LLVM_DEBUG(dbgs() << "AutoTuner: ProgramParams applied.\n");
    }
  }

  for (auto CRType : AutotuningOutputFilter)
    CodeRegionFilterTypes.insert(CRType);

  if (GenerateOutput) {
    switch (AutoTuningCompileMode) {
    case CoarseGrain: {
      bool Valid = false;
      if (AutotuningOutputFilter.getNumOccurrences() > 0) {
        Valid = true;
        for (auto CRType : AutotuningOutputFilter)
          if (CRType != LLVMParam) {
            Valid = false;
            break;
          }
      }
      if (!Valid) {
        AutoTuningCompileMode = Inactive;
        errs() << "AutoTunerCompile: Code region type filtering does not match"
                  " with incremental compilation option.\n"
                  "Disabling incremental compilation.\n";
      }
      break;
    }
    case FineGrain: {
      bool Valid = false;
      if (AutotuningOutputFilter.getNumOccurrences() > 0) {
        Valid = true;
        for (auto CRType : AutotuningOutputFilter) {
          if (CRType != Loop && CRType != CallSite && CRType != Function) {
            Valid = false;
            break;
          }
        }
      }
      if (!Valid) {
        AutoTuningCompileMode = Inactive;
        errs() << "AutoTunerCompile: Code region type filtering does not match"
                  "with incremental compilation option.\n"
                  "Disabling incremental compilation.\n";
      }
      break;
    }
    case Basic:
    case Inactive:
      break;
    default:
      llvm_unreachable("AutoTuningCompile: Unknown AutoTuner Incremental "
                       "Compilation mode.\n");
    }
  }

  MLEnabled = (CFGNumber.getNumOccurrences() > 0);
  if (EnableAutoTuningDump || MLEnabled)
    DumpEnabled = true;
  return Error::success();
}

llvm::Expected<int> AutoTuningEngine::getConfigNumber() {
  if (!isMLEnabled()) {
    std::string errorMsg =
        "No Autotuner configuration specified; ML guidance is unavailable.";
    return createStringError(inconvertibleErrorCode(), errorMsg);
  } else
    return CFGNumber;
}

Error AutoTuningEngine::finalize() {
  if (OutputOppDir.getNumOccurrences() > 0) {
    // Apply filters.
    applyOppFilters(TuningOpps);
    if (!TuningOpps.empty()) {
      if (Error E = AutoTuningRemarkManager::dump(
              *this, OutputOppDir, OutputFormat, RemarksPasses)) {
        errs() << "Error generating auto-tuning opportunities.\n";
        return E;
      }
    }

    // Clear these two global lists when ending the auto-tuning
    // in case of redundant information
    TuningOpps.clear();
  }
  return Error::success();
}

template <typename T>
bool AutoTuningEngine::lookUpGlobalParams(const std::string &ParamsName,
                                          T &Value) const {
  bool Found = GlobalParams.findByName(ParamsName, Value);
  if (Found) {
    LLVM_DEBUG(dbgs() << "Global Variable " << ParamsName << " is set.\n");
  }
  return Found;
}

template bool
AutoTuningEngine::lookUpGlobalParams<int>(const std::string &ParamsName,
                                          int &Value) const;
template bool
AutoTuningEngine::lookUpGlobalParams<bool>(const std::string &ParamsName,
                                           bool &Value) const;
template bool
AutoTuningEngine::lookUpGlobalParams<std::string>(const std::string &ParamsName,
                                                  std::string &Value) const;
template bool AutoTuningEngine::lookUpGlobalParams<std::vector<std::string>>(
    const std::string &ParamsName, std::vector<std::string> &Value) const;

class AutoTuningEngine Engine;

} // namespace autotuning

#endif
