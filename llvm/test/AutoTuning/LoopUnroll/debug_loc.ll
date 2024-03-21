; RUN: opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' | \
; RUN:     FileCheck %s -check-prefix=DISABLE

; RUN: rm %t.unroll_debug_loc0.yaml -rf
; RUN: sed 's#\[number\]#0#g' %S/Inputs/debug_loc_template.yaml > %t.unroll_debug_loc0.yaml
; RUN: opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-input=%t.unroll_debug_loc0.yaml | \
; RUN:     FileCheck %s -check-prefix=UNROLL0

; RUN: rm %t.unroll_debug_loc4.yaml -rf
; RUN: sed 's#\[number\]#4#g' %S/Inputs/debug_loc_template.yaml > %t.unroll_debug_loc4.yaml
; RUN: opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-code-region-matching-hash=false \
; RUN:     -auto-tuning-input=%t.unroll_debug_loc4.yaml | \
; RUN:     FileCheck %s -check-prefix=UNROLL4

; RUN: rm %t.unroll4.yaml -rf
; RUN: sed 's#\[number\]#4#g; s#\[name\]#for.cond#g; s#\[hash\]#11552168367013316892#g;'\
; RUN:     %S/Inputs/unroll_template.yaml > %t.unroll4.yaml
; RUN: opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-code-region-matching-hash=false \
; RUN:     -auto-tuning-input=%t.unroll4.yaml | \
; RUN:     FileCheck %s -check-prefix=UNROLL4-MISMATCH

; UNSUPPORTED: windows

; ModuleID = 'loop-opp.c'
source_filename = "loop-opp.c"
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: noinline nounwind uwtable
define i32 @foo(i32* %n) #0 !dbg !6 {
entry:
  %n.addr = alloca i32*, align 8
  %b = alloca i32, align 4
  %i = alloca i32, align 4
  store i32* %n, i32** %n.addr, align 8
  call void @llvm.dbg.declare(metadata i32** %n.addr, metadata !11, metadata !12), !dbg !13
  call void @llvm.dbg.declare(metadata i32* %b, metadata !14, metadata !12), !dbg !15
  store i32 0, i32* %b, align 4, !dbg !15
  call void @llvm.dbg.declare(metadata i32* %i, metadata !16, metadata !12), !dbg !18
  store i32 0, i32* %i, align 4, !dbg !18
  br label %for.cond, !dbg !19

for.cond:                                         ; preds = %for.inc, %entry
  %0 = load i32, i32* %i, align 4, !dbg !20
  %1 = load i32*, i32** %n.addr, align 8, !dbg !23
  %2 = load i32, i32* %1, align 4, !dbg !24
  %cmp = icmp slt i32 %0, %2, !dbg !25
  br i1 %cmp, label %for.body, label %for.end, !dbg !26

for.body:                                         ; preds = %for.cond
  %3 = load i32, i32* %b, align 4, !dbg !28
  %add = add nsw i32 %3, 1, !dbg !30
  store i32 %add, i32* %b, align 4, !dbg !31
  br label %for.inc, !dbg !32

for.inc:                                          ; preds = %for.body
  %4 = load i32, i32* %i, align 4, !dbg !33
  %inc = add nsw i32 %4, 1, !dbg !33
  store i32 %inc, i32* %i, align 4, !dbg !33
  br label %for.cond, !dbg !35, !llvm.loop !36

for.end:                                          ; preds = %for.cond
  %5 = load i32, i32* %b, align 4, !dbg !39
  ret i32 %5, !dbg !40
}

; Function Attrs: nounwind readnone
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

