; RUN: rm %t.non-existing.yaml -rf
; RUN: not opt  %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-input=%t.non-existing.yaml 2>&1 | \
; RUN:     FileCheck %s -check-prefix=ERROR

; UNSUPPORTED: windows

define void @foo(i32* nocapture %a) {
entry:
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %for.body ]
  %arrayidx = getelementptr inbounds i32, i32* %a, i64 %indvars.iv
  %0 = load i32, i32* %arrayidx, align 4
  %inc = add nsw i32 %0, 1
  store i32 %inc, i32* %arrayidx, align 4
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond = icmp eq i64 %indvars.iv.next, 64
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  ret void
}

; check if error massage is shown properly when input yaml is not found
;
; ERROR: Error parsing auto-tuning input.
; ERROR: No such file or directory
