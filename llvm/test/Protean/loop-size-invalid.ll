; RUN: opt -passes=protean-collect-features -S %s | \
; RUN:   %python %S/Inputs/extract-protean-features.py | FileCheck %s --match-full-lines

; CHECK-DAG: Size = 0

define void @invalid_loop_size(<2 x ptr> %buckets, i1 %again) {
entry:
  br label %loop

loop:
  call void @llvm.experimental.vector.histogram.add.v2p0.i32(<2 x ptr> %buckets, i32 1, <2 x i1> <i1 true, i1 true>)
  br i1 %again, label %loop, label %exit

exit:
  ret void
}

declare void @llvm.experimental.vector.histogram.add.v2p0.i32(<2 x ptr>, i32, <2 x i1>)
