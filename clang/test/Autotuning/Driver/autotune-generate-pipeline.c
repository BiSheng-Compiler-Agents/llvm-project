// Verify if -fautotune-generate can be invoked properly

// RUN: %clang -fautotune-generate -O1 --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | FileCheck \
// RUN: %s --check-prefix=GENERATE-DEFAULT

// RUN: AUTOTUNE_DATADIR=/tmp/test_autotune_datadir/ %clang -fautotune-generate -O1 --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | FileCheck \
// RUN: %s --check-prefix=GENERATE-ENV-VAR

// RUN: %clang -fautotune-generate -O1 -flto -fuse-ld=lld --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | FileCheck \
// RUN: %s --check-prefix=GENERATE-LTO-DEFAULT

// RUN: AUTOTUNE_DATADIR=/tmp/test_autotune_datadir/ %clang -fautotune-generate -O1 -flto -fuse-ld=lld --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | FileCheck \
// RUN: %s --check-prefix=GENERATE-LTO-ENV-VAR

// RUN: %clang -fautotune-generate -O0 --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | FileCheck \
// RUN: %s --check-prefix=ERROR-O0

// RUN: %clang -fautotune-generate -O1 -flto --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | FileCheck \
// RUN: %s --check-prefix=ERROR-LTO-WITHOUT-LLD

// RUN: export AUTOTUNE_MODE=-fautotune-generate
// RUN: %clang -O1 --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | \
// RUN:     FileCheck %s --check-prefix=GENERATE-DEFAULT

// RUN: %clang -fautotune-generate -O1 -c -flto %s -### \
// RUN:     --sysroot %S/Inputs/basic_cross_linux_tree 2>&1 | \
// RUN:     FileCheck %s --check-prefix=LTO-COMPILE-ONLY

// RUN: %clang -fautotune-generate -O1 -c -flto %s -v -o %t.tmp.o 2>&1 | \
// RUN:     FileCheck %s --check-prefix=LTO-COMPILE-ONLY

// RUN: not %clang -fautotune-generate -O1 -flto -v %t.tmp.o 2>&1 | \
// RUN:     FileCheck %s --check-prefix=ERROR-LTO-WITHOUT-LLD

// RUN: %clang -fautotune-generate -O1 -flto -v %t.tmp.o -fuse-ld=lld 2>&1 | \
// RUN:     FileCheck %s --check-prefix=LTO-LINK-ONLY

// RUN: export AUTOTUNE_MODE=-fautotune-generate
// RUN: AUTOTUNE_DATADIR=/tmp/test_autotune_datadir/ %clang -O1 --sysroot \
// RUN:     %S/Inputs/basic_cross_linux_tree -### %s 2>&1 | \
// RUN:     FileCheck %s --check-prefix=GENERATE-ENV-VAR

// RUN: export AUTOTUNE_MODE=-fautotune-generate
// RUN: %clang -O1 -flto -fuse-ld=lld --sysroot %S/Inputs/basic_cross_linux_tree -### %s 2>&1 | \
// RUN:     FileCheck %s --check-prefix=GENERATE-LTO-DEFAULT

// RUN: export AUTOTUNE_MODE=-fautotune-generate
// RUN: AUTOTUNE_DATADIR=/tmp/test_autotune_datadir/ %clang  -O1 -flto -fuse-ld=lld \
// RUN:     --sysroot %S/Inputs/basic_cross_linux_tree -### %s 2>&1 | \
// RUN:     FileCheck %s --check-prefix=GENERATE-LTO-ENV-VAR

// RUN: export AUTOTUNE_MODE=-fautotune-generate
// RUN: %clang -O0 --sysroot %S/Inputs/basic_cross_linux_tree -### %s 2>&1 | \
// RUN:     FileCheck %s --check-prefix=ERROR-O0

// RUN: export AUTOTUNE_MODE=-fautotune-generate
// RUN: %clang -O1 -flto --sysroot %S/Inputs/basic_cross_linux_tree -### %s 2>&1 | \
// RUN:     FileCheck %s --check-prefix=ERROR-LTO-WITHOUT-LLD

// RUN: %clang -fautotune-generate -c -O1 -flto=thin -fuse-ld=lld --sysroot \
// RUN:     %S/Inputs/basic_cross_linux_tree -### %s 2>&1 | \
// RUN:     FileCheck %s --check-prefix=GENERATE-THIN-SUCCESS1

// RUN: %clang -fautotune-generate=LLVMParam -O1 -flto=thin -fuse-ld=lld \
// RUN:     --sysroot %S/Inputs/basic_cross_linux_tree -### %s 2>&1 | \
// RUN:     FileCheck %s --check-prefix=GENERATE-THIN-SUCCESS2

// RUN: %clang -fautotune-generate -O1 -flto=thin -fuse-ld=lld --sysroot \
// RUN:     %S/Inputs/basic_cross_linux_tree -### %s 2>&1 | \
// RUN:     FileCheck %s --check-prefixes=GENERATE-THIN-FAIL1

// RUN: %clang -fautotune-generate=Loop -O1 -flto=thin -fuse-ld=lld --sysroot \
// RUN:     %S/Inputs/basic_cross_linux_tree -### %s 2>&1 | \
// RUN:     FileCheck %s --check-prefixes=GENERATE-THIN-FAIL2

int main() { return 0; }

