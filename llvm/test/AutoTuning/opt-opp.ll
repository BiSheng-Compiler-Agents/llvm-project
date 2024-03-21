; REQUIRES: asserts
; REQUIRES: x86-registered-target

; RUN: rm %t.default_opp -rf
; RUN: opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-opp=%t.default_opp --disable-output
; RUN: FileCheck %s --input-file %t.default_opp/opt-opp.ll.yaml  -check-prefix=DEFAULT

; RUN: rm %t.module_opp -rf
; RUN: opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-opp=%t.module_opp -auto-tuning-type-filter=Other --disable-output
; RUN: FileCheck %s --input-file %t.module_opp/opt-opp.ll.yaml -check-prefix=OTHER

; RUN: rm %t.loop_opp -rf
; RUN: opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-opp=%t.loop_opp -auto-tuning-type-filter=Loop --disable-output
; RUN: FileCheck %s --input-file %t.loop_opp/opt-opp.ll.yaml -check-prefix=LOOP

; RUN: rm %t.callsite_opp -rf
; RUN: opt %s -S -passes='function(require<opt-remark-emit>,loop-unroll),cgscc(inline)' \
; RUN:     -auto-tuning-opp=%t.callsite_opp -auto-tuning-type-filter=CallSite --disable-output
; RUN: FileCheck %s --input-file %t.callsite_opp/opt-opp.ll.yaml -check-prefix=CALLSITE

; RUN: rm %t.callsite_loop_opp -rf
; RUN: opt %s -S -passes='function(require<opt-remark-emit>,loop-unroll),cgscc(inline)' \
; RUN:     -auto-tuning-opp=%t.callsite_loop_opp -auto-tuning-type-filter=CallSite,Loop --disable-output
; RUN: FileCheck %s --input-file %t.callsite_loop_opp/opt-opp.ll.yaml -check-prefix=CALLSITE-LOOP1
; RUN: FileCheck %s --input-file %t.callsite_loop_opp/opt-opp.ll.yaml -check-prefix=CALLSITE-LOOP2

; RUN: rm %t.llvm_param_opp -rf
; RUN: opt %s -S -auto-tuning-opp=%t.llvm_param_opp \
; RUN:     -passes='function(require<opt-remark-emit>,loop-unroll),cgscc(inline)' \
; RUN:     -auto-tuning-type-filter=LLVMParam --disable-output
; RUN: FileCheck %s --input-file %t.llvm_param_opp/opt-opp.ll.yaml -check-prefix=LLVMPARAM

; RUN: rm %t.program_param_opp -rf
; RUN: opt %s -S -passes='function(require<opt-remark-emit>,loop-unroll),cgscc(inline)' \
; RUN:     -auto-tuning-opp=%t.program_param_opp -auto-tuning-type-filter=ProgramParam --disable-output
; RUN: FileCheck %s --input-file %t.program_param_opp/opt-opp.ll.yaml -check-prefix=ProgramPARAM

; Test if opp file with the same name exists already
; RUN: rm %t.default_opp -rf
; RUN: mkdir %t.default_opp && touch %t.default_opp/opt-opp.ll.yaml
; RUN: opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-opp=%t.default_opp --disable-output
; RUN: FileCheck %s --input-file %t.default_opp/opt-opp.ll.yaml.1 -check-prefix=DEFAULT

; Test that the loop code region is included if its size >= the threshold.
; RUN: rm %t.loop.opp -rf
; RUN: opt %s -S -auto-tuning-opp=%t.loop.opp -auto-tuning-size-threshold=13 \
; RUN:     -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -debug-only=autotuning --disable-output 2>&1 | \
; RUN:     FileCheck %s -check-prefix=SIZE-LOOP
; RUN: FileCheck %s --input-file %t.loop.opp/opt-opp.ll.yaml -check-prefix=SIZE-LOOP-OPP

; Test that the loop code region is excluded if its size < the threshold.
; RUN: rm %t.loop.opp -rf
; RUN: opt %s -S -auto-tuning-opp=%t.loop.opp -auto-tuning-size-threshold=14 \
; RUN:     -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -debug-only=autotuning --disable-output 2>&1 | \
; RUN:     FileCheck %s -check-prefix=SIZE-LOOP-FILTERED
; RUN: FileCheck %s --input-file %t.loop.opp/opt-opp.ll.yaml -check-prefix=SIZE-LOOP-OPP-FILTERED

; Test that the callsite code region is included if its size >= the threshold.
; RUN: rm %t.callsite.opp -rf
; RUN: opt %s -S -passes=inline -auto-tuning-opp=%t.callsite.opp --disable-output \
; RUN:     -auto-tuning-size-threshold=2 -debug-only=autotuning 2>&1 | \
; RUN:     FileCheck %s -check-prefix=SIZE-CALLSITE
; RUN: FileCheck %s --input-file %t.callsite.opp/opt-opp.ll.yaml -check-prefix=SIZE-CALLSITE-OPP

