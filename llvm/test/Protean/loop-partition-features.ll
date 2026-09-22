; RUN: opt -passes=protean-collect-features -S %s | \
; RUN:   %python %S/Inputs/extract-protean-features.py | FileCheck %s --match-full-lines

; Successful partitioning must produce nonzero features.
; CHECK-DAG: NumPartitions = 1
; CHECK-DAG: IndVarSetSize = 1
; CHECK-DAG: AvgStoreSetSize = 1
; CHECK-DAG: AvgNumInsts = 2

define void @store_loop(ptr %p) {
entry:
  br label %loop

loop:
  %iv = phi i64 [ 0, %entry ], [ %next, %loop ]
  %ptr = getelementptr i32, ptr %p, i64 %iv
  store i32 0, ptr %ptr
  %next = add nuw nsw i64 %iv, 1
  %continue = icmp ult i64 %next, 16
  br i1 %continue, label %loop, label %exit

exit:
  ret void
}
