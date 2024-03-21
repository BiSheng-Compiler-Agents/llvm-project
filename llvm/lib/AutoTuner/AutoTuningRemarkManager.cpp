#if defined(ENABLE_AUTOTUNER)
//===- llvm/AutoTuner/AutoTuningRemarkManager.cpp - Remark Manager --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of for inputting and outputting remarks
// for AutoTuning.
//
//===----------------------------------------------------------------------===//

#include "llvm/AutoTuner/AutoTuningRemarkManager.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/AutoTuner/AutoTuning.h"
#include "llvm/AutoTuner/AutoTuningRemarkStreamer.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/LLVMRemarkStreamer.h"
#include "llvm/Remarks/Remark.h"
#include "llvm/Remarks/RemarkFormat.h"
#include "llvm/Remarks/RemarkParser.h"
#include "llvm/Remarks/RemarkSerializer.h"
#include "llvm/Remarks/RemarkStreamer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ToolOutputFile.h"

// Enable debug messages for AutoTuner.
#define DEBUG_TYPE "autotuning"

using namespace llvm;
using namespace autotuning;

// Helper functions.
namespace {
// Convert string into CodeRegionType.
Expected<CodeRegionType> StringToCodeRegionType(const std::string &CRType) {
  if (CRType == "machine_basic_block")
    return autotuning::CodeRegionType::MachineBasicBlock;
  else if (CRType == "loop")
    return autotuning::CodeRegionType::Loop;
  else if (CRType == "function")
    return autotuning::CodeRegionType::Function;
  else if (CRType == "callsite")
    return autotuning::CodeRegionType::CallSite;
  else if (CRType == "llvm-param")
    return autotuning::CodeRegionType::LLVMParam;
  else if (CRType == "program-param")
    return autotuning::CodeRegionType::ProgramParam;
  else if (CRType == "switch")
    return autotuning::CodeRegionType::Switch;
  else if (CRType == "other")
    return autotuning::CodeRegionType::Other;
  else
    return make_error<StringError>("Unsupported CodeRegionType:" + CRType,
                                   inconvertibleErrorCode());
}

// Remark -> autotuning::ParameterManager
ParameterManager RemarkToParameterManager(const remarks::Remark &Remark) {
  // Create Parameters from a remark.
  ParameterManager ParamManager;
  for (const remarks::Argument &Arg : Remark.Args) {
    int Value = 0;
    if (!Arg.Val.getAsInteger(10, Value))
      // If no errors
      ParamManager.add(Arg.Key.str(), Value);
    else if (Arg.Val == "true")
      ParamManager.add(Arg.Key.str(), true);
    else if (Arg.Val == "false")
      ParamManager.add(Arg.Key.str(), false);
    // If there is a value of vector type
    else if (Arg.VectorVal) {
      std::vector<std::string> Strings;
      for (const StringRef &Val : *Arg.VectorVal) {
        Strings.push_back(Val.str());
      }
      ParamManager.add(Arg.Key.str(), Strings);
    } else
      // Add as String Value
      ParamManager.add(Arg.Key.str(), Arg.Val);
  }

  return ParamManager;
}

// Remark -> std::unordered_map<std::string, std::string>
std::unordered_map<std::string, std::string>
RemarkToStringMap(const remarks::Remark &Remark) {
  std::unordered_map<std::string, std::string> LLVMParams;
  for (const remarks::Argument &Arg : Remark.Args) {
    // Add as String Value
    LLVMParams[Arg.Key.str()] = Arg.Val.str();
  }
  return LLVMParams;
}

// Remark -> autotuning::SourceLocation
SourceLocation RemarkToSourceLocation(const remarks::Remark &Remark) {
  SourceLocation Location;
  if (Remark.Loc) {
    StringRef File = Remark.Loc->SourceFilePath;
    unsigned Line = Remark.Loc->SourceLine;
    unsigned Column = Remark.Loc->SourceColumn;
    Location = {File.str(), Line, Column};
  }
  return Location;
}

// Remark -> autotuning::CodeRegion
CodeRegion RemarkToCodeRegion(const remarks::Remark &Remark,
                              Expected<CodeRegionType> &Type) {
  // Create a SourceLocation from a remark.
  SourceLocation Location = RemarkToSourceLocation(Remark);
  // Create a CodeRegion from a remark.
  CodeRegion CR = CodeRegion(Remark.RemarkName.str(), Remark.FunctionName.str(),
                             Remark.PassName.str(), Type.get(), Location);
  if (Remark.CodeRegionHash)
    CR.setHash(Remark.CodeRegionHash.value_or(0));
  if (Remark.Invocation)
    CR.setInvocation(Remark.Invocation.value_or(0));

  return CR;
}

Expected<std::unique_ptr<ToolOutputFile>> emitAutoTuningRemarks(
    const StringRef RemarksFilename, const StringRef RemarksFormat,
    const StringRef RemarksPasses, const CodeRegions &CRList) {
  if (RemarksFilename.empty())
    return nullptr;
  // Parse remark format. Options are yaml, yaml-strtab and bitstream.
  Expected<remarks::Format> Format = remarks::parseFormat(RemarksFormat);
  if (Error E = Format.takeError())
    return make_error<LLVMRemarkSetupFormatError>(std::move(E));

  std::error_code EC;
  auto Flags =
      *Format == remarks::Format::YAML ? sys::fs::OF_Text : sys::fs::OF_None;
  auto RemarksFile =
      std::make_unique<ToolOutputFile>(RemarksFilename, EC, Flags);
  if (EC)
    return make_error<LLVMRemarkSetupFormatError>(errorCodeToError(EC));
  // Create a remark serializer to emit code regions.
  Expected<std::unique_ptr<remarks::RemarkSerializer>> RemarkSerializer =
      remarks::createRemarkSerializer(
          *Format, remarks::SerializerMode::Separate, RemarksFile->os());

  if (Error E = RemarkSerializer.takeError())
    return make_error<LLVMRemarkSetupFormatError>(std::move(E));
  // Create remark streamer based on the serializer.
  remarks::RemarkStreamer RStreamer =
      remarks::RemarkStreamer(std::move(*RemarkSerializer), RemarksFilename);
  AutoTuningRemarkStreamer Streamer(RStreamer);

  if (!RemarksPasses.empty())
    if (Error E = Streamer.setFilter(RemarksPasses))
      return make_error<LLVMRemarkSetupFormatError>(std::move(E));
  // Emit CodeRegions in Remark format.
  for (const CodeRegion &CR : CRList) {
    Streamer.emit(CR);
  }
  return std::move(RemarksFile);
}
} // namespace

