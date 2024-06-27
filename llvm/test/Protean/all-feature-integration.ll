; RUN: opt -passes=protean-collect-features --enable-protean-feature-dump --protean-dump-file=%t --protean-loop-dump-file=%t < %s 
; RUN: cat %t | FileCheck %s
@.str = private unnamed_addr constant [14 x i8] c"loop count %d\00", align 1

; Function Attrs: noinline nounwind optnone uwtable
define i32 @add(i32 %add1, i32 %add2) {
  %result = add i32 %add1, %add2
  ret i32 %result
}

define dso_local i32 @main() #0 {
entry:
  %retval = alloca i32, align 4
  %i = alloca i32, align 4
  store i32 0, ptr %retval, align 4
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.inc, %entry
  %0 = load i32, ptr %i, align 4
  %cmp = icmp slt i32 %0, 5
  br i1 %cmp, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %1 = load i32, ptr %i, align 4
  %call = call i32 (ptr, ...) @printf(ptr noundef @.str, i32 noundef %1)
  br label %for.inc

for.inc:                                          ; preds = %for.body
  %2 = load i32, ptr %i, align 4
  %inc = add nsw i32 %2, 1
  store i32 %inc, ptr %i, align 4
  br label %for.cond, !llvm.loop !1

for.end:                                          ; preds = %for.cond
  %3 = load i32, ptr %retval, align 4
  %final = call i32 @add(i32 %3, i32 1)
  ret i32 %final
}

declare i32 @printf(ptr noundef, ...) #1

!1 = !{!"llvm.loop.mustprogress"}

; CHECK: Module|Function|Callee|Caller|Loop
; CHECK: function_count
; CHECK: callee_Blocks
; CHECK: caller_Blocks
; CHECK: callsite_cost
; CHECK: TripCount
