; REQUIRES: asserts
; RUN: rm %t.callsite_opp -rf
; RUN: opt %s -O3 -debug-only=inline -disable-output -S 2>&1 | \
; RUN:     FileCheck %s -check-prefix=DEFAULT
; RUN: opt %s -O3 -auto-tuning-opp=%t.callsite_opp -disable-output -S 2>&1
; RUN: FileCheck %s --input-file %t.callsite_opp/opp.ll.yaml -check-prefix=AUTOTUNE

@a = global i32 4

; Function Attrs: nounwind readnone uwtable
define i32 @simpleFunction(i32 %a) #0 {
entry:
  call void @extern()
  %a1 = load volatile i32, i32* @a
  %x1 = add i32 %a1,  %a1
  %a2 = load volatile i32, i32* @a
  %x2 = add i32 %x1, %a2
  %a3 = load volatile i32, i32* @a
  %x3 = add i32 %x2, %a3
  %a4 = load volatile i32, i32* @a
  %x4 = add i32 %x3, %a4
  %a5 = load volatile i32, i32* @a
  %x5 = add i32 %x4, %a5
  %a6 = load volatile i32, i32* @a
  %x6 = add i32 %x5, %a6
  %a7 = load volatile i32, i32* @a
  %x7 = add i32 %x6, %a6
  %a8 = load volatile i32, i32* @a
  %x8 = add i32 %x7, %a8
  %a9 = load volatile i32, i32* @a
  %x9 = add i32 %x8, %a9
  %a10 = load volatile i32, i32* @a
  %x10 = add i32 %x9, %a10
  %a11 = load volatile i32, i32* @a
  %x11 = add i32 %x10, %a11
  %a12 = load volatile i32, i32* @a
  %x12 = add i32 %x11, %a12
  %add = add i32 %x12, %a
  ret i32 %add
}

; Function Attrs: nounwind readnone uwtable
define i32 @bar(i32 %a) #0 {
entry:
  %0 = tail call i32 @simpleFunction(i32 6)
  ret i32 %0
}

declare void @extern()

attributes #0 = { nounwind readnone uwtable }
attributes #1 = { nounwind cold readnone uwtable }


; NOTE: Need to make sure the function inling have the same behaviour as O3 and
;       'BaselineConfig'
; DEFAULT: Inlining calls in: bar
; DEFAULT: Inlining (cost=115, threshold=375), Call:   %0 = tail call i32 @simpleFunction(i32 6)

; AUTOTUNE:      Pass:            inline
; AUTOTUNE-NEXT: Name:            simpleFunction
; AUTOTUNE-NEXT: Function:        bar
; AUTOTUNE-NEXT: CodeRegionType:  callsite
; AUTOTUNE-NEXT: CodeRegionHash:  {{[0-9]+}}
; AUTOTUNE-NEXT: DynamicConfigs:  { ForceInline: [ 0, 1 ] }
; AUTOTUNE-NEXT: BaselineConfig:  { ForceInline: '1' }
; AUTOTUNE-NEXT: Invocation:      0
