; RUN: opt -passes=protean-collect-features -S %s | %python %S/Inputs/extract-protean-features.py | FileCheck %s --match-full-lines

; CHECK-DAG: MaxLoopHeight = 2

define void @unequal_sibling_depths(i2 %choice) {
entry:
  br label %outer

outer:
  br label %deep

deep:
  br label %inner

inner:
  switch i2 %choice, label %inner [
    i2 0, label %deep
    i2 1, label %shallow
  ]

shallow:
  switch i2 %choice, label %outer [
    i2 0, label %shallow
  ]
}
