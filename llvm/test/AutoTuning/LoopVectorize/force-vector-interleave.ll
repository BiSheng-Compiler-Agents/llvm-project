; RUN: rm %t.1 %t.2 %t.1.yaml -rf
; RUN: opt %s -passes=loop-vectorize -force-vector-interleave=1 -S -o %t.1
; RUN: sed 's#\[number\]#1#g' %S/Inputs/vectorize_template.yaml > %t.1.yaml
; RUN: opt %s -passes=loop-vectorize -auto-tuning-input=%t.1.yaml \
; RUN:     -S -o %t.2 -debug-only=autotuning 2>&1 | \
; RUN:     FileCheck %s -check-prefix=NUMBER1
; RUN: diff %t.1 %t.2

; RUN: rm %t.1 %t.2 %t.1.yaml -rf
; RUN: opt %s -passes=loop-vectorize -force-vector-interleave=1 -S -o %t.1
; RUN: sed 's#\[number\]#1#g' %S/Inputs/vectorize_template_no_metadata.yaml > %t.1.yaml
; RUN: opt %s -passes=loop-vectorize -auto-tuning-input=%t.1.yaml \
; RUN:     -auto-tuning-omit-metadata -S -o %t.2 -debug-only=autotuning 2>&1 | \
; RUN:     FileCheck %s -check-prefix=NUMBER1
; RUN: diff %t.1 %t.2

; RUN: rm %t.3 %t.4 %t.2.yaml -rf
; RUN: opt %s -passes=loop-vectorize -force-vector-interleave=2 -S -o %t.3
; RUN: sed 's#\[number\]#2#g' %S/Inputs/vectorize_template.yaml > %t.2.yaml
; RUN: opt %s -passes=loop-vectorize -auto-tuning-input=%t.2.yaml \
; RUN:     -S -o %t.4 -debug-only=autotuning 2>&1 | \
; RUN:     FileCheck %s -check-prefix=NUMBER2
; RUN: diff %t.3 %t.4

; RUN: rm %t.3 %t.4 %t.2.yaml -rf
; RUN: opt %s -passes=loop-vectorize -force-vector-interleave=2 -S -o %t.3
; RUN: sed 's#\[number\]#2#g' %S/Inputs/vectorize_template_no_metadata.yaml > %t.2.yaml
; RUN: opt %s -passes=loop-vectorize -auto-tuning-input=%t.2.yaml \
; RUN:     -auto-tuning-omit-metadata -S -o %t.4 -debug-only=autotuning 2>&1 | \
; RUN:     FileCheck %s -check-prefix=NUMBER2
; RUN: diff %t.3 %t.4

; Compiler should not generate tuning opportunities for AutoTuner if -force-vector-interleave is specified.
; RUN: rm %t.interleave_opp -rf
; RUN: opt %s -S -passes=loop-vectorize -auto-tuning-opp=%t.interleave_opp \
; RUN:     -force-vector-interleave=2 --disable-output
; RUN: FileCheck %s --input-file %t.interleave_opp/force-vector-interleave.ll.yaml \
; RUN:     -check-prefix=FORCE-INTERLEAVE

; RUN: rm %t.interleave_opp -rf
; RUN: opt %s -S -passes=loop-vectorize -auto-tuning-opp=%t.interleave_opp \
; RUN:     -force-vector-interleave=0 --disable-output
; RUN: FileCheck %s --input-file %t.interleave_opp/force-vector-interleave.ll.yaml \
; RUN:     -check-prefix=FORCE-INTERLEAVE

; RUN: rm %t.interleave_opp -rf
; RUN: opt %s -S -passes=loop-vectorize -auto-tuning-opp=%t.interleave_opp --disable-output
; RUN: FileCheck %s --input-file %t.interleave_opp/force-vector-interleave.ll.yaml \
; RUN:     -check-prefix=NO-FORCE-INTERLEAVE

; REQUIRES: asserts
; UNSUPPORTED: windows
target datalayout = "e-m:e-i64:64-n32:64"
target triple = "powerpc64le-unknown-linux-gnu"

define void @TestFoo(i1 %X, i1 %Y) {
bb:
  br label %.loopexit5.outer

.loopexit5.outer:
  br label %.lr.ph12

.loopexit:
  br i1 %X, label %.loopexit5.outer, label %.lr.ph12

.lr.ph12:
  %f.110 = phi i32* [ %tmp1, %.loopexit ], [ null, %.loopexit5.outer ]
  %tmp1 = getelementptr inbounds i32, i32* %f.110, i64 -2
  br i1 %Y, label %bb4, label %.loopexit

bb4:
  %j.27 = phi i32 [ 0, %.lr.ph12 ], [ %tmp7, %bb4 ]
  %tmp5 = load i32, i32* %f.110, align 4
  %tmp7 = add nsw i32 %j.27, 1
  %exitcond = icmp eq i32 %tmp7, 0
  br i1 %exitcond, label %.loopexit, label %bb4
}

; NUMBER1: VectorizationInterleave is set for the CodeRegion:
; NUMBER1:   Name: bb4
; NUMBER1:   FuncName: TestFoo
; NUMBER2: VectorizationInterleave is set for the CodeRegion:
; NUMBER2:   Name: bb4
; NUMBER2:   FuncName: TestFoo

; FORCE-INTERLEAVE-NOT: Pass:           loop-vectorize
; NO-FORCE-INTERLEAVE: Pass:            loop-vectorize
; NO-FORCE-INTERLEAVE: BaselineConfig:  { VectorizationInterleave:
