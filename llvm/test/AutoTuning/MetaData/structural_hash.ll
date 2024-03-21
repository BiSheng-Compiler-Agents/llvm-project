; RUN: rm %t.hash_opp -rf
; RUN: opt %s -S -passes='function(require<opt-remark-emit>,loop-unroll),cgscc(inline)' \
; RUN:     -auto-tuning-opp=%t.hash_opp -auto-tuning-type-filter=CallSite --disable-output
; RUN: FileCheck %s --input-file %t.hash_opp/structural_hash.ll.yaml -check-prefix=META-CALL1
; RUN: FileCheck %s --input-file %t.hash_opp/structural_hash.ll.yaml -check-prefix=META-CALL2
; RUN: FileCheck %s --input-file %t.hash_opp/structural_hash.ll.yaml -check-prefix=META-CALL3

; RUN: rm %t.hash_opp -rf
; RUN: opt %s -S -passes='function(require<opt-remark-emit>,loop-unroll),cgscc(inline)' \
; RUN:     -auto-tuning-type-filter=CallSite -auto-tuning-opp=%t.hash_opp \
; RUN:     -auto-tuning-omit-metadata --disable-output
; RUN: FileCheck %s --input-file %t.hash_opp/structural_hash.ll.yaml -check-prefix=NO-META-CALL

; UNSUPPORTED: windows

; ModuleID = 'loop_small.cpp'
source_filename = "loop_small.cpp"
target datalayout = "e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128"
target triple = "aarch64-unknown-linux-gnu"

@arr = dso_local global [1000000 x i32] zeroinitializer, align 4, !dbg !0

; Function Attrs: nounwind uwtable mustprogress
define dso_local void @_Z1fv() #0 !dbg !18 {
entry:
  %i = alloca i32, align 4
  call void @llvm.dbg.declare(metadata i32* %i, metadata !21, metadata !DIExpression()), !dbg !23
  store i32 0, i32* %i, align 4, !dbg !23
  br label %for.cond, !dbg !24

for.cond:                                         ; preds = %for.inc, %entry
  %0 = load i32, i32* %i, align 4, !dbg !25
  %cmp = icmp slt i32 %0, 2000, !dbg !27
  br i1 %cmp, label %for.body, label %for.end, !dbg !28

for.body:                                         ; preds = %for.cond
  %1 = load i32, i32* %i, align 4, !dbg !29
  %idxprom = sext i32 %1 to i64, !dbg !31
  %arrayidx = getelementptr inbounds [1000000 x i32], [1000000 x i32]* @arr, i64 0, i64 %idxprom, !dbg !31
  %2 = load i32, i32* %arrayidx, align 4, !dbg !32
  %add = add nsw i32 %2, 2, !dbg !32
  store i32 %add, i32* %arrayidx, align 4, !dbg !32
  br label %for.inc, !dbg !33

for.inc:                                          ; preds = %for.body
  %3 = load i32, i32* %i, align 4, !dbg !34
  %inc = add nsw i32 %3, 1, !dbg !34
  store i32 %inc, i32* %i, align 4, !dbg !34
  br label %for.cond, !dbg !35, !llvm.loop !36

for.end:                                          ; preds = %for.cond
  ret void, !dbg !39
}

; Function Attrs: nofree nosync nounwind readnone speculatable willreturn
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

; Function Attrs: nounwind uwtable mustprogress
define dso_local void @_Z1gv() #0 !dbg !40 {
entry:
  %0 = load i32, i32* getelementptr inbounds ([1000000 x i32], [1000000 x i32]* @arr, i64 0, i64 0), align 4, !dbg !41
  %inc = add nsw i32 %0, 1, !dbg !41
  store i32 %inc, i32* getelementptr inbounds ([1000000 x i32], [1000000 x i32]* @arr, i64 0, i64 0), align 4, !dbg !41
  ret void, !dbg !42
}

; Function Attrs: norecurse nounwind uwtable mustprogress
define dso_local i32 @main() #2 !dbg !43 {
entry:
  %retval = alloca i32, align 4
  %i = alloca i32, align 4
  store i32 0, i32* %retval, align 4
  call void @llvm.dbg.declare(metadata i32* %i, metadata !46, metadata !DIExpression()), !dbg !48
  store i32 0, i32* %i, align 4, !dbg !48
  br label %for.cond, !dbg !49

for.cond:                                         ; preds = %for.inc, %entry
  %0 = load i32, i32* %i, align 4, !dbg !50
  %cmp = icmp slt i32 %0, 1000000, !dbg !52
  br i1 %cmp, label %for.body, label %for.end, !dbg !53

for.body:                                         ; preds = %for.cond
  %1 = load i32, i32* %i, align 4, !dbg !54
  %idxprom = sext i32 %1 to i64, !dbg !55
  %arrayidx = getelementptr inbounds [1000000 x i32], [1000000 x i32]* @arr, i64 0, i64 %idxprom, !dbg !55
  store i32 0, i32* %arrayidx, align 4, !dbg !56
  br label %for.inc, !dbg !55

