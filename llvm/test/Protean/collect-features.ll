; RUN: opt -passes=protean-collect-features --enable-protean-feature-dump --protean-dump-file=%t < %s
; RUN: cat %t | FileCheck %s
; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @sha_init() #0 {
  ret void
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @sha_stream() #0 {
  call void @sha_init()
  ret void
}

; CHECK: global_variable_count
; CHECK: critical_edge_count
; CHECK: total_edge_count
; CHECK: loop_count
