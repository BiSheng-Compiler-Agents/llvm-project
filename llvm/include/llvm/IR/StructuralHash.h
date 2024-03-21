//===- llvm/IR/StructuralHash.h - IR Hashing --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides hashing of the LLVM IR structure to be used to check
// Passes modification status.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_STRUCTURALHASH_H
#define LLVM_IR_STRUCTURALHASH_H

#include <cstdint>
#if defined(ENABLE_AUTOTUNER)
#include <vector>
#endif

namespace llvm {

class Function;
class Module;

uint64_t StructuralHash(const Function &F);
uint64_t StructuralHash(const Module &M);

#if defined(ENABLE_AUTOTUNER)
class MachineBasicBlock;
class BasicBlock;
class CallBase;
class SwitchInst;

uint64_t StructuralHash(const std::vector<BasicBlock *> BBs);
uint64_t StructuralHash(const MachineBasicBlock &MBB);
uint64_t StructuralHash(const CallBase &CB);
uint64_t StructuralHash(const SwitchInst &SI);
#endif
} // end namespace llvm

#endif
