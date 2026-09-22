; RUN: opt -passes=protean-collect-features -S %s | %python %S/Inputs/extract-protean-features.py | FileCheck %s --match-full-lines

define void @leaf() {
  ret void
}

define void @main() {
entry:
  br label %last
last:
  ret void
}

; CHECK-DAG: total_bb_count = 3
; CHECK-DAG: average_bb_per_function = 1.5
