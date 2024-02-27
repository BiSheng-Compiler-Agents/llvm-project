; REQUIRES: asserts
; RUN: opt -debug-pass-manager -passes='no-op-module,no-op-cgscc,no-op-module,no-op-function,no-op-loop,no-op-function,no-op-loop' -o %t1.out < %s 2>%t.txt
; RUN: opt -debug-pass-manager -passes='cgscc(no-op-cgscc),no-op-module,no-op-function,no-op-loop,no-op-function,no-op-loop' -o %t1.out < %s 2>%t1.txt
; RUN: not llvm-diff %t.txt %t1.txt 2>&1 | FileCheck %s --check-prefix CGSCC

; RUN: opt -debug-pass-manager -passes='no-op-module,no-op-function,no-op-module,no-op-cgscc,no-op-loop,no-op-function,no-op-loop' -o %t1.out < %s 2>%t2.txt
; RUN: opt -debug-pass-manager -passes='function(no-op-function),no-op-module,no-op-cgscc,no-op-loop,no-op-function,no-op-loop' -o %t1.out < %s 2>%t3.txt
; RUN: not llvm-diff %t2.txt %t3.txt 2>&1 | FileCheck %s --check-prefix FUNCTION

; RUN: opt -debug-pass-manager -passes='no-op-module,no-op-loop,no-op-module,no-op-cgscc,no-op-loop,no-op-function,no-op-loop' -o %t1.out < %s 2>%t2.txt
; RUN: opt -debug-pass-manager -passes='function(loop(no-op-loop)),no-op-module,no-op-cgscc,no-op-loop,no-op-function,no-op-loop' -o %t1.out < %s 2>%t3.txt
; RUN: not llvm-diff %t2.txt %t3.txt 2>&1 | FileCheck %s --check-prefix LOOP

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

; CHECK: Running pass: NoOpModulePass on [module]
; CGSCC-NOT: NoOpCGSCCPass
; CGSCC-NOT: NoOpLoopPass
; CGSCC-NOT: NoOpFunctionPass

; FUNCTION-NOT: NoOpCGSCCPass
; FUNCTION-NOT: NoOpFunctionPass
; FUNCTION-NOT: NoOpLoopPass

; LOOP-NOT: NoOpCGSCCPass
; LOOP-NOT: NoOpFunctionPass
; LOOP-NOT: NoOpLoopPass