for.inc:                                          ; preds = %for.body
  %2 = load i32, i32* %i, align 4, !dbg !57
  %inc = add nsw i32 %2, 1, !dbg !57
  store i32 %inc, i32* %i, align 4, !dbg !57
  br label %for.cond, !dbg !58, !llvm.loop !59

for.end:                                          ; preds = %for.cond
  call void @_Z1fv(), !dbg !61
  call void @_Z1gv(), !dbg !62
  call void @_Z1fv(), !dbg !63
  %3 = load i32, i32* %retval, align 4, !dbg !64
  ret i32 %3, !dbg !64
}

attributes #0 = { nounwind uwtable mustprogress "disable-tail-calls"="false" "frame-pointer"="non-leaf" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+neon" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nofree nosync nounwind readnone speculatable willreturn }
attributes #2 = { norecurse nounwind uwtable mustprogress "disable-tail-calls"="false" "frame-pointer"="non-leaf" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+neon" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.dbg.cu = !{!2}
!llvm.module.flags = !{!10, !11, !12, !13, !14, !15, !16}
!llvm.ident = !{!17}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(name: "arr", scope: !2, file: !3, line: 1, type: !6, isLocal: false, isDefinition: true)
!2 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: !3, producer: "Huawei Bisheng Compiler clang version 12.0.0 (clang-6d7704116510 flang-6d7704116510)", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, enums: !4, globals: !5, splitDebugInlining: false, nameTableKind: None)
!3 = !DIFile(filename: "loop_small.cpp", directory: "/home/g84189222/boole3/llvm-project/tuneTest")
!4 = !{}
!5 = !{!0}
!6 = !DICompositeType(tag: DW_TAG_array_type, baseType: !7, size: 32000000, elements: !8)
!7 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!8 = !{!9}
!9 = !DISubrange(count: 1000000)
!10 = !{i32 7, !"Dwarf Version", i32 4}
!11 = !{i32 2, !"Debug Info Version", i32 3}
!12 = !{i32 1, !"wchar_size", i32 4}
!13 = !{i32 1, !"branch-target-enforcement", i32 0}
!14 = !{i32 1, !"sign-return-address", i32 0}
!15 = !{i32 1, !"sign-return-address-all", i32 0}
!16 = !{i32 1, !"sign-return-address-with-bkey", i32 0}
!17 = !{!"Huawei Bisheng Compiler clang version 12.0.0 (clang-6d7704116510 flang-6d7704116510)"}
!18 = distinct !DISubprogram(name: "f", linkageName: "_Z1fv", scope: !3, file: !3, line: 3, type: !19, scopeLine: 3, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !2, retainedNodes: !4)
!19 = !DISubroutineType(types: !20)
!20 = !{null}
!21 = !DILocalVariable(name: "i", scope: !22, file: !3, line: 4, type: !7)
!22 = distinct !DILexicalBlock(scope: !18, file: !3, line: 4, column: 2)
!23 = !DILocation(line: 4, column: 10, scope: !22)
!24 = !DILocation(line: 4, column: 6, scope: !22)
!25 = !DILocation(line: 4, column: 15, scope: !26)
!26 = distinct !DILexicalBlock(scope: !22, file: !3, line: 4, column: 2)
!27 = !DILocation(line: 4, column: 16, scope: !26)
!28 = !DILocation(line: 4, column: 2, scope: !22)
!29 = !DILocation(line: 5, column: 7, scope: !30)
!30 = distinct !DILexicalBlock(scope: !26, file: !3, line: 4, column: 27)
!31 = !DILocation(line: 5, column: 3, scope: !30)
!32 = !DILocation(line: 5, column: 10, scope: !30)
!33 = !DILocation(line: 6, column: 2, scope: !30)
!34 = !DILocation(line: 4, column: 24, scope: !26)
!35 = !DILocation(line: 4, column: 2, scope: !26)
!36 = distinct !{!36, !28, !37, !38}
!37 = !DILocation(line: 6, column: 2, scope: !22)
!38 = !{!"llvm.loop.mustprogress"}
!39 = !DILocation(line: 7, column: 1, scope: !18)
!40 = distinct !DISubprogram(name: "g", linkageName: "_Z1gv", scope: !3, file: !3, line: 8, type: !19, scopeLine: 8, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !2, retainedNodes: !4)
!41 = !DILocation(line: 9, column: 8, scope: !40)
!42 = !DILocation(line: 10, column: 1, scope: !40)
!43 = distinct !DISubprogram(name: "main", scope: !3, file: !3, line: 12, type: !44, scopeLine: 12, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !2, retainedNodes: !4)
!44 = !DISubroutineType(types: !45)
!45 = !{!7}
!46 = !DILocalVariable(name: "i", scope: !47, file: !3, line: 13, type: !7)
!47 = distinct !DILexicalBlock(scope: !43, file: !3, line: 13, column: 2)
!48 = !DILocation(line: 13, column: 10, scope: !47)
!49 = !DILocation(line: 13, column: 6, scope: !47)
!50 = !DILocation(line: 13, column: 15, scope: !51)
!51 = distinct !DILexicalBlock(scope: !47, file: !3, line: 13, column: 2)
!52 = !DILocation(line: 13, column: 16, scope: !51)
!53 = !DILocation(line: 13, column: 2, scope: !47)
!54 = !DILocation(line: 13, column: 35, scope: !51)
!55 = !DILocation(line: 13, column: 31, scope: !51)
!56 = !DILocation(line: 13, column: 38, scope: !51)
!57 = !DILocation(line: 13, column: 27, scope: !51)
!58 = !DILocation(line: 13, column: 2, scope: !51)
!59 = distinct !{!59, !53, !60, !38}
!60 = !DILocation(line: 13, column: 40, scope: !47)
!61 = !DILocation(line: 14, column: 2, scope: !43)
!62 = !DILocation(line: 15, column: 2, scope: !43)
!63 = !DILocation(line: 16, column: 2, scope: !43)
!64 = !DILocation(line: 17, column: 1, scope: !43)

