; REQUIRES: asserts
; RUN: opt < %s -passes=inline -debug-only=inline -disable-output -S 2>&1 | FileCheck %s -check-prefix=DEFAULT
; simpleFunction will be inlined with the default behavior.

; RUN: rm %t.force-inline.yaml -rf
; RUN: sed 's#\[force-inline\]#true#g' %S/Inputs/template.yaml > %t.force-inline.yaml
; RUN: opt %s -passes=inline -debug-only=inline -disable-output -S \
; RUN:     -auto-tuning-input=%t.force-inline.yaml 2>&1 | \
; RUN:     FileCheck %s -check-prefix=FORCE-INLINE
; Test with ForceInline=true;

; RUN: rm %t.force-inline.yaml -rf
; RUN: sed 's#\[force-inline\]#true#g' %S/Inputs/template_no_metadata.yaml > %t.force-inline.yaml
; RUN: opt %s -passes=inline -S -auto-tuning-input=%t.force-inline.yaml \
; RUN:     -debug-only=inline -disable-output -auto-tuning-omit-metadata 2>&1 | \
; RUN:     FileCheck %s -check-prefix=FORCE-INLINE
; Test with ForceInline=true;

; RUN: rm %t.no-inline.yaml -rf
; RUN: sed 's#\[force-inline\]#false#g' %S/Inputs/template.yaml > %t.no-inline.yaml
; RUN: opt %s -passes=inline -debug-only=inline -disable-output -S \
; RUN:     -auto-tuning-input=%t.no-inline.yaml 2>&1 | \
; RUN:     FileCheck %s -check-prefix=NO-INLINE
; Test with ForceInline=false;

; RUN: rm %t.no-inline.yaml -rf
; RUN: sed 's#\[force-inline\]#false#g' %S/Inputs/template_no_metadata.yaml > %t.no-inline.yaml
; RUN: opt %s -passes='cgscc(inline)' -debug-only=inline -disable-output -S \
; RUN:     -auto-tuning-input=%t.no-inline.yaml -auto-tuning-omit-metadata 2>&1 | \
; RUN:     FileCheck %s -check-prefix=NO-INLINE
; Test with ForceInline=false;

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

; DEFAULT: Inlining (cost=120, threshold=337)
; DEFAULT-SAME: simpleFunction
; FORCE-INLINE: Inlining (cost=always): Force inlined by auto-tuning
; FORCE-INLINE-SAME: simpleFunction
; NO-INLINE: NOT Inlining (cost=never): Force non-inlined by auto-tuning
; NO-INLINE-SAME: simpleFunction
