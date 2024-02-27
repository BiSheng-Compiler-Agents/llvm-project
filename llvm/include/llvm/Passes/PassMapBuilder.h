//===- PassMapBuilder.h - Creates Map from Pass to Pass Type --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2024, Huawei Technologies Co., Ltd. All rights reserved.
//
//===----------------------------------------------------------------------===//
//
// Maps pass to its corresponding Pass Type (ie. Module, CGSCC, Function, and
// Loop) to be used for Pass Order Nesting in PhaseOrder.cpp
//
//===----------------------------------------------------------------------===//
#include <string>
#include <unordered_map>

std::unordered_map<std::string, std::string> initializePassMap();
