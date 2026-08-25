//===- protean.cpp - The Simulated Annealing Optimizer --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2024, Huawei Technologies Co., Ltd. All rights reserved.
//
//===----------------------------------------------------------------------===//
//
// Optimizations may be specified an arbitrary number of times on the command
// line, They are run in the order specified.
//
//===----------------------------------------------------------------------===//

#include "IRAnalysis/PhaseOrder.h"
#include "IRAnalysis/SimulatedAnnealing.h"
#include "NewPMDriver.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/RegionPass.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LLVMRemarkStreamer.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/LegacyPassNameParser.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/InitializePasses.h"
#include "llvm/LinkAllIR.h"
#include "llvm/LinkAllPasses.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Remarks/HotnessThresholdParser.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/IPO/WholeProgramDevirt.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Debugify.h"
#include <algorithm>
#include <iomanip>
#include <memory>
#include <filesystem>
#include <optional>
#include <string>

using namespace llvm;
using namespace opt_tool;

#undef DEBUG_TYPE
#define DEBUG_TYPE "protean"

static codegen::RegisterCodeGenFlags CFG;

// In "llvm/lib/Passes/PassBuilderPipelines.cpp"
extern cl::opt<bool> UseProteanInitialPasses;

// The OptimizationList is automatically populated with registered Passes by the
// PassNameParser.
static cl::list<const PassInfo *, bool, PassNameParser> PassList(cl::desc(
    "Optimizations available (use '-passes=' for the new pass manager)"));

static cl::opt<bool> EnableLegacyPassManager(
    "bugpoint-enable-legacy-pm",
    cl::desc(
        "Enable the legacy pass manager. This is strictly for bugpoint "
        "due to it not working with the new PM, please do not use otherwise."),
    cl::init(false));

// This flag specifies a textual description of the optimization pass pipeline
// to run over the module. This flag switches opt to use the new pass manager
// infrastructure, completely disabling all of the flags specific to the old
// pass management.
static cl::opt<std::string> PassPipeline(
    "passes",
    cl::desc(
        "A textual description of the pass pipeline. To have analysis passes "
        "available before a certain pass, add 'require<foo-analysis>'."));
static cl::alias PassPipeline2("p", cl::aliasopt(PassPipeline),
                               cl::desc("Alias for -passes"));

static cl::opt<bool> PrintPasses("print-passes",
                                 cl::desc("Print available passes that can be "
                                          "specified in -passes=foo and exit"));

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input bitcode file>"),
                                          cl::init("-"),
                                          cl::value_desc("filename"));

static cl::opt<std::string> OutputFilename("o",
                                           cl::desc("Override output filename"),
                                           cl::value_desc("filename"));

static cl::opt<bool> Force("f", cl::desc("Enable binary output on terminals"));

static cl::opt<bool> NoOutput("disable-output",
                              cl::desc("Do not write result bitcode file"),
                              cl::Hidden);

static cl::opt<bool> OutputAssembly("S",
                                    cl::desc("Write output as LLVM assembly"));

static cl::opt<bool>
    OutputThinLTOBC("thinlto-bc",
                    cl::desc("Write output as ThinLTO-ready bitcode"));

static cl::opt<bool>
    SplitLTOUnit("thinlto-split-lto-unit",
                 cl::desc("Enable splitting of a ThinLTO LTOUnit"));

static cl::opt<bool>
    UnifiedLTO("unified-lto",
               cl::desc("Use unified LTO piplines. Ignored unless -thinlto-bc "
                        "is also specified."),
               cl::Hidden, cl::init(false));

static cl::opt<std::string> ThinLinkBitcodeFile(
    "thin-link-bitcode-file", cl::value_desc("filename"),
    cl::desc(
        "A file in which to write minimized bitcode for the thin link only"));

static cl::opt<bool> NoVerify("disable-verify",
                              cl::desc("Do not run the verifier"), cl::Hidden);

static cl::opt<bool> NoUpgradeDebugInfo("disable-upgrade-debug-info",
                                        cl::desc("Generate invalid output"),
                                        cl::ReallyHidden);

static cl::opt<bool> VerifyEach("verify-each",
                                cl::desc("Verify after each transform"));

static cl::opt<bool>
    DisableDITypeMap("disable-debug-info-type-map",
                     cl::desc("Don't use a uniquing type map for debug info"));

static cl::opt<bool>
    StripDebug("strip-debug",
               cl::desc("Strip debugger symbol info from translation unit"));

static cl::opt<bool>
    StripNamedMetadata("strip-named-metadata",
                       cl::desc("Strip module-level named metadata"));

static cl::opt<bool>
    OptLevelO0("O0", cl::desc("Optimization level 0. Similar to clang -O0. "
                              "Same as -passes='default<O0>'"));

static cl::opt<bool>
    OptLevelO1("O1", cl::desc("Optimization level 1. Similar to clang -O1. "
                              "Same as -passes='default<O1>'"));

static cl::opt<bool>
    OptLevelO2("O2", cl::desc("Optimization level 2. Similar to clang -O2. "
                              "Same as -passes='default<O2>'"));

static cl::opt<bool>
    OptLevelOs("Os", cl::desc("Like -O2 but size-conscious. Similar to clang "
                              "-Os. Same as -passes='default<Os>'"));

