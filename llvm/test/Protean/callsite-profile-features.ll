; RUN: opt -passes=protean-collect-features -S %s -o %t.ll
; RUN: %python %S/Inputs/extract-protean-features.py %t.ll > %t.features
; RUN: FileCheck %s --match-full-lines < %t.features

; Both indirect and external calls must contribute in hot and cold blocks.
; The loop executes 128 times per entry, the cold block once per 128 entries.
; BFI scales the hottest block to 2^54, so the five calls' frequencies sum to
; 2 * 2^54 + 2 * 2^40 + 2^47. Each call's caller has height one.
; CHECK-DAG: total_function_calls = 5
; CHECK-DAG: cold_callsite = 2
; CHECK-DAG: hot_callsite = 2
; CHECK-DAG: block_freq = 3.61717335e+16
; CHECK-DAG: callsite_height = 5
; CHECK-DAG: caller_CallerHeight = 1
; CHECK-DAG: is_indirect = 2

declare void @external()

define void @caller(ptr %fp, i1 %rare, i1 %again) {
entry:
  call void @leaf()
  br i1 %rare, label %cold, label %loop, !prof !0
cold:
  call void %fp()
  call void @external()
  br label %loop
loop:
  call void %fp()
  call void @external()
  br i1 %again, label %loop, label %exit, !prof !1
exit:
  ret void
}

define void @leaf() {
  ret void
}

!0 = !{!"branch_weights", i32 1, i32 127}
!1 = !{!"branch_weights", i32 127, i32 1}
