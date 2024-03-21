; Test loop unrolling using auto-tuning YAML api with IRs generated when ASSERTION=OFF
; The IRs generated when ASSERTION=OFF usually only use slot numbers as variable names.

; RUN: opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' | \
; RUN:     FileCheck %s -check-prefix=DISABLE

; RUN: rm %t.result1_raw %t.unroll1_raw.yaml -rf
; RUN: sed 's#\[number\]#1#g; s#\[hash\]#18159364858606519094#g' \
; RUN:     %S/Inputs/unroll_raw_template.yaml > %t.unroll1_raw.yaml
; RUN: opt %s -S -passes='require<opt-remark-emit>,function(loop-unroll)' \
; RUN:     -auto-tuning-input=%t.unroll1_raw.yaml | FileCheck %s -check-prefix=UNROLL1

; RUN: rm %t.result2_raw %t.unroll2_raw.yaml -rf
; RUN: sed 's#\[number\]#2#g; s#\[hash\]#18159364858606519094#g' \
; RUN:     %S/Inputs/unroll_raw_template.yaml > %t.unroll2_raw.yaml
; RUN: opt %s -S -passes='require<opt-remark-emit>,function(loop-unroll)' \
; RUN:     -auto-tuning-input=%t.unroll2_raw.yaml | FileCheck %s -check-prefix=UNROLL2

; RUN: rm %t.result4_raw %t.unroll4_raw.yaml -rf
; RUN: sed 's#\[number\]#4#g; s#\[hash\]#18159364858606519094#g' \
; RUN:     %S/Inputs/unroll_raw_template.yaml > %t.unroll4_raw.yaml
; RUN: opt %s -S -passes='require<opt-remark-emit>,function(loop-unroll)' \
; RUN:     -auto-tuning-input=%t.unroll4_raw.yaml | FileCheck %s -check-prefix=UNROLL4

; UNSUPPORTED: windows

; ModuleID = 't.ll'
source_filename = "t.ll"

@.str = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1

define void @test(i32*) {
  %2 = alloca i32*, align 8
  store i32* %0, i32** %2, align 8
  %3 = load i32*, i32** %2, align 8
  %4 = load i32, i32* %3, align 4
  %5 = add nsw i32 %4, 2
  %6 = load i32*, i32** %2, align 8
  store i32 %5, i32* %6, align 4
  ret void
}

define i32 @main() {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  store i32 0, i32* %1, align 4
  store i32 8, i32* %2, align 4
  %3 = load i32, i32* %2, align 4
  %4 = icmp sle i32 %3, 88
  br i1 %4, label %.lr.ph, label %13

.lr.ph:                                           ; preds = %0
  br label %5

; <label>:5:                                      ; preds = %.lr.ph, %8
  call void @test(i32* %2)
  %6 = load i32, i32* %2, align 4
  %7 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([4 x i8], [4 x i8]* @.str, i32 0, i32 0), i32 %6)
  br label %8

; <label>:8:                                      ; preds = %5
  %9 = load i32, i32* %2, align 4
  %10 = add nsw i32 %9, 8
  store i32 %10, i32* %2, align 4
  %11 = load i32, i32* %2, align 4
  %12 = icmp sle i32 %11, 88
  br i1 %12, label %5, label %._crit_edge

._crit_edge:                                      ; preds = %8
  br label %13

; <label>:13:                                     ; preds = %._crit_edge, %0
  %14 = load i32, i32* %1, align 4
  ret i32 %14
}

declare i32 @printf(i8*, ...)


; Auto-tuning-enabled loop unrolling - check that the loop is not unrolled when the auto-tuning feature is disabled
;
; DISABLE-LABEL: @main(
; DISABLE: call void @test(ptr %2)
; DISABLE-NOT: call void @test(ptr %2)
; DISABLE-NOT: llvm.loop.unroll.disable


; Auto-tuning-enabled loop unrolling - check that we can unroll the loop by 1
; when explicitly requested.
;
; UNROLL1-LABEL: @main(
; UNROLL1: call void @test(ptr %2)
; UNROLL1-NOT: call void @test(ptr %2)

; Auto-tuning-enabled loop unrolling - check that we can unroll the loop by 2
; when explicitly requested.
;
; UNROLL2-LABEL: @main(
; UNROLL2: call void @test(ptr %2)
; UNROLL2: call void @test(ptr %2)
; UNROLL2-NOT: call void @test(ptr %2)
; UNROLL2: llvm.loop.unroll.disable


; Auto-tuning-enabled loop unrolling - check that we can unroll the loop by 4
; when explicitly requested.
;
; UNROLL4-LABEL: @main(
; UNROLL4: call void @test(ptr %2)
; UNROLL4: call void @test(ptr %2)
; UNROLL4: call void @test(ptr %2)
; UNROLL4: call void @test(ptr %2)
; UNROLL4: llvm.loop.unroll.disable
