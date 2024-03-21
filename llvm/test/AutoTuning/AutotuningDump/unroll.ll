; RUN: rm -rf %T.tmp/Output
; RUN: mkdir -p %T.tmp/Output
; RUN: rm %t.DEFAULT.yaml -rf
; RUN: sed 's#\[number\]#0#g; s#\[name\]#for.body#g' %S/Inputs/unroll_template.yaml > %t.DEFAULT.yaml
; RUN: env AUTOTUNE_DATADIR=%T.tmp/Output opt %s -S -passes='require<autotuning-dump>' \
; RUN:     -auto-tuning-input=%t.DEFAULT.yaml -auto-tuning-config-id=1
; RUN: env AUTOTUNE_DATADIR=%T.tmp/Output opt %s -S -passes='require<autotuning-dump>' \
; RUN:     -auto-tuning-input=%t.DEFAULT.yaml -auto-tuning-config-id=2
; RUN: cat %T.tmp/Output/unroll.ll/1.ll | FileCheck %s -check-prefix=DEFAULT
; RUN: cat %T.tmp/Output/unroll.ll/2.ll | FileCheck %s -check-prefix=DEFAULT
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
; Check that only loop body is inside the IR File.
; DEFAULT-LABEL: for.body:                                         ; preds = %for.body, %entry
; DEFAULT-NEXT: %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %for.body ]
; DEFAULT-NEXT:  %arrayidx = getelementptr inbounds i32, ptr %a, i64 %indvars.iv
; DEFAULT:  %exitcond = icmp eq i64 %indvars.iv.next, 64
; DEFAULT:  br i1 %exitcond, label %for.end, label %for.body

; RUN: rm -rf %T.tmp/Output
