; RUN: rm %t.callsite_opp -rf
; RUN: sed 's#\[number\]#25#g; s#\[func_name\]#ColdFunction#g' %S/Inputs/template.yaml > %t.template25.yaml
; RUN: opt %s -passes=inline -S -auto-tuning-opp=%t.callsite_opp -auto-tuning-type-filter=CallSite

; RUN: FileCheck  %s  --input-file %t.callsite_opp/opp.ll.yaml -check-prefix=CALLSITE

@a = global i32 4

declare void @extern()
; Function Attrs: nounwind readnone uwtable
define i32 @simpleFunction(i32 %a) #1 {
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

define i32 @bar(i32 %a) #0 {
entry:
  %0 = tail call i32 @simpleFunction(i32 6)
  ret i32 %0
}

attributes #0 = { nounwind readnone uwtable }
attributes #1 = { nounwind cold readnone uwtable }

; Check if code regions are properly generated as tuning opportunities.
; CALLSITE: --- !AutoTuning
; CALLSITE-NEXT: Pass:            inline
; CALLSITE-NEXT: Name:            simpleFunction
; CALLSITE-NEXT: Function:        bar
; CALLSITE-NEXT: CodeRegionType:  callsite
; CALLSITE-NEXT: CodeRegionHash:  {{[0-9]+}}
; CALLSITE-NEXT: DynamicConfigs:  { ForceInline: [ 0, 1 ] }
; CALLSITE-NEXT: BaselineConfig:  { ForceInline: '1' }
; CALLSITE-NEXT: Invocation:      0
; CALLSITE-NEXT: ...

; Check if external functions are filtered out.
; EXTERNAL-NOT: Name:        extern
