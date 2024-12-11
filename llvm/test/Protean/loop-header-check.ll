; RUN: rm -f -- %t1 && opt -passes='default<O3>' --passes='protean-collect-features' --enable-protean-feature-dump  --protean-dump-file=%t1 --debug-only=proteanFC < %s 2> %t2
; RUN cat %t1 %t2 | FileCheck %s

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

; CHECK: [[HEADER:.*]],main,for.cond
; CHECK: Protean Feature Size: 9
; CHECK: Protean Feature IsOuterMostLoop: 1