attributes #0 = { noinline nounwind uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind readnone }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4}
!llvm.ident = !{!5}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "" ,isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, enums: !2)
!1 = !DIFile(filename: "loop-opp.c", directory: "")
!2 = !{}
!3 = !{i32 2, !"Dwarf Version", i32 4}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!5 = !{!""}
!6 = distinct !DISubprogram(name: "foo", scope: !1, file: !1, line: 1, type: !7, isLocal: false, isDefinition: true, scopeLine: 2, flags: DIFlagPrototyped, isOptimized: false, unit: !0)
!7 = !DISubroutineType(types: !8)
!8 = !{!9, !10}
!9 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!10 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !9, size: 64)
!11 = !DILocalVariable(name: "n", arg: 1, scope: !6, file: !1, line: 1, type: !10)
!12 = !DIExpression()
!13 = !DILocation(line: 1, column: 20, scope: !6)
!14 = !DILocalVariable(name: "b", scope: !6, file: !1, line: 3, type: !9)
!15 = !DILocation(line: 3, column: 9, scope: !6)
!16 = !DILocalVariable(name: "i", scope: !17, file: !1, line: 4, type: !9)
!17 = distinct !DILexicalBlock(scope: !6, file: !1, line: 4, column: 5)
!18 = !DILocation(line: 4, column: 14, scope: !17)
!19 = !DILocation(line: 4, column: 10, scope: !17)
!20 = !DILocation(line: 4, column: 20, scope: !21)
!21 = !DILexicalBlockFile(scope: !22, file: !1, discriminator: 1)
!22 = distinct !DILexicalBlock(scope: !17, file: !1, line: 4, column: 5)
!23 = !DILocation(line: 4, column: 25, scope: !21)
!24 = !DILocation(line: 4, column: 24, scope: !21)
!25 = !DILocation(line: 4, column: 22, scope: !21)
!26 = !DILocation(line: 4, column: 5, scope: !27)
!27 = !DILexicalBlockFile(scope: !17, file: !1, discriminator: 1)
!28 = !DILocation(line: 6, column: 11, scope: !29)
!29 = distinct !DILexicalBlock(scope: !22, file: !1, line: 5, column: 5)
!30 = !DILocation(line: 6, column: 12, scope: !29)
!31 = !DILocation(line: 6, column: 9, scope: !29)
!32 = !DILocation(line: 7, column: 5, scope: !29)
!33 = !DILocation(line: 4, column: 28, scope: !34)
!34 = !DILexicalBlockFile(scope: !22, file: !1, discriminator: 2)
!35 = !DILocation(line: 4, column: 5, scope: !34)
!36 = distinct !{!36, !37, !38}
!37 = !DILocation(line: 4, column: 5, scope: !17)
!38 = !DILocation(line: 7, column: 5, scope: !17)
!39 = !DILocation(line: 8, column: 12, scope: !6)
!40 = !DILocation(line: 8, column: 5, scope: !6)

; Auto-tuning-enabled loop unrolling - check that the loop is not unrolled when the auto-tuning feature is disabled when
; the input remark contains DebugLoc info.
;
; DISABLE-LABEL: @foo(
; DISABLE: for.cond
; DISABLE: for.body
; DISABLE-NOT: for.body.1
; DISABLE: for.inc
; DISABLE-NOT: llvm.loop.unroll.disable

; Auto-tuning-enabled loop unrolling - check that the loop is not unrolled
; when unroll count explicitly set to be 0.
;
; UNROLL0-LABEL: @foo(
; UNROLL0: for.cond
; UNROLL0: for.body
; UNROLL0-NOT: for.body.1
; UNROLL0: for.inc
; UNROLL0-NOT: llvm.loop.unroll.disable

; Auto-tuning-enabled loop unrolling - check that we can unroll the loop by 4
; when explicitly requested.
;
; UNROLL4-LABEL: @foo(
; UNROLL4: for.cond
; UNROLL4: for.body
; UNROLL4: for.body.1
; UNROLL4: for.body.2
; UNROLL4: for.body.3
; UNROLL4: llvm.loop.unroll.disable

; Auto-tuning-enabled loop unrolling - check that the loop is not unrolled
; when DebugLoc is missing in the input remark.
;
; UNROLL4-MISMATCH-LABEL: @foo(
; UNROLL4-MISMATCH: for.cond
; UNROLL4-MISMATCH: for.body
; UNROLL4-MISMATCH-NOT: for.body.1
; UNROLL4-MISMATCH: for.inc
; UNROLL4-MISMATCH-NOT: llvm.loop.unroll.disable
