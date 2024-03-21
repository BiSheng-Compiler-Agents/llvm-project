; RUN: rm %t.default-opp -rf
; RUN: llvm-profdata merge %S/Inputs/pgo-instr.proftext -o %t.profdata
; RUN: opt %s -passes='pgo-instr-use,inline' -pgo-test-profile-file=%t.profdata -S -auto-tuning-opp=%t.default-opp -auto-tuning-exclude-cold=false --disable-output
; RUN: FileCheck  %s  --input-file %t.default-opp/pgo-instr-filters.ll.yaml  -check-prefix=NON-FILTER

; RUN: rm %t.filtered-opp -rf
; RUN: llvm-profdata merge %S/Inputs/pgo-instr.proftext -o %t.profdata
; RUN: opt %s -passes='pgo-instr-use,inline' -pgo-test-profile-file=%t.profdata -S -auto-tuning-opp=%t.filtered-opp -auto-tuning-exclude-cold --disable-output -pgo-instr-old-cfg-hashing=true
; RUN: FileCheck  %s  --input-file %t.filtered-opp/pgo-instr-filters.ll.yaml  -check-prefix=EXCLUDE-COLD

; RUN: rm %t.filtered-opp -rf
; RUN: llvm-profdata merge %S/Inputs/pgo-instr.proftext -o %t.profdata
; RUN: opt %s -passes='pgo-instr-use,inline' -pgo-test-profile-file=%t.profdata -S -auto-tuning-opp=%t.filtered-opp -auto-tuning-hot-only --disable-output -pgo-instr-old-cfg-hashing=true
; RUN: FileCheck  %s  --input-file %t.filtered-opp/pgo-instr-filters.ll.yaml  -check-prefix=HOT-ONLY

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@s = common dso_local local_unnamed_addr global i32 0, align 4

define void @cold() {

entry:
  %0 = tail call i32 @callee(i32 5)
  store i32 1, i32* @s, align 4
  ret void
}

define void @hot() {
entry:
  %0 = load i32, i32* @s, align 4
  %1 = tail call i32 @callee(i32 5)
  %add = add nsw i32 %0, 4
  store i32 %add, i32* @s, align 4
  ret void
}

define void @unknown() {
entry:
  %0 = tail call i32 @callee(i32 5)
  store i32 1, i32* @s, align 4
  ret void
}

define i32 @callee(i32 %a) {
entry:
  %add = add nsw i32 %a, 4
  ret i32 %add
}

; NON-FILTER-DAG: Function:        cold
; NON-FILTER-DAG: Function:        hot
; NON-FILTER-DAG: Function:        unknown

; EXCLUDE-COLD-NOT: Function:        cold
; EXCLUDE-COLD-DAG: Function:        hot
; EXCLUDE-COLD-DAG: Function:        unknown

; HOT-ONLY-NOT: Function:        unknown
; HOT-ONLY-NOT: Function:        cold
; HOT-ONLY-DAG: Function:        hot