; Test that the callsite code region is excluded if its size < the threshold.
; RUN: rm %t.callsite.opp -rf
; RUN: opt %s -S -passes=inline -auto-tuning-opp=%t.callsite.opp \
; RUN:     -auto-tuning-size-threshold=24 --disable-output -debug-only=autotuning \
; RUN:     2>&1 | FileCheck %s -check-prefix=SIZE-CALLSITE-FILTERED
; RUN: FileCheck %s --input-file %t.callsite.opp/opt-opp.ll.yaml -check-prefix=SIZE-CALLSITE-OPP-FILTERED

; RUN: rm -rf %t.other
; RUN: opt %s -S -O3  -auto-tuning-opp=%t.other -auto-tuning-type-filter=Other
; RUN: grep "Name: \+'%S/opt-opp.ll'" %t.other/opt-opp.ll.yaml
; RUN: not grep "Name: \+opt-opp.ll" %t.other/opt-opp.ll.yaml

; RUN: rm -rf %t.other
; RUN: opt %s -S -O3  -auto-tuning-opp=%t.other -auto-tuning-type-filter=Other \
; RUN:     -autotuning-project-dir=%S/
; RUN: not grep "Name: \+'%S/opt-opp.ll'" %t.other/opt-opp.ll.yaml
; RUN: grep "Name: \+opt-opp.ll" %t.other/opt-opp.ll.yaml

; UNSUPPORTED: windows

; ModuleID = 'loop-opp.c'
source_filename = "loop-opp.c"
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: noinline nounwind uwtable
define i32 @test(i32* %n) #0 !dbg !6 {
entry:
  call void @callee(i32 6), !dbg !18
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

@a = global i32 4
define void @callee(i32 %a) #2 {
entry:
  %a1 = load volatile i32, i32* @a
  %x1 = add i32 %a1,  %a1
  %add = add i32 %x1, %a
  ret void
}

; Function Attrs: nounwind readnone
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

attributes #0 = { noinline nounwind uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind readnone }
attributes #2 = { nounwind uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4}
!llvm.ident = !{!5}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "" ,isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, enums: !2)
!1 = !DIFile(filename: "loop-opp.c", directory: "")
!2 = !{}
!3 = !{i32 2, !"Dwarf Version", i32 4}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!5 = !{!""}
!6 = distinct !DISubprogram(name: "test", scope: !1, file: !1, line: 1, type: !7, isLocal: false, isDefinition: true, scopeLine: 2, flags: DIFlagPrototyped, isOptimized: false, unit: !0)
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

; DEFAULT: --- !AutoTuning
; DEFAULT-NEXT: Pass:            loop-unroll
; DEFAULT-NEXT: Name:            for.cond
; DEFAULT-NEXT: DebugLoc:        { File: loop-opp.c, Line: 4, Column: 5 }
; DEFAULT-NEXT: Function:        test
; DEFAULT-NEXT: CodeRegionType:  loop
; DEFAULT-NEXT: CodeRegionHash:  {{[0-9]+}}
; DEFAULT-NEXT: DynamicConfigs:  { UnrollCount: [ {{[0-9]+(, [0-9]+)*}} ] }
; DEFAULT-NEXT: BaselineConfig:  { UnrollCount: '{{[0-9]+}}' }
; DEFAULT-NEXT: Invocation:      0
; DEFAULT-NEXT: ...
; DEFAULT-NEXT: --- !AutoTuning
; DEFAULT-NEXT: Pass:            all
; DEFAULT-NEXT: Name:
; DEFAULT-SAME: opt-opp.ll
; DEFAULT-NEXT: Function:        none
; DEFAULT-NEXT: CodeRegionType:  other
; COM: Module level hashes can differ based on the filepath so we check a regex
; DEFAULT-NEXT: CodeRegionHash:  {{[0-9]+}}
; DEFAULT-NEXT: DynamicConfigs:  { }
; DEFAULT-NEXT: BaselineConfig:  { }
; DEFAULT-NEXT: Invocation:      0
; DEFAULT-NEXT: ...

; LOOP: --- !AutoTuning
; LOOP-NEXT: Pass:            loop-unroll
; LOOP-NEXT: Name:            for.cond
; LOOP-NEXT: DebugLoc:        { File: loop-opp.c, Line: 4, Column: 5 }
; LOOP-NEXT: Function:        test
; LOOP-NEXT: CodeRegionType:  loop
; LOOP-NEXT: CodeRegionHash:  {{[0-9]+}}
; LOOP-NEXT: DynamicConfigs:  { UnrollCount: [ {{[0-9]+(, [0-9]+)*}} ] }
; LOOP-NEXT: BaselineConfig:  { UnrollCount: '{{[0-9]+}}' }
; LOOP-NEXT: Invocation:      0
; LOOP-NEXT: ...