; META-CALL1: --- !AutoTuning
; META-CALL1: Pass:           inline
; META-CALL1: Name:           _Z1fv
; META-CALL1: DebugLoc:       { File: loop_small.cpp, Line: 14, Column: 2 }
; META-CALL1-NEXT: Function:       main
; META-CALL1-NEXT: CodeRegionType: callsite
; META-CALL1-NEXT: CodeRegionHash: {{[0-9]+}}
; META-CALL1-NEXT: DynamicConfigs: { ForceInline: [ 0, 1 ] }
; META-CALL1-NEXT: BaselineConfig: { ForceInline: '1' }
; META-CALL1-NEXT: Invocation:     0
; META-CALL1-NEXT: ...
; META-CALL2: --- !AutoTuning
; META-CALL2: Pass:           inline
; META-CALL2: Name:           _Z1fv
; META-CALL2: DebugLoc:       { File: loop_small.cpp, Line: 16, Column: 2 }
; META-CALL2-NEXT: Function:       main
; META-CALL2-NEXT: CodeRegionType: callsite
; META-CALL2-NEXT: CodeRegionHash: {{[0-9]+}}
; META-CALL2-NEXT: DynamicConfigs: { ForceInline: [ 0, 1 ] }
; META-CALL2-NEXT: BaselineConfig: { ForceInline: '1' }
; META-CALL2-NEXT: Invocation:     0
; META-CALL2-NEXT: ...
; META-CALL3: --- !AutoTuning
; META-CALL3: Pass:           inline
; META-CALL3: Name:           _Z1gv
; META-CALL3: DebugLoc:       { File: loop_small.cpp, Line: 15, Column: 2 }
; META-CALL3-NEXT: Function:       main
; META-CALL3-NEXT: CodeRegionType: callsite
; META-CALL3-NEXT: CodeRegionHash: {{[0-9]+}}
; META-CALL3-NEXT: DynamicConfigs: { ForceInline: [ 0, 1 ] }
; META-CALL3-NEXT: BaselineConfig: { ForceInline: '1' }
; META-CALL3-NEXT: Invocation:     0
; META-CALL3-NEXT: ...

; NO-META-CALL: --- !AutoTuning
; NO-META-CALL-NEXT: Pass:           inline
; NO-META-CALL-NEXT: CodeRegionType: callsite
; NO-META-CALL-NEXT: CodeRegionHash: {{[0-9]+}}
; NO-META-CALL-NEXT: DynamicConfigs: { ForceInline: [ 0, 1 ] }
; NO-META-CALL-NEXT: BaselineConfig: { ForceInline: '1' }
; NO-META-CALL-NEXT: Invocation:     0
; NO-META-CALL-NEXT: ...
; NO-META-CALL-NEXT: --- !AutoTuning
; NO-META-CALL-NEXT: Pass:           inline
; NO-META-CALL-NEXT: CodeRegionType: callsite
; NO-META-CALL-NEXT: CodeRegionHash: {{[0-9]+}}
; NO-META-CALL-NEXT: DynamicConfigs: { ForceInline: [ 0, 1 ] }
; NO-META-CALL-NEXT: BaselineConfig: { ForceInline: '1' }
; NO-META-CALL-NEXT: Invocation:     0
; NO-META-CALL-NEXT: ...
; NO-META-CALL-NEXT: --- !AutoTuning
; NO-META-CALL-NEXT: Pass:           inline
; NO-META-CALL-NEXT: CodeRegionType: callsite
; NO-META-CALL-NEXT: CodeRegionHash: {{[0-9]+}}
; NO-META-CALL-NEXT: DynamicConfigs: { ForceInline: [ 0, 1 ] }
; NO-META-CALL-NEXT: BaselineConfig: { ForceInline: '1' }
; NO-META-CALL-NEXT: Invocation:     0
; NO-META-CALL-NEXT: ...
