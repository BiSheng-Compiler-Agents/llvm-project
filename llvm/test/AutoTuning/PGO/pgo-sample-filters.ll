; RUN: rm %t.default-opp -rf
; RUN: opt %s -passes='sample-profile,inline' -sample-profile-file=%S/Inputs/pgo-sample-cold.prof -auto-tuning-opp=%t.default-opp -auto-tuning-exclude-cold=false --disable-output -S
; RUN: FileCheck %s -check-prefix=NON-FILTER < %t.default-opp/pgo-sample-filters.ll.yaml

; Test -auto-tuning-exclude-cold with a cold caller in sample profile.
; RUN: rm %t.filtered-opp -rf
; RUN: opt %s -passes='sample-profile,inline' -sample-profile-file=%S/Inputs/pgo-sample-cold.prof -auto-tuning-opp=%t.filtered-opp -auto-tuning-exclude-cold --disable-output -S
; RUN: FileCheck %s -check-prefix=COLD-PROFILE-EXCLUDE-COLD < %t.filtered-opp/pgo-sample-filters.ll.yaml

; Test -auto-tuning-hot-only with a cold caller in sample profile.
; RUN: rm %t.filtered-opp -rf
; RUN: opt %s -passes='sample-profile,inline' -sample-profile-file=%S/Inputs/pgo-sample-cold.prof -auto-tuning-opp=%t.filtered-opp -auto-tuning-hot-only --disable-output -S
; RUN: FileCheck %s -check-prefix=COLD-PROFILE-HOT-ONLY < %t.filtered-opp/pgo-sample-filters.ll.yaml

; Test -auto-tuning-exclude-cold with a hot caller in sample profile.
; RUN: rm %t.filtered-opp -rf
; RUN: opt %s -passes='sample-profile,inline' -sample-profile-file=%S/Inputs/pgo-sample-hot.prof -auto-tuning-opp=%t.filtered-opp -auto-tuning-exclude-cold --disable-output -S
; RUN: FileCheck %s -check-prefix=HOT-PROFILE-EXCLUDE-COLD < %t.filtered-opp/pgo-sample-filters.ll.yaml

; Test -auto-tuning-hot-only with a hot caller in sample profile.
; RUN: rm %t.filtered-opp -rf
; RUN: opt %s -passes='sample-profile,inline' -sample-profile-file=%S/Inputs/pgo-sample-hot.prof -auto-tuning-opp=%t.filtered-opp -auto-tuning-hot-only --disable-output -S
; RUN: FileCheck %s -check-prefix=HOT-PROFILE-HOT-ONLY < %t.filtered-opp/pgo-sample-filters.ll.yaml


@.str = private unnamed_addr constant [11 x i8] c"sum is %d\0A\00", align 1

define i32 @_Z3sumii(i32 %x, i32 %y) #0 !dbg !6 {
entry:
  %0 = tail call i32 @callee(i32 5)
  %x.addr = alloca i32, align 4
  %y.addr = alloca i32, align 4
  store i32 %x, i32* %x.addr, align 4
  store i32 %y, i32* %y.addr, align 4
  %tmp = load i32, i32* %x.addr, align 4, !dbg !8
  %tmp1 = load i32, i32* %y.addr, align 4, !dbg !8
  %add = add nsw i32 %tmp, %tmp1, !dbg !8
  ret i32 %add, !dbg !8
}

define i32 @main() #0 !dbg !9 {
entry:
  %0 = tail call i32 @callee(i32 5)
  %retval = alloca i32, align 4
  %s = alloca i32, align 4
  %i = alloca i32, align 4
  store i32 0, i32* %retval
  store i32 0, i32* %i, align 4, !dbg !10
  br label %while.cond, !dbg !11

while.cond:                                       ; preds = %if.end, %entry
  %tmp = load i32, i32* %i, align 4, !dbg !12
  %inc = add nsw i32 %tmp, 1, !dbg !12
  store i32 %inc, i32* %i, align 4, !dbg !12
  %cmp = icmp slt i32 %tmp, 400000000, !dbg !12
  br i1 %cmp, label %while.body, label %while.end, !dbg !12