; CALLSITE: --- !AutoTuning
; CALLSITE-NEXT: Pass:            inline
; CALLSITE-NEXT: Name:            callee
; CALLSITE-NEXT: DebugLoc:        { File: loop-opp.c, Line: 4, Column: 14 }
; CALLSITE-NEXT: Function:        test
; CALLSITE-NEXT: CodeRegionType:  callsite
; CALLSITE-NEXT: CodeRegionHash:  {{[0-9]+}}
; CALLSITE-NEXT: DynamicConfigs:  { ForceInline: [ 0, 1 ] }
; CALLSITE-NEXT: BaselineConfig:  { ForceInline: '1' }
; CALLSITE-NEXT: Invocation:      0
; CALLSITE-NEXT: ...

; CALLSITE-LOOP1: CodeRegionType:  loop
; CALLSITE-LOOP1-NOT: CodeRegionType:  other
; CALLSITE-LOOP2: CodeRegionType:  callsite
; CALLSITE-LOOP2-NOT: CodeRegionType:  other

; OTHER: --- !AutoTuning
; OTHER-NEXT: Pass:            all
; OTHER-NEXT: Name:
; OTHER-SAME: opt-opp.ll
; OTHER-NEXT: Function:        none
; OTHER-NEXT: CodeRegionType:  other
; COM: Module level hashes can differ based on the filepath so we check a regex
; OTHER-NEXT: CodeRegionHash:  {{[0-9]+}}
; OTHER-NEXT: DynamicConfigs:  { }
; OTHER-NEXT: BaselineConfig:  { }
; OTHER-NEXT: Invocation:      0
; OTHER-NEXT: ...

; LLVMPARAM: --- !AutoTuning
; LLVMPARAM-NEXT: Pass:            none
; LLVMPARAM-NEXT: Name:
; LLVMPARAM-SAME: opt-opp.ll
; LLVMPARAM-NEXT: Function:        none
; LLVMPARAM-NEXT: CodeRegionType:  llvm-param
; LLVMPARAM-NEXT: CodeRegionHash:  {{[0-9]+}}
; LLVMPARAM-NEXT: DynamicConfigs:  { }
; LLVMPARAM-NEXT: BaselineConfig:  { }
; LLVMPARAM-NEXT: Invocation:      0
; LLVMPARAM-NEXT: ...

; ProgramPARAM: --- !AutoTuning
; ProgramPARAM-NEXT: Pass:            none
; ProgramPARAM-NEXT: Name:
; ProgramPARAM-SAME: opt-opp.ll
; ProgramPARAM-NEXT: Function:        none
; ProgramPARAM-NEXT: CodeRegionType:  program-param
; ProgramPARAM-NEXT: CodeRegionHash:  {{[0-9]+}}
; ProgramPARAM-NEXT: DynamicConfigs:  { }
; ProgramPARAM-NEXT: BaselineConfig:  { }
; ProgramPARAM-NEXT: Invocation:      0
; ProgramPARAM-NEXT: ...

; SIZE-LOOP:  PassName: loop-unroll
; SIZE-LOOP-NEXT:  Type: loop
; SIZE-LOOP-NEXT:  Size: 13
; SIZE-LOOP:  Module added as an tuning opportunity

; SIZE-LOOP-OPP-DAG: Pass:            loop-unroll
; SIZE-LOOP-OPP-DAG: Pass:            all

; SIZE-LOOP-FILTERED-NOT:  PassName: loop-unroll
; SIZE-LOOP-FILTERED:  Module added as an tuning opportunity

; SIZE-LOOP-OPP-FILTERED-NOT: Pass:            loop-unroll
; Ths "other" code regions should remain as-is.
; SIZE-LOOP-OPP-FILTERED: CodeRegionType:  other

; SIZE-CALLSITE:  PassName: inline
; SIZE-CALLSITE-NEXT:  Type: callsite
; SIZE-CALLSITE-NEXT:  Size: 4
; SIZE-CALLSITE:  Module added as an tuning opportunity

; SIZE-CALLSITE-OPP-DAG: Pass:            inline
; SIZE-CALLSITE-OPP-DAG: Pass:            all

; SIZE-CALLSITE-FILTERED-NOT:  PassName: inline
; SIZE-CALLSITE-FILTERED:  Module added as an tuning opportunity

; SIZE-CALLSITE-OPP-FILTERED-NOT: Pass:            inline
; Ths "other" code regions should remain as-is.
; SIZE-CALLSITE-OPP-FILTERED: CodeRegionType:  other
