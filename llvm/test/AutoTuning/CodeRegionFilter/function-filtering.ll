; REQUIRES: asserts

; RUN: rm -rf %t.filter
; RUN: opt %s -S -passes='function(require<opt-remark-emit>,loop-unroll),cgscc(inline)' \
; RUN:     -auto-tuning-opp=%t.filter -auto-tuning-type-filter=CallSite,Loop --disable-output
; RUN: FileCheck %s --input-file %t.filter/function-filtering.ll.yaml -check-prefix=DEFAULT

; RUN: rm -rf %t.filter
; RUN: opt %s -S -passes='function(require<opt-remark-emit>,loop-unroll),cgscc(inline)' \
; RUN:     -auto-tuning-opp=%t.filter -auto-tuning-type-filter=CallSite,Loop \
; RUN:     -auto-tuning-function-filter=foo --disable-output
; RUN: FileCheck %s --input-file %t.filter/function-filtering.ll.yaml -check-prefix=FILTER_FOO

; RUN: rm -rf %t.filter
; RUN: opt %s -S -passes='function(require<opt-remark-emit>,loop-unroll),cgscc(inline)' \
; RUN:     -auto-tuning-opp=%t.filter -auto-tuning-type-filter=CallSite,Loop \
; RUN:     -auto-tuning-function-filter=bar --disable-output
; RUN: FileCheck %s --input-file %t.filter/function-filtering.ll.yaml -check-prefix=FILTER_BAR

; RUN: rm -rf %t.filter
; RUN: opt %s -S -passes='function(require<opt-remark-emit>,loop-unroll),cgscc(inline)' \
; RUN:     -auto-tuning-opp=%t.filter -auto-tuning-type-filter=CallSite,Loop \
; RUN:     -auto-tuning-function-filter=dummy -debug-only=autotuning | \
; RUN:     FileCheck %s -check-prefix=FILTER_DUMMY

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

define void @bar(i32* nocapture %a) {
entry:
  call void @foo(i32* %a)
  ret void
}

; DEFAULT: --- !AutoTuning
; DEFAULT: --- !AutoTuning

; FILTER_FOO: --- !AutoTuning
; FILTER_FOO: Function:        foo
; FILTER_FOO-NOT: --- !AutoTuning

; FILTER_BAR: --- !AutoTuning
; FILTER_BAR: Function:        bar
; FILTER_BAR-NOT: --- !AutoTuning

; FILTER_DUMMY-NOT: --- !AutoTuning
; FILTER_DUMMY-NOT: --- !AutoTuning
