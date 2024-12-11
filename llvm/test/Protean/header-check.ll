; RUN: rm -f -- %t1 && opt -passes='default<O3>' --passes='protean-collect-features' --enable-protean-feature-dump  --protean-dump-file=%t1 --debug-only=proteanFC < %s 2> %t2
; RUN: sed '1d' %t1 > %t3
; RUN: cat %t3 %t2 | FileCheck %s

define i32 @callee(i32 %x) {
entry:
    ret i32 %x
}

define i32 @main() {
entry:
    %i = alloca i32, align 4
    store i32 0, ptr %i, align 4
    br label %for.cond

for.cond:
    %0 = load i32, ptr %i, align 4
    %cmp = icmp slt i32 %0, 5
    br i1 %cmp, label %for.body, label %for.end

for.body:
    call i32 @callee(i32 %0)
    br label %for.inc

for.inc:
    %next_i = add i32 %0, 1
    store i32 %next_i, ptr %i, align 4
    br label %for.cond, !llvm.loop !1

for.end:
    ret i32 0
}

!1 = !{!"llvm.loop.mustprogress"}

; CHECK: [[HEADER:.*]]||||
; CHECK-NEXT: [[HEADER]]|main|callee|main|
; CHECK-NEXT: [[HEADER]]|main|||for.cond
; CHECK: Protean Feature node_count: 2
; CHECK: Protean Feature average_calls_per_function: 1.500000
; CHECK: Protean Feature InstructionPerBlock: 1.000000
; CHECK: Protean Feature scc_size: 1
; CHECK: Protean Feature IsInnerMostLoop: 1
; CHECK: Protean Feature IsOuterMostLoop: 1
