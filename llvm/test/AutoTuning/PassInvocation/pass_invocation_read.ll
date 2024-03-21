; RUN: rm %t.config.yaml -rf
; RUN: sed 's#\[number\]#0#g;' %S/Inputs/pass_invocation.yaml > %t.config.yaml
; RUN: opt %s -S -O3 -print-after=loop-unroll-full -print-after=loop-unroll \
; RUN:     -auto-tuning-code-region-matching-hash=false \
; RUN:     -auto-tuning-input=%t.config.yaml --disable-output 2>&1 | \
; RUN:     FileCheck %s --check-prefix=INVOCATION-0

; RUN: rm %t.config.yaml -rf
; RUN: sed 's#\[number\]#1#g;' %S/Inputs/pass_invocation.yaml > %t.config.yaml
; RUN: opt %s -S -O3 -print-after=loop-unroll-full -print-after=loop-unroll \
; RUN:     -auto-tuning-code-region-matching-hash=false \
; RUN:     -auto-tuning-input=%t.config.yaml --disable-output 2>&1 | \
; RUN:     FileCheck %s --check-prefix=INVOCATION-1

; Function Attrs: norecurse nounwind readonly uwtable
define dso_local i64 @find(i64* nocapture readonly %a, i64 %n, i64 %Value) {
entry:
  %cmp6.not = icmp eq i64 %n, 0
  br i1 %cmp6.not, label %for.end, label %for.body

for.body:                                         ; preds = %entry, %for.inc
  %i.07 = phi i64 [ %inc, %for.inc ], [ 0, %entry ]
  %arrayidx = getelementptr inbounds i64, i64* %a, i64 %i.07
  %0 = load i64, i64* %arrayidx, align 8
  %cmp1 = icmp eq i64 %0, %Value
  br i1 %cmp1, label %for.end, label %for.inc

for.inc:                                          ; preds = %for.body
  %inc = add nuw i64 %i.07, 1
  %cmp = icmp ult i64 %inc, %n
  br i1 %cmp, label %for.body, label %for.end

for.end:                                          ; preds = %for.inc, %for.body, %entry
  %i.0.lcssa = phi i64 [ 0, %entry ], [ %i.07, %for.body ], [ %inc, %for.inc ]
  ret i64 %i.0.lcssa
}

; INVOCATION-0: *** IR Dump After {{.*}}Unroll
; INVOCATION-0: for.body.preheader:                               ; preds = %entry
; INVOCATION-0: for.body:                                         ; preds = %for.inc.1, %for.body.preheader
; INVOCATION-0: for.inc:                                          ; preds = %for.body
; INVOCATION-0: for.body.1:                                       ; preds = %for.inc
; INVOCATION-0: for.inc.1:                                        ; preds = %for.body.1
; INVOCATION-0: for.end.loopexit:                                 ; preds = %for.inc.1, %for.body.1, %for.body, %for.inc
; INVOCATION-0: *** IR Dump After {{.*}}Unroll
; INVOCATION-0: for.body.preheader:                               ; preds = %entry
; INVOCATION-0: for.body:                                         ; preds = %for.body.preheader, %for.inc.1
; INVOCATION-0: for.inc:                                          ; preds = %for.body
; INVOCATION-0: for.body.1:                                       ; preds = %for.inc
; INVOCATION-0: for.inc.1:                                        ; preds = %for.body.1
; INVOCATION-0: for.end.loopexit:                                 ; preds = %for.inc.1, %for.body.1, %for.body, %for.inc

; INVOCATION-1: *** IR Dump After {{.*}}Unroll
; INVOCATION-1: for.body.preheader:                               ; preds = %entry
; INVOCATION-1: for.body:                                         ; preds = %for.body.preheader, %for.inc
; INVOCATION-1: for.inc:                                          ; preds = %for.body
; INVOCATION-1: for.end.loopexit:                                 ; preds = %for.body, %for.inc
; INVOCATION-1: *** IR Dump After {{.*}}Unroll
; INVOCATION-1: for.body.preheader:                               ; preds = %entry
; INVOCATION-1: for.body:                                         ; preds = %for.inc.1, %for.body.preheader
; INVOCATION-1: for.inc:                                          ; preds = %for.body
; INVOCATION-1: for.body.1:                                       ; preds = %for.inc
; INVOCATION-1: for.inc.1:                                        ; preds = %for.body.1
; INVOCATION-1: for.end.loopexit:                                 ; preds = %for.inc.1, %for.body.1, %for.body, %for.inc
