; RUN: rm %t.switch_opp -rf
; RUN: llc %s -auto-tuning-opp=%t.switch_opp -auto-tuning-type-filter=Switch -o /dev/null
; RUN: FileCheck %s --input-file %t.switch_opp/switch-opp.ll.yaml

; UNSUPPORTED: windows

define i32 @test(i32 %arg) #0 {
entry:
  switch i32 %arg, label %bb5 [
    i32 1, label %bb1
    i32 2, label %bb2
    i32 3, label %bb3
    i32 4, label %bb4
  ]

bb1:			; pred = %entry
  br label %bb2

bb2:			; pred = %entry, %bb1
  %res.0 = phi i32 [ 1, %entry ], [ 2, %bb1 ]
  br label %bb3

bb3:			; pred = %entry, %bb2
  %res.1 = phi i32 [ 0, %entry ], [ %res.0, %bb2 ]
  %phitmp = add nsw i32 %res.1, 2
  br label %bb4

bb4:			; pred = %entry, %bb3
  %res.2 = phi i32 [ 1, %entry ], [ %phitmp, %bb3 ]
  br label %bb5

bb5:			; pred = %entry, %bb4
  %res.3 = phi i32 [ 0, %entry ], [ %res.2, %bb4 ]
  %0 = add nsw i32 %res.3, 1
  ret i32 %0
}

; CHECK: --- !AutoTuning
; CHECK-NEXT: Pass:            switch-lowering
; CHECK-NEXT: Name:            'i32 %arg'
; CHECK-NEXT: Function:        test
; CHECK-NEXT: CodeRegionType:  switch
; CHECK-NEXT: CodeRegionHash:  {{[0-9]+}}
; CHECK-NEXT: DynamicConfigs:  { }
; CHECK-NEXT: BaselineConfig:  { }
; CHECK-NEXT: Invocation:      0
; CHECK-NEXT: ...
