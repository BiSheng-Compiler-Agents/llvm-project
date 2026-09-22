; RUN: opt -passes=protean-collect-features -S %s | \
; RUN:   %python %S/Inputs/extract-protean-features.py | FileCheck %s --match-full-lines

; The callee has loops at depths 1 and 2, not just the top-level loop.
; CHECK-DAG: callee_MaxLoopDepth = 2
; CHECK-DAG: callee_AvgNestedLoopLevel = 1.5

define void @caller(i1 %again) {
  call void @nested(i1 %again)
  ret void
}

define void @nested(i1 %again) {
entry:
  br label %outer

outer:
  br label %inner

inner:
  br i1 %again, label %inner, label %outer.latch

outer.latch:
  br i1 %again, label %outer, label %exit

exit:
  ret void
}
