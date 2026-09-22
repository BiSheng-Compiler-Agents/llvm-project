; RUN: opt -passes=protean-collect-features -S %s | \
; RUN:   %python %S/Inputs/extract-protean-features.py | FileCheck %s --match-full-lines

; Both calls must contribute their tail markers despite having no direct callee.
; CHECK-DAG: total_function_calls = 2
; CHECK-DAG: is_indirect = 2
; CHECK-DAG: is_must_tail = 1
; CHECK-DAG: is_tail = 2

define void @tail_caller(ptr %fp) {
  tail call void %fp()
  ret void
}

define void @musttail_caller(ptr %fp) {
  musttail call void %fp(ptr %fp)
  ret void
}
