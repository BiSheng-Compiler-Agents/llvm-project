; NOTE: This file is used to test when UnrollCount = 1 and when the compiler
; sees that Loop Peeling is beneficial and possible, then we do Loop Peeling.
; RUN: rm %t.unroll1.yaml -rf
; RUN: sed 's#\[number\]#1#g;' %S/Inputs/loop_peel.yaml > %t.unroll1.yaml
; RUN: opt  %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-code-region-matching-hash=false \
; RUN:     -auto-tuning-input=%t.unroll1.yaml | FileCheck %s

; RUN: rm %t.unroll0.yaml -rf
; RUN: sed 's#\[number\]#0#g;' %S/Inputs/loop_peel.yaml > %t.unroll0.yaml
; RUN: opt  %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-code-region-matching-hash=false \
; RUN:     -auto-tuning-input=%t.unroll0.yaml | FileCheck %s --check-prefix=DISABLE

; RUN: opt  %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-code-region-matching-hash=false \
; RUN:     -auto-tuning-opp=%t.unroll_opp -auto-tuning-type-filter=Loop --disable-output
; RUN: FileCheck %s --input-file %t.unroll_opp/loop_peel.ll.yaml -check-prefix=TEST-1

define i32 @invariant_backedge_1(i32 %a, i32 %b) {
; CHECK-LABEL: @invariant_backedge_1
; CHECK-NOT:     %plus = phi
; CHECK:       loop.peel:
; CHECK:       loop:
; CHECK:         %i = phi
; CHECK:         %sum = phi
; DISABLE-LABEL: @invariant_backedge_1
; DISABLE-NOT: loop.peel:
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %incsum, %loop ]
  %plus = phi i32 [ %a, %entry ], [ %b, %loop ]

  %incsum = add i32 %sum, %plus
  %inc = add i32 %i, 1
  %cmp = icmp slt i32 %i, 1000

  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum
}

; Check for dynamic values when UnrollCount is set to 1:
; TEST-1:      Pass:                loop-unroll
; TEST-1-NEXT: Name:                loop
; TEST-1-NEXT: Function:            invariant_backedge_1
; TEST-1-NEXT: CodeRegionType:      loop
; TEST-1-NEXT: CodeRegionHash:      {{[0-9]+}}
; TEST-1-NEXT: DynamicConfigs:      { UnrollCount: [ 0, 1, 2 ] }