llvm::Error AutoTuningRemarkManager::read(AutoTuningEngine &E,
                                          const std::string &InputFileName,
                                          const std::string &RemarksFormat) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> Buf =
      MemoryBuffer::getFile(InputFileName.c_str());
  if (std::error_code EC = Buf.getError())
    return make_error<StringError>(
        "Can't open file " + InputFileName + ": " + EC.message(), EC);
  // Parse remark format. Options are yaml, yaml-strtab and bitstream.
  Expected<remarks::Format> Format = remarks::parseFormat(RemarksFormat);
  if (!Format)
    return Format.takeError();

  Expected<std::unique_ptr<remarks::RemarkParser>> MaybeParser =
      remarks::createRemarkParserFromMeta(*Format, (*Buf)->getBuffer());
  if (!MaybeParser) {
    return MaybeParser.takeError();
  }
  remarks::RemarkParser &Parser = **MaybeParser;

  while (true) {
    Expected<std::unique_ptr<remarks::Remark>> MaybeRemark = Parser.next();
    if (!MaybeRemark) {
      Error E = MaybeRemark.takeError();
      if (E.isA<remarks::EndOfFileError>()) {
        // EOF.
        consumeError(std::move(E));
        break;
      }
      return E;
    }
    const remarks::Remark &Remark = **MaybeRemark;

    if (Remark.RemarkType != remarks::Type::AutoTuning)
      continue;

    if (!Remark.CodeRegionType)
      return make_error<StringError>("CodeRegionType field is missing.",
                                     inconvertibleErrorCode());
    Expected<CodeRegionType> Type =
        StringToCodeRegionType((*Remark.CodeRegionType).str());
    if (!Type)
      return Type.takeError();
    CodeRegionType CRType = Type.get();
    // If CodeRegionType is Other, this remark corresponds to global
    // parameters, and no need to create a CodeRegion object. Check if the
    // Remark of global parameters is for the current Module.
    if (CRType == autotuning::Other && Remark.RemarkName == Engine.ModuleID) {
      Engine.GlobalParams = RemarkToParameterManager(Remark);
      continue;
    }
    if (CRType == autotuning::LLVMParam &&
        Remark.RemarkName == Engine.ModuleID) {
      Engine.LLVMParams = RemarkToStringMap(Remark);
      continue;
    }
    if (CRType == autotuning::ProgramParam &&
        Remark.RemarkName == Engine.ModuleID) {
      Engine.ProgramParams = RemarkToStringMap(Remark);
      continue;
    }
    if (Engine.isThinLTOTuning() &&
        (CRType == autotuning::CallSite || CRType == autotuning::Loop ||
         CRType == autotuning::MachineBasicBlock ||
         CRType == autotuning::Function)) {
      LLVM_DEBUG(dbgs() << "AutoTuner does not support tuning of "
                        << CodeRegion::getTypeAsString(CRType)
                        << " for thinLTO durning link-time optimization. "
                           "Ignoring current code region.\n");
      continue;
    }
    // Create a SourceLocation from a remark.
    CodeRegion CR = RemarkToCodeRegion(Remark, Type);
    ParameterManager ParamManager = RemarkToParameterManager(Remark);
    // Add the CodeRegion-ParameterManager entry into LoopUpTable.
    Engine.ParamTable[CR] = ParamManager;

    std::string Filename = CR.getSourceLoc().SourceFilePath;
    size_t Pos = Filename.rfind(".");
    if (Pos != std::string::npos)
      Filename.erase(Pos, Filename.size());
    Engine.OppPassList[Filename].insert(CR.getPassName());
    Engine.CodeRegionFilterTypes.insert(CR.getType());
  }
  return Error::success();
}