while.body:                                       ; preds = %while.cond
  %tmp1 = load i32, i32* %i, align 4, !dbg !14
  %cmp1 = icmp ne i32 %tmp1, 100, !dbg !14
  br i1 %cmp1, label %if.then, label %if.else, !dbg !14

if.then:                                          ; preds = %while.body
  %tmp2 = load i32, i32* %i, align 4, !dbg !16
  %tmp3 = load i32, i32* %s, align 4, !dbg !16
  %call = call i32 @_Z3sumii(i32 %tmp2, i32 %tmp3), !dbg !16
; INLINE-NOT: call i32 @_Z3sumii
; NOTINLINE: call i32 @_Z3sumii
  store i32 %call, i32* %s, align 4, !dbg !16
  br label %if.end, !dbg !16

if.else:                                          ; preds = %while.body
  store i32 30, i32* %s, align 4, !dbg !18
  br label %if.end

if.end:                                           ; preds = %if.else, %if.then
  br label %while.cond, !dbg !20

while.end:                                        ; preds = %while.cond
  %tmp4 = load i32, i32* %s, align 4, !dbg !22
  %call2 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([11 x i8], [11 x i8]* @.str, i32 0, i32 0), i32 %tmp4), !dbg !22
  ret i32 0, !dbg !23
}

define i32 @callee(i32 %a) #0 {
entry:
  %add = add nsw i32 %a, 4
  ret i32 %add
}

declare i32 @printf(i8*, ...)

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4}
!llvm.ident = !{!5}

attributes #0 = {"use-sample-profile"}

!0 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus, file: !1, producer: "clang version 3.5 ", isOptimized: false, runtimeVersion: 0, emissionKind: NoDebug, enums: !2, retainedTypes: !2, globals: !2, imports: !2)
!1 = !DIFile(filename: "calls.cc", directory: ".")
!2 = !{}
!3 = !{i32 2, !"Dwarf Version", i32 4}
!4 = !{i32 1, !"Debug Info Version", i32 3}
!5 = !{!"clang version 3.5 "}
!6 = distinct !DISubprogram(name: "sum", scope: !1, file: !1, line: 3, type: !7, scopeLine: 3, virtualIndex: 6, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !2)
!7 = !DISubroutineType(types: !2)
!8 = !DILocation(line: 4, scope: !6)
!9 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 7, type: !7, scopeLine: 7, virtualIndex: 6, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !2)
!10 = !DILocation(line: 8, scope: !9)
!11 = !DILocation(line: 9, scope: !9)
!12 = !DILocation(line: 9, scope: !13)
!13 = !DILexicalBlockFile(scope: !9, file: !1, discriminator: 2)
!14 = !DILocation(line: 10, scope: !15)
!15 = distinct !DILexicalBlock(scope: !9, file: !1, line: 10)
!16 = !DILocation(line: 10, scope: !17)
!17 = !DILexicalBlockFile(scope: !15, file: !1, discriminator: 2)
!18 = !DILocation(line: 10, scope: !19)
!19 = !DILexicalBlockFile(scope: !15, file: !1, discriminator: 4)
!20 = !DILocation(line: 10, scope: !21)
!21 = !DILexicalBlockFile(scope: !15, file: !1, discriminator: 6)
!22 = !DILocation(line: 11, scope: !9)
!23 = !DILocation(line: 12, scope: !9)

; Note that hotness of main is unknown.
; NON-FILTER-DAG: Function:        _Z3sumii
; NON-FILTER-DAG: Function:        main

; COLD-PROFILE-EXCLUDE-COLD-NOT: Function:        _Z3sumii
; COLD-PROFILE-EXCLUDE-COLD-DAG: Function:        main

; COLD-PROFILE-HOT-ONLY-NOT: Function:        _Z3sumii
; COLD-PROFILE-HOT-ONLY-NOT: Function:        main

; HOT-PROFILE-EXCLUDE-COLD-DAG: Function:        _Z3sumii
; HOT-PROFILE-EXCLUDE-COLD-DAG: Function:        main

; HOT-PROFILE-HOT-ONLY-NOT: Function:        main
; HOT-PROFILE-HOT-ONLY-DAG: Function:        _Z3sumii
