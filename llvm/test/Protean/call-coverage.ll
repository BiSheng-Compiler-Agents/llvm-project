; RUN: rm -rf %t
; RUN: split-file %s %t
; RUN: opt -passes=protean-collect-features -enable-protean-feature-dump \
; RUN:   -protean-dump-file=%t/mixed.csv -S %t/mixed.ll -o %t/mixed.out.ll
; RUN: FileCheck %s --check-prefix=CSV < %t/mixed.csv
; RUN: head -n 1 %t/mixed.csv | diff %S/Inputs/header-order.txt -
; RUN: %python %S/Inputs/extract-protean-features.py %t/mixed.out.ll | FileCheck %s --check-prefix=MIXED --match-full-lines
; RUN: opt -passes=protean-collect-features -S %t/invoke.ll | \
; RUN:   %python %S/Inputs/extract-protean-features.py | FileCheck %s --check-prefixes=SINGLE,INVOKE --match-full-lines
; RUN: opt -passes=protean-collect-features -S %t/external.ll | \
; RUN:   %python %S/Inputs/extract-protean-features.py | FileCheck %s --check-prefixes=SINGLE,EXTERNAL --match-full-lines
; RUN: opt -passes=protean-collect-features -S %t/alias.ll | \
; RUN:   %python %S/Inputs/extract-protean-features.py | FileCheck %s --check-prefixes=SINGLE,ALIAS --match-full-lines

; An unresolved first call must not disrupt subsequent external/defined rows.
; Intrinsics and inline assembly must not produce call rows.
; CSV: Module|Function|Callee|Caller|Loop,
; CSV-NEXT: [[MODULE:[^,|]+]]||||,
; CSV-NEXT: [[MODULE]]|caller||caller|,
; CSV-NEXT: [[MODULE]]|caller|external|caller|,
; CSV-NEXT: [[MODULE]]|driver|caller|driver|,
; CSV-NEXT: [[MODULE]]|caller|||loop,
; CSV-NEXT: [[MODULE]]|driver|||loop,{{.*}}
; CSV-NOT: {{.}}

; There are two calls in caller and one in driver. Metadata sums caller's
; features twice (once per call row), and includes it once as driver's callee.
; Multiple pointer arguments still count as one callsite with pointer arguments.
; MIXED-DAG: edge_count = 1
; MIXED-DAG: total_function_calls = 3
; MIXED-DAG: average_calls_per_function = 1.5
; MIXED-DAG: median_calls_per_function = 1.5
; MIXED-DAG: caller_basic_block_count = 9
; MIXED-DAG: caller_conditionally_executed_blocks = 6
; MIXED-DAG: caller_users = 5
; MIXED-DAG: caller_InitialSize = 29
; MIXED-DAG: caller_Blocks = 9
; MIXED-DAG: callee_Calls = 2
; MIXED-DAG: caller_Calls = 5
; MIXED-DAG: caller_Loops = 3
; MIXED-DAG: caller_MaxLoopDepth = 3
; MIXED-DAG: caller_MaxDomTreeLevel = 6
; MIXED-DAG: caller_PtrArgs = 6
; MIXED-DAG: callee_PtrCallee = 2
; MIXED-DAG: caller_PtrCallee = 5
; MIXED-DAG: callee_CallReturnPtr = 2
; MIXED-DAG: caller_CallReturnPtr = 4
; MIXED-DAG: caller_ConditionalBranch = 3
; MIXED-DAG: caller_CallerHeight = 1
; MIXED-DAG: caller_CallUsage = 2
; MIXED-DAG: callee_NumCallsiteInLoop = 2
; MIXED-DAG: caller_NumCallsiteInLoop = 5
; MIXED-DAG: callee_NumOfCallUsesInLoop = 1
; MIXED-DAG: caller_NumOfCallUsesInLoop = 2
; MIXED-DAG: caller_SuccessorPerBlock = 3
; MIXED-DAG: caller_AvgNestedLoopLevel = 3
; MIXED-DAG: caller_InstrPerLoop = 12
; MIXED-DAG: caller_BlockWithMultipleSuccessorsPerLoop = 3
; MIXED-DAG: nr_ctant_params = 1
; MIXED-DAG: is_indirect = 1
; MIXED-DAG: loop_level = 3
; MIXED-DAG: is_in_inner_loop = 3
; MIXED-DAG: caller_scc_count = 9
; MIXED-DAG: caller_average_component_size = 3

; Calls without a directly available body still contribute callsite features.
; SINGLE-DAG: total_function_calls = 1
; SINGLE-DAG: average_calls_per_function = 1
; SINGLE-DAG: median_calls_per_function = 1
; SINGLE-DAG: caller_Calls = 1
; INVOKE-DAG: is_indirect = 1
; EXTERNAL-DAG: nr_ctant_params = 1
; EXTERNAL-DAG: is_indirect = 0
; ALIAS-DAG: is_indirect = 0

;--- mixed.ll
declare ptr @external(ptr)
declare ptr @llvm.launder.invariant.group.p0(ptr)

define void @caller(ptr %fp, ptr %p, i1 %again) {
entry:
  br label %loop
loop:
  %indirect = call ptr %fp(ptr %p, ptr %p, i32 7)
  %external = call ptr @external(ptr %p)
  %intrinsic = call ptr @llvm.launder.invariant.group.p0(ptr %p)
  %asm = call ptr asm sideeffect "", "=r,r"(ptr %p)
  br i1 %again, label %loop, label %exit
exit:
  ret void
}

define void @driver(ptr %fp, ptr %p, i1 %again) {
entry:
  br label %loop
loop:
  call void @caller(ptr %fp, ptr %p, i1 %again)
  br i1 %again, label %loop, label %exit
exit:
  ret void
}

;--- invoke.ll
declare i32 @__gxx_personality_v0(...)

define void @caller(ptr %fp) personality ptr @__gxx_personality_v0 {
entry:
  invoke void %fp() to label %normal unwind label %exception
normal:
  ret void
exception:
  %lp = landingpad { ptr, i32 } cleanup
  resume { ptr, i32 } %lp
}

;--- external.ll
declare void @external(i32)

define void @caller() {
  call void @external(i32 7)
  ret void
}

;--- alias.ll
@alias = alias void (), ptr @caller

; getCalledFunction() is null for an alias, but isIndirectCall() is false.
define void @caller() {
  call void @alias()
  ret void
}