Error AutoTuningRemarkManager::dump(const autotuning::AutoTuningEngine &E,
                                    const std::string &DirName,
                                    const std::string &RemarksFormat,
                                    const std::string &RemarksPasses) {
  // Change to absolute path.
  SmallString<256> OutputPath = StringRef(DirName);
  sys::fs::make_absolute(OutputPath);

  // Make sure the new output directory exists, creating it if necessary.
  if (std::error_code EC = sys::fs::create_directories(OutputPath)) {
    return make_error<StringError>("could not create directory: " +
                                       Twine(OutputPath) + ": " + EC.message(),
                                   EC);
  }
  if (!Engine.TuningOpps.empty()) {
    StringRef ModelFileName = sys::path::filename(Engine.ModuleID);
    sys::path::append(OutputPath, ModelFileName + "." + RemarksFormat);

    int i = 1; // Output file suffix starts from 1.
    // Check all exiting xml files xml.1...i and create a new file
    // suffix.(i+1).
    while (sys::fs::exists(OutputPath)) {
      sys::path::remove_filename(OutputPath);
      sys::path::append(OutputPath,
                        ModelFileName + "." + RemarksFormat + "." + Twine(i));
      i += 1;
    }
    Expected<std::unique_ptr<ToolOutputFile>> RemarksFileOrErr =
        emitAutoTuningRemarks(OutputPath, RemarksFormat, RemarksPasses,
                              Engine.TuningOpps);
    if (Error E = RemarksFileOrErr.takeError()) {
      return E;
    }

    std::unique_ptr<ToolOutputFile> RemarksFile = std::move(*RemarksFileOrErr);
    if (RemarksFile)
      RemarksFile->keep();
  }
  return Error::success();
}

#endif
