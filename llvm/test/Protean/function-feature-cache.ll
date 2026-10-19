; REQUIRES: asserts
; RUN: opt -passes='protean-collect-features' \
; RUN:   -debug-only=proteanFC -S %s -o %t.ll 2>&1 | FileCheck %s \
; RUN:   --implicit-check-not='Collecting Protean function features:'
; RUN: rm -f %t.csv
; RUN: opt -passes='protean-collect-features' -enable-protean-feature-dump \
; RUN:   -protean-dump-file=%t.csv -debug-only=proteanFC -S %s -o %t.dump.ll 2>&1 | FileCheck %s \
; RUN:   --implicit-check-not='Collecting Protean function features:'
; RUN: %python %S/Inputs/extract-protean-features.py %t.ll > %t.features
; RUN: %python %S/Inputs/extract-protean-features.py %t.dump.ll > %t.dump.features
; RUN: diff %t.features %t.dump.features

; Analyze each function exactly once.
; Uncalled functions are skipped during function feature analysis.
; CHECK: Collecting Inlining Features
; CHECK: Collecting Protean function features: leaf
; CHECK: Protean Feature InstructionPerBlock: 2
; CHECK: Collecting Protean function features: caller
; CHECK: Protean Feature InstructionPerBlock: 9
; CHECK: Collecting Protean function features: external
; CHECK: Protean Feature InstructionPerBlock: 0
; CHECK: Collecting Protean function features: <indirect>
; CHECK: Protean Feature InstructionPerBlock: 0

declare void @external()

define void @caller(ptr %fp, i32 %x) {
  %same = add i32 %x, 0
  call void @leaf(i32 %same)
  call void @leaf(i32 %x)
  call void @caller(ptr %fp, i32 %x)
  call void @external()
  call void @external()
  call void %fp()
  call void %fp()
  ret void
}

define void @leaf(i32 %x) {
  call void @external()
  ret void
}

define void @uncalled() {
  ret void
}