static cl::opt<bool> OptLevelOz(
    "Oz",
    cl::desc("Like -O2 but optimize for code size above all else. Similar to "
             "clang -Oz. Same as -passes='default<Oz>'"));

static cl::opt<bool>
    OptLevelO3("O3", cl::desc("Optimization level 3. Similar to clang -O3. "
                              "Same as -passes='default<O3>'"));

static cl::opt<unsigned> CodeGenOptLevelCL(
    "codegen-opt-level",
    cl::desc("Override optimization level for codegen hooks, legacy PM only"));

static cl::opt<std::string>
    TargetTriple("mtriple", cl::desc("Override target triple for module"));

static cl::opt<bool> EmitSummaryIndex("module-summary",
                                      cl::desc("Emit module summary index"),
                                      cl::init(false));

static cl::opt<bool> EmitModuleHash("module-hash", cl::desc("Emit module hash"),
                                    cl::init(false));

static cl::opt<bool>
    DisableSimplifyLibCalls("disable-simplify-libcalls",
                            cl::desc("Disable simplify-libcalls"));

static cl::list<std::string> DisableBuiltins(
    "disable-builtin",
    cl::desc("Disable specific target library builtin function"));

static cl::opt<bool> EnableDebugify(
    "enable-debugify",
    cl::desc(
        "Start the pipeline with debugify and end it with check-debugify"));

static cl::opt<bool> VerifyDebugInfoPreserve(
    "verify-debuginfo-preserve",
    cl::desc("Start the pipeline with collecting and end it with checking of "
             "debug info preservation."));

static cl::opt<std::string> ClDataLayout("data-layout",
                                         cl::desc("data layout string to use"),
                                         cl::value_desc("layout-string"),
                                         cl::init(""));

static cl::opt<bool> PreserveBitcodeUseListOrder(
    "preserve-bc-uselistorder",
    cl::desc("Preserve use-list order when writing LLVM bitcode."),
    cl::init(true), cl::Hidden);

static cl::opt<bool> PreserveAssemblyUseListOrder(
    "preserve-ll-uselistorder",
    cl::desc("Preserve use-list order when writing LLVM assembly."),
    cl::init(false), cl::Hidden);

static cl::opt<bool> RunTwice("run-twice",
                              cl::desc("Run all passes twice, re-using the "
                                       "same pass manager (legacy PM only)."),
                              cl::init(false), cl::Hidden);

static cl::opt<bool> DiscardValueNames(
    "discard-value-names",
    cl::desc("Discard names from Value (other than GlobalValue)."),
    cl::init(false), cl::Hidden);

static cl::opt<bool> TimeTrace("time-trace", cl::desc("Record time trace"));

static cl::opt<unsigned> TimeTraceGranularity(
    "time-trace-granularity",
    cl::desc(
        "Minimum time granularity (in microseconds) traced by time profiler"),
    cl::init(500), cl::Hidden);

static cl::opt<std::string>
    TimeTraceFile("time-trace-file",
                  cl::desc("Specify time trace file destination"),
                  cl::value_desc("filename"));

static cl::opt<bool> RemarksWithHotness(
    "pass-remarks-with-hotness",
    cl::desc("With PGO, include profile count in optimization remarks"),
    cl::Hidden);

static cl::opt<std::optional<uint64_t>, false, remarks::HotnessThresholdParser>
    RemarksHotnessThreshold(
        "pass-remarks-hotness-threshold",
        cl::desc("Minimum profile count required for "
                 "an optimization remark to be output. "
                 "Use 'auto' to apply the threshold from profile summary"),
        cl::value_desc("N or 'auto'"), cl::init(0), cl::Hidden);

static cl::opt<std::string>
    RemarksFilename("pass-remarks-output",
                    cl::desc("Output filename for pass remarks"),
                    cl::value_desc("filename"));

static cl::opt<std::string>
    RemarksPasses("pass-remarks-filter",
                  cl::desc("Only record optimization remarks from passes whose "
                           "names match the given regular expression"),
                  cl::value_desc("regex"));

static cl::opt<std::string> RemarksFormat(
    "pass-remarks-format",
    cl::desc("The format used for serializing remarks (default: YAML)"),
    cl::value_desc("format"), cl::init("yaml"));

static cl::list<std::string>
    PassPlugins("load-pass-plugin",
                cl::desc("Load passes from plugin library"));

//===----------------------------------------------------------------------===//
// Protean Compiler Hyperparameters
//

static cl::opt<CoolingType> CoolingSchedule(
    "cooling", cl::init(Geometric),
    cl::desc("Choose Cooling Schedule for Simulated Annealing"),
    cl::values(clEnumVal(Geometric, "Determine cost based on file size"),
               clEnumVal(Linear, "Determine cost based on instruction count")));

static cl::opt<unsigned> MaxIterations(
    "max-iterations",
    cl::desc("Specify Maximum Iterations for Simulated Annealing"),
    cl::init(100));

static cl::opt<unsigned>
    RngVal("rng-val", cl::desc("Specify RNG Val for Simulated Annealing"),
           cl::init(123));

static cl::opt<double> MaxTemperature(
    "max-temperature",
    cl::desc("Specify Maximum Temperature for Simulated Annealing"),
    cl::init(100));

