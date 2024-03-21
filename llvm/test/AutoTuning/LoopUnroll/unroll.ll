; RUN: opt %s -S -passes=loop-unroll | FileCheck %s -check-prefix=DISABLE

; RUN: rm %t.unroll0.yaml -rf
; RUN: sed 's#\[number\]#0#g; s#\[name\]#for.body#g; s#\[hash\]#14791762861362113823#g' \
; RUN:     %S/Inputs/unroll_template.yaml > %t.unroll0.yaml
; RUN: opt %s -S -passes=loop-unroll -auto-tuning-input=%t.unroll0.yaml \
; RUN:     -auto-tuning-code-region-matching-hash=false | \
; RUN:     FileCheck %s -check-prefix=UNROLL0

; RUN: rm %t.unroll0.yaml -rf
; RUN: sed 's#\[number\]#0#g; s#\[hash\]#14791762861362113823#g' \
; RUN:     %S/Inputs/unroll_template_no_metadata.yaml > %t.unroll0.yaml
; RUN: opt %s -S -passes=loop-unroll -auto-tuning-input=%t.unroll0.yaml \
; RUN:     -auto-tuning-omit-metadata | \
; RUN:     FileCheck %s -check-prefix=UNROLL0

; RUN: rm %t.result1 %t.unroll1.yaml -rf
; RUN: sed 's#\[number\]#1#g; s#\[name\]#for.body#g; s#\[hash\]#14791762861362113823#g' \
; RUN:     %S/Inputs/unroll_template.yaml > %t.unroll1.yaml
; RUN: opt %s -S -passes=loop-unroll -auto-tuning-input=%t.unroll1.yaml | \
; RUN:     FileCheck %s -check-prefix=UNROLL1

; RUN: rm %t.result1 %t.unroll1.yaml -rf
; RUN: sed 's#\[number\]#1#g; s#\[hash\]#14791762861362113823#g' \
; RUN:     %S/Inputs/unroll_template_no_metadata.yaml > %t.unroll1.yaml
; RUN: opt %s -S -passes=loop-unroll -auto-tuning-input=%t.unroll1.yaml \
; RUN:     -auto-tuning-omit-metadata | \
; RUN:     FileCheck %s -check-prefix=UNROLL1

; RUN: rm %t.result4 %t.unroll4.yaml -rf
; RUN: sed 's#\[number\]#4#g; s#\[name\]#for.body#g; s#\[hash\]#14791762861362113823#g' \
; RUN:     %S/Inputs/unroll_template.yaml > %t.unroll4.yaml
; RUN: opt %s -S -passes=loop-unroll -auto-tuning-input=%t.unroll4.yaml | \
; RUN:     FileCheck %s -check-prefix=UNROLL4

; RUN: rm %t.result4 %t.unroll4.yaml -rf
; RUN: sed 's#\[number\]#4#g; s#\[hash\]#14791762861362113823#g' \
; RUN:     %S/Inputs/unroll_template_no_metadata.yaml > %t.unroll4.yaml
; RUN: opt %s -S -passes=loop-unroll -auto-tuning-input=%t.unroll4.yaml \
; RUN:     -auto-tuning-omit-metadata | \
; RUN:     FileCheck %s -check-prefix=UNROLL4

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

; Auto-tuning-enabled loop unrolling - check that the loop is not unrolled when the auto-tuning feature is disabled
;
; DISABLE-LABEL: @foo(
; DISABLE: store i32
; DISABLE-NOT: store i32
; DISABLE: br i1
; DISABLE-NOT: llvm.loop.unroll.disable


; Auto-tuning-enabled loop unrolling - check that the loop is not unrolled
; when unroll count explicitly set to be 0.
;
; UNROLL0-LABEL: @foo(
; UNROLL0: store i32
; UNROLL0-NOT: store i32
; UNROLL0: br i1
; UNROLL0-NOT: llvm.loop.unroll.disable


; Auto-tuning-enabled loop unrolling - Requesting UnrollCount = 1 will perform
; Loop Peeling, and if Loop Peeling isn't possible/beneficial then Unroll Count
; is unchanged.
;
; UNROLL1-LABEL: @foo(
; UNROLL1: store i32
; UNROLL1-NOT: store i32
; UNROLL1: br i1
; UNROLL1: llvm.loop.unroll.disable

; Auto-tuning-enabled loop unrolling - check that we can unroll the loop by 4
; when explicitly requested.
;
; UNROLL4-LABEL: @foo(
; UNROLL4: store i32
; UNROLL4: store i32
; UNROLL4: store i32
; UNROLL4: store i32
; UNROLL4: br i1
; UNROLL4: llvm.loop.unroll.disable
