; RUN: opt -passes=protean-collect-features --enable-protean-feature-dump --protean-dump-file=%t < %s 
; RUN: cat %t | FileCheck %s

@.str = private unnamed_addr constant [3 x i8] c"%d\00", align 1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @test(i32 %0, i32 %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  store i32 %0, i32* %3, align 4
  store i32 %1, i32* %4, align 4
  store i32 0, i32* %5, align 4
  br label %6

6:                                                ; preds = %15, %2
  %7 = load i32, i32* %5, align 4
  %8 = icmp slt i32 %7, 5
  br i1 %8, label %9, label %18

9:                                                ; preds = %6
  %10 = load i32, i32* %5, align 4
  %11 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([3 x i8], [3 x i8]* @.str, i64 0, i64 0), i32 %10)
  %12 = load i32, i32* %5, align 4
  %13 = load i32, i32* %4, align 4
  %14 = add nsw i32 %13, %12
  store i32 %14, i32* %4, align 4
  br label %15

15:                                               ; preds = %9
  %16 = load i32, i32* %5, align 4
  %17 = add nsw i32 %16, 1
  store i32 %17, i32* %5, align 4
  br label %6, !llvm.loop !2

18:                                               ; preds = %6
  %19 = load i32, i32* %4, align 4
  ret i32 %19
}

declare dso_local i32 @printf(i8*, ...) #1

!2 = distinct !{!2, !3}
!3 = !{!"llvm.loop.mustprogress"}

; CHECK: function_count
; CHECK: total_bb_count
; CHECK: average_bb_per_function
; CHECK: total_instruction_count
; CHECK: total_function_calls
; CHECK: average_calls_per_function
; CHECK: median_calls_per_function
; CHECK: average_instructions_per_function
; CHECK: average_load_instructions_per_function
; CHECK: average_store_instructions_per_function
; CHECK: loop_count
; CHECK: total_edge_count
; CHECK: critical_edge_count
; CHECK: global_variable_count