// GENERATE-DEFAULT: {{clang.* "-cc1"}}
// GENERATE-DEFAULT-SAME: -debug-info-kind=line-tables-only
// GENERATE-DEFAULT-SAME: -auto-tuning-opp=autotune_datadir/opp
// GENERATE-DEFAULT-SAME: -auto-tuning-type-filter=CallSite,Function,Loop
// GENERATE-DEFAULT: ld
// GENERATE-DEFAULT-NOT: -auto-tuning-opp=autotune_datadir/opp
// GENERATE-DEFAULT-NOT: -auto-tuning-type-filter=CallSite,Function,Loop

// GENERATE-ENV-VAR: {{clang.* "-cc1"}}
// GENERATE-ENV-VAR-SAME: -auto-tuning-opp=/tmp/test_autotune_datadir/opp
// GENERATE-ENV-VAR-SAME: -auto-tuning-type-filter=CallSite,Function,Loop

// GENERATE-LTO-DEFAULT: {{clang.* "-cc1"}}
// GENERATE-LTO-DEFAULT-SAME: -debug-info-kind=line-tables-only
// GENERATE-LTO-DEFAULT: -auto-tuning-opp=autotune_datadir/opp
// GENERATE-LTO-DEFAULT: -auto-tuning-type-filter=CallSite,Function,Loop
// GENERATE-LTO-DEFAULT: ld.lld
// GENERATE-LTO-DEFAULT-SAME: -auto-tuning-opp=autotune_datadir/opp
// GENERATE-LTO-DEFAULT-SAME: -auto-tuning-type-filter=CallSite,Function,Loop

// GENERATE-LTO-ENV-VAR: {{clang.* "-cc1"}}
// GENERATE-LTO-ENV-VAR: -auto-tuning-opp=/tmp/test_autotune_datadir/opp
// GENERATE-LTO-ENV-VAR: -auto-tuning-type-filter=CallSite,Function,Loop
// GENERATE-LTO-ENV-VAR: ld.lld
// GENERATE-LTO-ENV-VAR-SAME: -auto-tuning-opp=/tmp/test_autotune_datadir/opp
// GENERATE-LTO-ENV-VAR-SAME: -auto-tuning-type-filter=CallSite,Function,Loop

// ERROR-O0: error: -fautotune/-fautotune-generate should not be enabled at -O0

// ERROR-LTO-WITHOUT-LLD: error: LTO requires -fuse-ld=lld

// GENERATE-THIN-SUCCESS1-NOT: error
// GENERATE-THIN-SUCCESS1: {{clang.* "-cc1"}}
// GENERATE-THIN-SUCCESS1-SAME: -auto-tuning-type-filter=CallSite,Function,Loop
// GENERATE-THIN-SUCCESS1-NOT: "{{.*}}ld.lld"

// GENERATE-THIN-SUCCESS2-NOT: error:
// GENERATE-THIN-SUCCESS2: {{clang.* "-cc1"}}
// GENERATE-THIN-SUCCESS2-SAME: -auto-tuning-type-filter=LLVMParam
// GENERATE-THIN-SUCCESS2: "{{.*}}ld.lld"
// GENERATE-THIN-SUCCESS2-SAME: -auto-tuning-type-filter=LLVMParam

// GENERATE-THIN-FAIL1: error: AutoTuner: no valid code region type specified
// GENERATE-THIN-FAIL1-SAME: for ThinLTO mode
// GENERATE-THIN-FAIL1: {{clang.* "-cc1"}}
// GENERATE-THIN-FAIL1-SAME: -auto-tuning-type-filter=CallSite,Function,Loop
// GENERATE-THIN-FAIL1: "{{.*}}ld.lld"
// GENERATE-THIN-FAIL1-SAME: -auto-tuning-type-filter=CallSite,Function,Loop

// GENERATE-THIN-FAIL2: error: fine-grained autotuning not supported in ThinLTO
// GENERATE-THIN-FAIL2-SAME: mode
// GENERATE-THIN-FAIL2-NEXT: error: unsupported argument 'Loop' to option
// GENERATE-THIN-FAIL2-SAME: 'fautotune-generate='
// GENERATE-THIN-FAIL2: {{clang.* "-cc1"}}
// GENERATE-THIN-FAIL2-SAME: -auto-tuning-type-filter=Loop
// GENERATE-THIN-FAIL2: "{{.*}}ld.lld"
// GENERATE-THIN-FAIL2-SAME: -auto-tuning-type-filter=

// LTO-COMPILE-ONLY: {{clang.*-cc1}}
// LTO-COMPILE-ONLY-SAME: -debug-info-kind=line-tables-only
// LTO-COMPILE-ONLY-SAME: -auto-tuning-opp=autotune_datadir/opp
// LTO-COMPILE-ONLY-SAME: -auto-tuning-type-filter=CallSite,Function,Loop
// LTO-COMPILE-ONLY-NOT: ld.lld
// LTO-COMPILE-ONLY-NOT: error: LTO requires -fuse-ld=lld

// LTO-LINK-ONLY-NOT: {{clang.*-cc1}}
// LTO-LINK-ONLY: ld.lld
// LTO-LINK-ONLY-SAME: -auto-tuning-opp=autotune_datadir/opp
// LTO-LINK-ONLY-SAME: -auto-tuning-type-filter=CallSite,Function,Loop