static cl::opt<double> MinTemperature(
    "min-temperature",
    cl::desc("Specify Minimum Temperature for Simulated Annealing"),
    cl::init(1));

static cl::opt<unsigned> InitialSampleSize(
    "initial-sample-size",
    cl::desc(
        "Specify Number of Initial Random Samples for Simulated Annealing"),
    cl::init(20));

static cl::opt<double>
    MutationRate("mutation-rate",
                 cl::desc("Specify Mutation Rate for Genetic Recommender"),
                 cl::init(0.05));

static cl::opt<double>
    CrossoverRate("crossover-rate",
                  cl::desc("Specify Crossover Rate for Genetic Recommender"),
                  cl::init(0.95));

static cl::opt<double>
    PopulationSize("population-size",
                   cl::desc("Specify Population Size for Genetic Recommender"),
                   cl::init(20));

static cl::opt<CrossoverFunction> CrossoverType(
    "crossover-type", cl::init(SinglePoint),
    cl::desc("Choose crossover method type for Genetic Recommender"),
    cl::values(clEnumVal(SinglePoint, "Single point crossover method"),
               clEnumVal(DoublePoint, "Double point crossover method"),
               clEnumVal(Uniform, "Uniform crossover method")));

static cl::opt<MutationFunction> MutationType(
    "mutation-type", cl::init(FlipOne),
    cl::desc("Choose mutation method type for Genetic Recommender"),
    cl::values(clEnumVal(FlipOne, "Mutate one sequence mutation method"),
               clEnumVal(SwapTwo, "Swap two sequences mutation method")));

static cl::opt<bool> UseAOTModel("enable-protean-aot", cl::init(true),
                                 cl::desc("Specify whether to use AOT"));

static cl::opt<IRCostFunction> CostType(
    "cost-type", cl::init(IRAnalysis),
    cl::desc("Choose IR Cost Function used for Simulated Annealing"),
    cl::values(
        clEnumVal(FileSize, "Determine cost based on file size"),
        clEnumVal(InstCount, "Determine cost based on instruction count"),
        clEnumVal(IRAnalysis, "Determine cost based on IR Analyzer"),
        clEnumVal(MCA, "Determine cost based on llvm-mca (cycle count)")));

static cl::opt<bool> ProteanOutputTable(
    "protean-output-table",
    cl::desc("Output Table of Simulated Annealing information"),
    cl::init(false));

static cl::opt<bool>
    UseProteanCollect("use-protean-collect",
                      cl::desc("Use protean collect features for IR Analyzer"),
                      cl::init(false));

static cl::opt<bool>
    ModLevelIPC("module-level-ipc",
                cl::desc("Enable IPC for module level shared memory"),
                cl::init(false));

//===----------------------------------------------------------------------===//
// CodeGen-related helper functions.
//

static CodeGenOptLevel GetCodeGenOptLevel() {
  return static_cast<CodeGenOptLevel>(unsigned(CodeGenOptLevelCL));
}

struct TimeTracerRAII {
  TimeTracerRAII(StringRef ProgramName) {
    if (TimeTrace)
      timeTraceProfilerInitialize(TimeTraceGranularity, ProgramName);
  }
  ~TimeTracerRAII() {
    if (TimeTrace) {
      if (auto E = timeTraceProfilerWrite(TimeTraceFile, OutputFilename)) {
        handleAllErrors(std::move(E), [&](const StringError &SE) {
          errs() << SE.getMessage() << "\n";
        });
        return;
      }
      timeTraceProfilerCleanup();
    }
  }
};

// For use in NPM transition. Currently this contains most codegen-specific
// passes. Remove passes from here when porting to the NPM.
// TODO: use a codegen version of PassRegistry.def/PassBuilder::is*Pass() once
// it exists.
static bool shouldPinPassToLegacyPM(StringRef Pass) {
  std::vector<StringRef> PassNameExactToIgnore = {
      "nvvm-reflect",
      "nvvm-intr-range",
      "amdgpu-simplifylib",
      "amdgpu-image-intrinsic-opt",
      "amdgpu-usenative",
      "amdgpu-promote-alloca",
      "amdgpu-promote-alloca-to-vector",
      "amdgpu-lower-kernel-attributes",
      "amdgpu-propagate-attributes-early",
      "amdgpu-propagate-attributes-late",
      "amdgpu-unify-metadata",
      "amdgpu-printf-runtime-binding",
      "amdgpu-always-inline"};
  if (llvm::is_contained(PassNameExactToIgnore, Pass))
    return false;

  std::vector<StringRef> PassNamePrefix = {
      "x86-",    "xcore-", "wasm-",  "systemz-", "ppc-",    "nvvm-",
      "nvptx-",  "mips-",  "lanai-", "hexagon-", "bpf-",    "avr-",
      "thumb2-", "arm-",   "si-",    "gcn-",     "amdgpu-", "aarch64-",
      "amdgcn-", "polly-", "riscv-", "dxil-"};
  std::vector<StringRef> PassNameContain = {"ehprepare"};
  std::vector<StringRef> PassNameExact = {
      "safe-stack",
      "cost-model",
      "codegenprepare",
      "interleaved-load-combine",
      "unreachableblockelim",
      "verify-safepoint-ir",
      "atomic-expand",
      "expandvp",
      "mve-tail-predication",
      "interleaved-access",
      "global-merge",
      "pre-isel-intrinsic-lowering",
      "expand-reductions",
      "indirectbr-expand",
      "generic-to-nvvm",
      "expandmemcmp",
      "loop-reduce",
      "lower-amx-type",
      "pre-amx-config",
      "lower-amx-intrinsics",
      "polyhedral-info",
      "print-polyhedral-info",
      "replace-with-veclib",
      "jmc-instrument",
      "dot-regions",
      "dot-regions-only",
      "view-regions",
      "view-regions-only",
      "select-optimize",
      "expand-large-div-rem",
      "structurizecfg",
      "fix-irreducible",
      "expand-large-fp-convert",
      "callbrprepare",
  };
  for (const auto &P : PassNamePrefix)
    if (Pass.starts_with(P))
      return true;
  for (const auto &P : PassNameContain)
    if (Pass.contains(P))
      return true;
  return llvm::is_contained(PassNameExact, Pass);
}

