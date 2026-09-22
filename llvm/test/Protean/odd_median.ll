; RUN: opt -passes=protean-collect-features -S %s | %python %S/Inputs/extract-protean-features.py | FileCheck %s --match-full-lines

define void @random() {
  ret void
}

define void @leaf() {
  call void @random()
  ret void
}

define void @main() {
  call void @leaf()
  call void @leaf()
  ret void
}

; CHECK-DAG: median_calls_per_function = 1
