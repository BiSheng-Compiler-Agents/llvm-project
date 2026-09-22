; RUN: rm -f %t
; RUN: opt -passes=protean-collect-features -enable-protean-feature-dump \
; RUN:   -protean-dump-file=%t -disable-output %s
; RUN: FileCheck %s < %t

; Check the CSV dump directly to retain the exact integer values before module
; feature aggregation converts them to float.
; CHECK: Module|Function|Callee|Caller|Loop,{{.*}},Size,InitialIVValueInt,FinalIVValueInt,StepValueInt,

; Values below INT32_MIN must retain their magnitude and sign as well.
; CHECK: {{^[^|]*}}|wide_negative|||loop,{{.*}},-4294967297,-21474836485,-8589934594,

define void @wide_negative() {
entry:
  br label %loop

loop:
  %iv = phi i64 [ -4294967297, %entry ], [ %next, %loop ]
  %next = add nsw i64 %iv, -8589934594
  %continue = icmp sgt i64 %next, -21474836485
  br i1 %continue, label %loop, label %exit

exit:
  ret void
}