// For use in NPM transition.
static bool shouldForceLegacyPM() {
  for (const auto &P : PassList) {
    StringRef Arg = P->getPassArgument();
    if (shouldPinPassToLegacyPM(Arg))
      return true;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// main for opt
//
int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  // Enable debug stream buffering.
  EnableDebugBuffering = true;

  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();

  // Initialize passes
  PassRegistry &Registry = *PassRegistry::getPassRegistry();
  initializeCore(Registry);
  initializeScalarOpts(Registry);
  initializeVectorization(Registry);
  initializeIPO(Registry);
  initializeAnalysis(Registry);
  initializeTransformUtils(Registry);
  initializeInstCombine(Registry);
  initializeTarget(Registry);
  // For codegen passes, only passes that do IR to IR transformation are
  // supported.
  initializeExpandLargeDivRemLegacyPassPass(Registry);
  initializeExpandLargeFpConvertLegacyPassPass(Registry);
  initializeScalarizeMaskedMemIntrinLegacyPassPass(Registry);
  initializeSelectOptimizePass(Registry);
  initializeCallBrPreparePass(Registry);
  initializeCodeGenPrepareLegacyPassPass(Registry);
  initializeAtomicExpandLegacyPass(Registry);
  initializeWinEHPreparePass(Registry);
  initializeDwarfEHPrepareLegacyPassPass(Registry);
  initializeSafeStackLegacyPassPass(Registry);
  initializeSjLjEHPreparePass(Registry);
  initializePreISelIntrinsicLoweringLegacyPassPass(Registry);
  initializeGlobalMergePass(Registry);
  initializeInterleavedLoadCombinePass(Registry);
  initializeInterleavedAccessPass(Registry);
  initializeUnreachableBlockElimLegacyPassPass(Registry);
  initializeExpandReductionsPass(Registry);
  initializeExpandVectorPredicationPass(Registry);
  initializeWasmEHPreparePass(Registry);
  initializeWriteBitcodePassPass(Registry);
  initializeReplaceWithVeclibLegacyPass(Registry);
  initializeJMCInstrumenterPass(Registry);

  SmallVector<PassPlugin, 1> PluginList;
  PassPlugins.setCallback([&](const std::string &PluginPath) {
    auto Plugin = PassPlugin::Load(PluginPath);
    if (!Plugin) {
      errs() << "Failed to load passes from '" << PluginPath
             << "'. Request ignored.\n";
      return;
    }
    PluginList.emplace_back(Plugin.get());
  });

  // Register the Target and CPU printer for --version.
  cl::AddExtraVersionPrinter(sys::printDefaultTargetAndDetectedCPU);

  cl::ParseCommandLineOptions(
      argc, argv, "llvm .bc -> .bc modular optimizer and analysis printer\n");

  LLVMContext Context;

  // TODO: remove shouldForceLegacyPM().
  const bool UseNPM = (!EnableLegacyPassManager && !shouldForceLegacyPM()) ||
                      PassPipeline.getNumOccurrences() > 0;

  if (UseNPM && !PassList.empty()) {
    errs() << "The `opt -passname` syntax for the new pass manager is "
              "not supported, please use `opt -passes=<pipeline>` (or the `-p` "
              "alias for a more concise version).\n";
    errs() << "See https://llvm.org/docs/NewPassManager.html#invoking-opt "
              "for more details on the pass pipeline syntax.\n\n";
    return 1;
  }

  if (!UseNPM && PluginList.size()) {
    errs() << argv[0] << ": " << PassPlugins.ArgStr
           << " specified with legacy PM.\n";
    return 1;
  }

  // FIXME: once the legacy PM code is deleted, move runPassPipeline() here and
  // construct the PassBuilder before parsing IR so we can reuse the same
  // PassBuilder for print passes.
  if (PrintPasses) {
    printPasses(outs());
    return 0;
  }

  TimeTracerRAII TimeTracer(argv[0]);

  SMDiagnostic Err;

  Context.setDiscardValueNames(DiscardValueNames);
  if (!DisableDITypeMap)
    Context.enableDebugTypeODRUniquing();

  Expected<std::unique_ptr<ToolOutputFile>> RemarksFileOrErr =
      setupLLVMOptimizationRemarks(Context, RemarksFilename, RemarksPasses,
                                   RemarksFormat, RemarksWithHotness,
                                   RemarksHotnessThreshold);
  if (Error E = RemarksFileOrErr.takeError()) {
    errs() << toString(std::move(E)) << '\n';
    return 1;
  }
  std::unique_ptr<ToolOutputFile> RemarksFile = std::move(*RemarksFileOrErr);

  // Load the input module...
  auto SetDataLayout = [](StringRef, StringRef) -> std::optional<std::string> {
    if (ClDataLayout.empty())
      return std::nullopt;
    return ClDataLayout;
  };
  std::unique_ptr<Module> M;
  if (NoUpgradeDebugInfo)
    M = parseAssemblyFileWithIndexNoUpgradeDebugInfo(
            InputFilename, Err, Context, nullptr, SetDataLayout)
            .Mod;
  else
    M = parseIRFile(InputFilename, Err, Context,
                    ParserCallbacks(SetDataLayout));

  if (!M) {
    Err.print(argv[0], errs());
    return 1;
  }

  // Strip debug info before running the verifier.
  if (StripDebug)
    StripDebugInfo(*M);

  // Erase module-level named metadata, if requested.
  if (StripNamedMetadata) {
    while (!M->named_metadata_empty()) {
      NamedMDNode *NMD = &*M->named_metadata_begin();
      M->eraseNamedMetadata(NMD);
    }
  }

  // If we are supposed to override the target triple or data layout, do so now.
  if (!TargetTriple.empty())
    M->setTargetTriple(Triple::normalize(TargetTriple));

  // Immediately run the verifier to catch any problems before starting up the
  // pass pipelines.  Otherwise we can crash on broken code during
  // doInitialization().
  if (!NoVerify && verifyModule(*M, &errs())) {
    errs() << argv[0] << ": " << InputFilename
           << ": error: input module is broken!\n";
    return 1;
  }

  // Enable testing of whole program devirtualization on this module by invoking
  // the facility for updating public visibility to linkage unit visibility when
  // specified by an internal option. This is normally done during LTO which is
  // not performed via opt.
  updateVCallVisibilityInModule(
      *M,
      /*WholeProgramVisibilityEnabledInLTO=*/false,
      // FIXME: These need linker information via a
      // TBD new interface.
      /*DynamicExportSymbols=*/{},
      /*ValidateAllVtablesHaveTypeInfos=*/false,
      /*IsVisibleToRegularObj=*/[](StringRef) { return true; });

  // Figure out what stream we are supposed to write to...
  std::unique_ptr<ToolOutputFile> Out;
  std::unique_ptr<ToolOutputFile> ThinLinkOut;
  if (NoOutput) {
    if (!OutputFilename.empty())
      errs() << "WARNING: The -o (output filename) option is ignored when\n"
                "the --disable-output option is used.\n";
  } else {
    // Default to standard output.
    if (OutputFilename.empty() ||
        OutputFilename.find_last_of(".") == std::string::npos)
      OutputFilename = "-";

    std::error_code EC;
    sys::fs::OpenFlags Flags =
        OutputAssembly ? sys::fs::OF_TextWithCRLF : sys::fs::OF_None;
    Out.reset(new ToolOutputFile(OutputFilename, EC, Flags));
    if (EC) {
      errs() << EC.message() << '\n';
      return 1;
    }

    if (!ThinLinkBitcodeFile.empty()) {
      ThinLinkOut.reset(
          new ToolOutputFile(ThinLinkBitcodeFile, EC, sys::fs::OF_None));
      if (EC) {
        errs() << EC.message() << '\n';
        return 1;
      }
    }
  }

  Triple ModuleTriple(M->getTargetTriple());
  std::string CPUStr, FeaturesStr;
  std::unique_ptr<TargetMachine> TM;
  if (ModuleTriple.getArch()) {
    CPUStr = codegen::getCPUStr();
    FeaturesStr = codegen::getFeaturesStr();
    Expected<std::unique_ptr<TargetMachine>> ExpectedTM =
        codegen::createTargetMachineForTriple(ModuleTriple.str(),
                                              GetCodeGenOptLevel());
    if (auto E = ExpectedTM.takeError()) {
      errs() << argv[0] << ": WARNING: failed to create target machine for '"
             << ModuleTriple.str() << "': " << toString(std::move(E)) << "\n";
    } else {
      TM = std::move(*ExpectedTM);
    }
  } else if (ModuleTriple.getArchName() != "unknown" &&
             ModuleTriple.getArchName() != "") {
    errs() << argv[0] << ": unrecognized architecture '"
           << ModuleTriple.getArchName() << "' provided.\n";
    return 1;
  }

  // Override function attributes based on CPUStr, FeaturesStr, and command line
  // flags.
  codegen::setFunctionAttributes(CPUStr, FeaturesStr, *M);

  // If the output is set to be emitted to standard out, and standard out is a
  // console, print out a warning message and refuse to do it.  We don't
  // impress anyone by spewing tons of binary goo to a terminal.
  if (!Force && !NoOutput && !OutputAssembly)
    if (CheckBitcodeOutputToConsole(Out->os()))
      NoOutput = true;

  if (OutputThinLTOBC) {
    M->addModuleFlag(Module::Error, "EnableSplitLTOUnit", SplitLTOUnit);
    if (UnifiedLTO)
      M->addModuleFlag(Module::Error, "UnifiedLTO", 1);
  }

  // Add an appropriate TargetLibraryInfo pass for the module's triple.
  TargetLibraryInfoImpl TLII(ModuleTriple);

  // The -disable-simplify-libcalls flag actually disables all builtin optzns.
  if (DisableSimplifyLibCalls)
    TLII.disableAllFunctions();
  else {
    // Disable individual builtin functions in TargetLibraryInfo.
    LibFunc F;
    for (auto &FuncName : DisableBuiltins)
      if (TLII.getLibFunc(FuncName, F))
        TLII.setUnavailable(F);
      else {
        errs() << argv[0] << ": cannot disable nonexistent builtin function "
               << FuncName << '\n';
        return 1;
      }
  }

  if (UseNPM) {
    if (legacy::debugPassSpecified()) {
      errs() << "-debug-pass does not work with the new PM, either use "
                "-debug-pass-manager, or use the legacy PM\n";
      return 1;
    }
    auto NumOLevel = OptLevelO0 + OptLevelO1 + OptLevelO2 + OptLevelO3 +
                     OptLevelOs + OptLevelOz;
    if (NumOLevel > 1) {
      errs() << "Cannot specify multiple -O#\n";
      return 1;
    }
    if (NumOLevel > 0 && (PassPipeline.getNumOccurrences() > 0)) {
      errs() << "Cannot specify -O# and --passes=/--foo-pass, use "
                "-passes='default<O#>,other-pass'\n";
      return 1;
    }
    std::string Pipeline = PassPipeline;

    if (OptLevelO0)
      Pipeline = "default<O0>";
    if (OptLevelO1)
      Pipeline = "default<O1>";
    if (OptLevelO2)
      Pipeline = "default<O2>";
    if (OptLevelO3)
      Pipeline = "default<O3>";
    if (OptLevelOs)
      Pipeline = "default<Os>";
    if (OptLevelOz)
      Pipeline = "default<Oz>";

    // If protean is enabled goto the Simulated Annealing process:
    //   The Simulated Annealing process first creates an random recipe then it
    //   creates a forked process.
    //     (1) The parent process wait's for the child process and continue the
    //     Simulated Annealing main loop.
    //     (2) The child process will return back here and modify
    //     the Pipeline according to the randomly generated recipe.
    // At the end of the Simulated Annealing loop the parent process will just
    // exit/return from here.
    if (UseProteanInitialPasses.getValue()) {
      LLVM_DEBUG(dbgs() << "ProteanCompiler :: Beginning Simulated Annealing for Module "
             << M->getName() << "\n");
      std::string Recipes;
      if (OutputFilename == "-") {
        errs() << "Specify an output.o file with -o (e.g., -o output.bc) to "
                  "activate Simulated Annealing\n";
        return 1;
      }

      SimulatedAnnealingProtean SAProtean = SimulatedAnnealingProtean(
          RngVal, CoolingSchedule, MaxTemperature, MinTemperature,
          MaxIterations, CostType, OutputFilename, ProteanOutputTable,
          UseProteanCollect, ModLevelIPC, UseAOTModel, InitialSampleSize,
          MutationRate, CrossoverRate, PopulationSize, CrossoverType,
          MutationType);
      PhaseOrderGeneratorBase::PMap PassMap = createPassMap();
      if (ProteanOutputTable) {
        errs() << "Protean is performing SA on module: " << M->getName() << "\n";
        std::stringstream ss;
        ss << std::setw(9) << "Iteration  ";
        ss << std::setw(20) << "Current State";
        ss << std::setw(20) << "Next State";
        ss << std::setw(20) << "Best State";
        ss << std::setw(20) << "Current Cost";
        ss << std::setw(20) << "Next Cost";
        ss << std::setw(20) << "Best Cost";
        ss << std::setw(20) << "Temperature\n";
        dbgs() << ss.str();
      }

      SAProtean.run();
      if (SAProtean.getFinished()) {
        LLVM_DEBUG(dbgs() << "ProteanCompiler :: Simulated Annealing finished running for "
                  "Module "
               << M->getName() << "\n");
        LLVM_DEBUG(dbgs() << "The final recipe accepted is: "
                          << PhaseOrderGeneratorBase::recipesToPasses(
                                 SAProtean.getFinalState(), PassMap)
                          << "\n");
        std::string RecipeStr =
            PhaseOrderGeneratorBase::recipesToString(SAProtean.getFinalState());
        std::error_code EC;

        // Find the best recipe and copy it to the appropriate file names.
        std::string RecipeOutputFilename = OutputFilename;
        RecipeOutputFilename.insert(RecipeOutputFilename.find_last_of("."),
                                    "-" + RecipeStr);
        std::filesystem::copy(RecipeOutputFilename, OutputFilename.getValue(),
                              EC);
        if (EC) {
          errs() << EC.message() << '\n';
          return 1;
        }

        if (!ThinLinkBitcodeFile.empty()) {
          std::string RecipeThinLinkBitcodeFile = ThinLinkBitcodeFile;
          RecipeThinLinkBitcodeFile.insert(
              RecipeThinLinkBitcodeFile.find_last_of("."), "-" + RecipeStr);

          std::filesystem::copy(RecipeThinLinkBitcodeFile,
                                ThinLinkBitcodeFile.getValue(), EC);
          if (EC) {
            errs() << EC.message() << '\n';
            return 1;
          }
        }

        // Keep the files to prevent deletion.
        if (Out.get())
          Out.get()->keep();
        if (ThinLinkOut.get())
          ThinLinkOut.get()->keep();
        if (RemarksFile.get())
          RemarksFile.get()->keep();
        return 0;
      } else {
        // Reset the outputfile name for each recipe.
        // NOTE: For now we don't care about the remark files.
        // TODO: Need the Simulated Annealing to keep track of already visited
        // states thus we do reuse the same recipe in previous iterations.
        std::string RecipeStr =
            PhaseOrderGeneratorBase::recipesToString(SAProtean.getCurState());

        std::error_code EC;
        sys::fs::OpenFlags Flags =
            OutputAssembly ? sys::fs::OF_TextWithCRLF : sys::fs::OF_None;

        // Reset for outputfile
        std::string NewOutputFilename = OutputFilename;
        NewOutputFilename.insert(NewOutputFilename.find_last_of("."),
                                 "-" + RecipeStr);
        Out.reset(new ToolOutputFile(NewOutputFilename, EC, Flags));

        // Reset for ThinLinkBitcodeFile
        if (!ThinLinkBitcodeFile.empty()) {
          std::string NewThinLinkBitcodeFile = ThinLinkBitcodeFile;
          NewThinLinkBitcodeFile.insert(
              NewThinLinkBitcodeFile.find_last_of("."), "-" + RecipeStr);
          ThinLinkOut.reset(
              new ToolOutputFile(ThinLinkBitcodeFile, EC, sys::fs::OF_None));
          if (EC) {
            errs() << EC.message() << '\n';
            return 1;
          }
        }

        Recipes = PhaseOrderGeneratorBase::recipesToPasses(
            SAProtean.getCurState(), PassMap);
      }
      LLVM_DEBUG(dbgs() << "Running Recipe: "
                        << PhaseOrderGeneratorBase::recipesToString(
                               SAProtean.getCurState())
                        << "\n");
      if (!Pipeline.empty())
        Pipeline += ",";
      Pipeline += "annotation2metadata,forceattrs,inferattrs,coro-early";
      if (!Recipes.empty())
        Pipeline += ",";
      Pipeline += Recipes;
      Pipeline += ",protean-collect-features";
      if (UseProteanCollect) {
        LLVM_DEBUG(dbgs() << "ProteanCollectFeature Pass is enabled! " << "\n");
      }
      LLVM_DEBUG(dbgs() << "Pipeline: " << Pipeline << "\n");
    }

    OutputKind OK = OK_NoOutput;
    if (!NoOutput)
      OK = OutputAssembly
               ? OK_OutputAssembly
               : (OutputThinLTOBC ? OK_OutputThinLTOBitcode : OK_OutputBitcode);

    VerifierKind VK = VK_VerifyOut;
    if (NoVerify)
      VK = VK_NoVerifier;
    else if (VerifyEach)
      VK = VK_VerifyEachPass;

    // The user has asked to use the new pass manager and provided a pipeline
    // string. Hand off the rest of the functionality to the new code for that
    // layer.
    return runPassPipeline(argv[0], *M, TM.get(), &TLII, Out.get(),
                           ThinLinkOut.get(), RemarksFile.get(), Pipeline,
                           PluginList, OK, VK, PreserveAssemblyUseListOrder,
                           PreserveBitcodeUseListOrder, EmitSummaryIndex,
                           EmitModuleHash, EnableDebugify,
                           VerifyDebugInfoPreserve, UnifiedLTO)
               ? 0
               : 1;
  }

  if (OptLevelO0 || OptLevelO1 || OptLevelO2 || OptLevelOs || OptLevelOz ||
      OptLevelO3) {
    errs() << "Cannot use -O# with legacy PM.\n";
    return 1;
  }
  if (EmitSummaryIndex) {
    errs() << "Cannot use -module-summary with legacy PM.\n";
    return 1;
  }
  if (EmitModuleHash) {
    errs() << "Cannot use -module-hash with legacy PM.\n";
    return 1;
  }
  if (OutputThinLTOBC) {
    errs() << "Cannot use -thinlto-bc with legacy PM.\n";
    return 1;
  }
  // Create a PassManager to hold and optimize the collection of passes we are
  // about to build. If the -debugify-each option is set, wrap each pass with
  // the (-check)-debugify passes.
  DebugifyCustomPassManager Passes;
  DebugifyStatsMap DIStatsMap;
  DebugInfoPerPass DebugInfoBeforePass;
  if (DebugifyEach) {
    Passes.setDebugifyMode(DebugifyMode::SyntheticDebugInfo);
    Passes.setDIStatsMap(DIStatsMap);
  } else if (VerifyEachDebugInfoPreserve) {
    Passes.setDebugifyMode(DebugifyMode::OriginalDebugInfo);
    Passes.setDebugInfoBeforePass(DebugInfoBeforePass);
    if (!VerifyDIPreserveExport.empty())
      Passes.setOrigDIVerifyBugsReportFilePath(VerifyDIPreserveExport);
  }

  bool AddOneTimeDebugifyPasses =
      (EnableDebugify && !DebugifyEach) ||
      (VerifyDebugInfoPreserve && !VerifyEachDebugInfoPreserve);

  Passes.add(new TargetLibraryInfoWrapperPass(TLII));

  // Add internal analysis passes from the target machine.
  Passes.add(createTargetTransformInfoWrapperPass(TM ? TM->getTargetIRAnalysis()
                                                     : TargetIRAnalysis()));

  if (AddOneTimeDebugifyPasses) {
    if (EnableDebugify) {
      Passes.setDIStatsMap(DIStatsMap);
      Passes.add(createDebugifyModulePass());
    } else if (VerifyDebugInfoPreserve) {
      Passes.setDebugInfoBeforePass(DebugInfoBeforePass);
      Passes.add(createDebugifyModulePass(DebugifyMode::OriginalDebugInfo, "",
                                          &(Passes.getDebugInfoPerPass())));
    }
  }

  if (TM) {
    // FIXME: We should dyn_cast this when supported.
    auto &LTM = static_cast<LLVMTargetMachine &>(*TM);
    Pass *TPC = LTM.createPassConfig(Passes);
    Passes.add(TPC);
  }

  // Create a new optimization pass for each one specified on the command line
  for (unsigned i = 0; i < PassList.size(); ++i) {
    const PassInfo *PassInf = PassList[i];
    if (PassInf->getNormalCtor()) {
      Pass *P = PassInf->getNormalCtor()();
      if (P) {
        // Add the pass to the pass manager.
        Passes.add(P);
        // If we are verifying all of the intermediate steps, add the verifier.
        if (VerifyEach)
          Passes.add(createVerifierPass());
      }
    } else
      errs() << argv[0] << ": cannot create pass: " << PassInf->getPassName()
             << "\n";
  }

  // Check that the module is well formed on completion of optimization
  if (!NoVerify && !VerifyEach)
    Passes.add(createVerifierPass());

  if (AddOneTimeDebugifyPasses) {
    if (EnableDebugify)
      Passes.add(createCheckDebugifyModulePass(false));
    else if (VerifyDebugInfoPreserve) {
      if (!VerifyDIPreserveExport.empty())
        Passes.setOrigDIVerifyBugsReportFilePath(VerifyDIPreserveExport);
      Passes.add(createCheckDebugifyModulePass(
          false, "", nullptr, DebugifyMode::OriginalDebugInfo,
          &(Passes.getDebugInfoPerPass()), VerifyDIPreserveExport));
    }
  }

  // In run twice mode, we want to make sure the output is bit-by-bit
  // equivalent if we run the pass manager again, so setup two buffers and
  // a stream to write to them. Note that llc does something similar and it
  // may be worth to abstract this out in the future.
  SmallVector<char, 0> Buffer;
  SmallVector<char, 0> FirstRunBuffer;
  std::unique_ptr<raw_svector_ostream> BOS;
  raw_ostream *OS = nullptr;

  const bool ShouldEmitOutput = !NoOutput;

  // Write bitcode or assembly to the output as the last step...
  if (ShouldEmitOutput || RunTwice) {
    assert(Out);
    OS = &Out->os();
    if (RunTwice) {
      BOS = std::make_unique<raw_svector_ostream>(Buffer);
      OS = BOS.get();
    }
    if (OutputAssembly)
      Passes.add(createPrintModulePass(*OS, "", PreserveAssemblyUseListOrder));
    else
      Passes.add(createBitcodeWriterPass(*OS, PreserveBitcodeUseListOrder));
  }

  // Before executing passes, print the final values of the LLVM options.
  cl::PrintOptionValues();

  if (!RunTwice) {
    // Now that we have all of the passes ready, run them.
    Passes.run(*M);
  } else {
    // If requested, run all passes twice with the same pass manager to catch
    // bugs caused by persistent state in the passes.
    std::unique_ptr<Module> M2(CloneModule(*M));
    // Run all passes on the original module first, so the second run processes
    // the clone to catch CloneModule bugs.
    Passes.run(*M);
    FirstRunBuffer = Buffer;
    Buffer.clear();

    Passes.run(*M2);

    // Compare the two outputs and make sure they're the same
    assert(Out);
    if (Buffer.size() != FirstRunBuffer.size() ||
        (memcmp(Buffer.data(), FirstRunBuffer.data(), Buffer.size()) != 0)) {
      errs()
          << "Running the pass manager twice changed the output.\n"
             "Writing the result of the second run to the specified output.\n"
             "To generate the one-run comparison binary, just run without\n"
             "the compile-twice option\n";
      if (ShouldEmitOutput) {
        Out->os() << BOS->str();
        Out->keep();
      }
      if (RemarksFile)
        RemarksFile->keep();
      return 1;
    }
    if (ShouldEmitOutput)
      Out->os() << BOS->str();
  }

  if (DebugifyEach && !DebugifyExport.empty())
    exportDebugifyStats(DebugifyExport, Passes.getDebugifyStatsMap());

  // Declare success.
  if (!NoOutput)
    Out->keep();

  if (RemarksFile)
    RemarksFile->keep();

  if (ThinLinkOut)
    ThinLinkOut->keep();

  return 0;
}
