// Verify if -fautotune can be invoked properly

// RUN: %clang -fautotune -O1 --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | FileCheck \
// RUN: %s --check-prefix=AUTOTUNE-DEFAULT

// RUN: %clang -fautotune=0 -O1 --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | FileCheck \
// RUN: %s --check-prefix=AUTOTUNE-DEFAULT-ID0

// RUN: AUTOTUNE_DATADIR=/tmp/test_autotune_datadir/ %clang -fautotune=1 -O1 --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | FileCheck \
// RUN: %s --check-prefix=AUTOTUNE-ENV-VAR-ID1

// RUN: %clang -fautotune -O1 -flto -fuse-ld=lld --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | FileCheck \
// RUN: %s --check-prefix=AUTOTUNE-LTO-DEFAULT

// RUN: AUTOTUNE_DATADIR=/tmp/test_autotune_datadir/ %clang -fautotune -O1 -flto -fuse-ld=lld --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | FileCheck \
// RUN: %s --check-prefix=AUTOTUNE-LTO-ENV-VAR

// RUN: %clang -fautotune -O0 --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | FileCheck \
// RUN: %s --check-prefix=ERROR-O0

// RUN: %clang -fautotune -O1 -flto --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | FileCheck \
// RUN: %s --check-prefix=ERROR-LTO-WITHOUT-LLD

// RUN: %clang -fautotune=test -O1 --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | FileCheck \
// RUN: %s --check-prefix=ERROR-NON-INTEGER-ID
// RUN: %clang -fautotune -O1 -c -flto %s -### \
// RUN:     --sysroot %S/Inputs/basic_cross_linux_tree 2>&1 | \
// RUN:     FileCheck %s --check-prefix=LTO-COMPILE-ONLY

// RUN: mkdir -p %T.tmp/Output
// RUN: cp %S/Inputs/config.yaml %T.tmp/Output
// RUN: env AUTOTUNE_DATADIR=%T.tmp/Output \
// RUN:     %clang -fautotune -O1 -c -flto %s -v -o %t.tmp.o 2>&1 | \
// RUN:     FileCheck %s --check-prefix=LTO-COMPILE-ONLY

// RUN: env AUTOTUNE_DATADIR=%T.tmp/Output \
// RUN:     not %clang -fautotune -O1 -flto -v %t.tmp.o 2>&1 | \
// RUN:     FileCheck %s --check-prefix=ERROR-LTO-WITHOUT-LLD

// RUN: env AUTOTUNE_DATADIR=%T.tmp/Output \
// RUN:     %clang -fautotune -O1 -flto -v %t.tmp.o -fuse-ld=lld 2>&1 | \
// RUN:     FileCheck %s --check-prefix=LTO-LINK-ONLY

// RUN: export AUTOTUNE_MODE=-fautotune
// RUN: %clang -O1 --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | \
// RUN:     FileCheck %s --check-prefix=AUTOTUNE-DEFAULT

// RUN: export AUTOTUNE_MODE=-fautotune=0
// RUN: %clang -O1 --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | \
// RUN:     FileCheck %s --check-prefix=AUTOTUNE-DEFAULT-ID0

// RUN: export AUTOTUNE_MODE=-fautotune=1
// RUN: AUTOTUNE_DATADIR=/tmp/test_autotune_datadir/ %clang -O1 --sysroot \
// RUN:     %S/Inputs/basic_cross_linux_tree -### %s 2>&1 | \
// RUN:     FileCheck %s --check-prefix=AUTOTUNE-ENV-VAR-ID1

// RUN: export AUTOTUNE_MODE=-fautotune
// RUN: %clang -O1 -flto -fuse-ld=lld --sysroot %S/Inputs/basic_cross_linux_tree -### %s 2>&1 | \
// RUN:     FileCheck %s --check-prefix=AUTOTUNE-LTO-DEFAULT

// RUN: export AUTOTUNE_MODE=-fautotune
// RUN: AUTOTUNE_DATADIR=/tmp/test_autotune_datadir/ %clang -O1 -flto -fuse-ld=lld \
// RUN:     --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | \
// RUN:     FileCheck %s --check-prefix=AUTOTUNE-LTO-ENV-VAR

// RUN: export AUTOTUNE_MODE=-fautotune
// RUN: %clang -O0 --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | \
// RUN:     FileCheck %s --check-prefix=ERROR-O0

// RUN: export AUTOTUNE_MODE=-fautotune
// RUN: %clang -O1 -flto --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | \
// RUN:     FileCheck %s --check-prefix=ERROR-LTO-WITHOUT-LLD

// RUN: export AUTOTUNE_MODE=-fautotune=test
// RUN: %clang -O1 --sysroot %S/Inputs/basic_cross_linux_tree -### %s   2>&1 | \
// RUN:     FileCheck %s --check-prefix=ERROR-NON-INTEGER-ID

// RUN: env AUTOTUNE_MODE=-fautotune \
// RUN: %clang -O1 --sysroot %S/Inputs/basic_cross_linux_tree -### %s \
// RUN:     -flto=thin -fuse-ld=lld 2>&1 | \
// RUN:     FileCheck %s --check-prefix=AUTOTUNE-THIN-DEFAULT

int main() { return 0; }

// AUTOTUNE-DEFAULT: {{clang.* "-cc1"}}
// AUTOTUNE-DEFAULT-SAME: -debug-info-kind=line-tables-only
// AUTOTUNE-DEFAULT-SAME: -auto-tuning-input=autotune_datadir/config.yaml
// AUTOTUNE-DEFAULT: ld
// AUTOTUNE-DEFAULT-NOT: -auto-tuning-opp=autotune_datadir/opp
// AUTOTUNE-DEFAULT-NOT: -auto-tuning-type-filter=CallSite,Function,Loop

// AUTOTUNE-ENV-VAR: {{clang.* "-cc1"}}
// AUTOTUNE-ENV-VAR-SAME: -auto-tuning-input=/tmp/test_autotune_datadir/config.yaml

// AUTOTUNE-DEFAULT-ID0: {{clang.* "-cc1"}}
// AUTOTUNE-DEFAULT-ID0-SAME: -auto-tuning-input=autotune_datadir/config-0.yaml

// AUTOTUNE-ENV-VAR-ID1: {{clang.* "-cc1"}}
// AUTOTUNE-ENV-VAR-ID1-SAME: -auto-tuning-input=/tmp/test_autotune_datadir/config-1.yaml

// AUTOTUNE-LTO-DEFAULT: {{clang.* "-cc1"}}
// AUTOTUNE-LTO-DEFAULT-SAME: -debug-info-kind=line-tables-only
// AUTOTUNE-LTO-DEFAULT-SAME: -auto-tuning-input=autotune_datadir/config.yaml
// AUTOTUNE-LTO-DEFAULT: ld.lld
// AUTOTUNE-LTO-DEFAULT-SAME: -auto-tuning-input=autotune_datadir/config.yaml

// AUTOTUNE-LTO-ENV-VAR: {{clang.* "-cc1"}}
// AUTOTUNE-LTO-ENV-VAR-SAME: -auto-tuning-input=/tmp/test_autotune_datadir/config.yaml
// AUTOTUNE-LTO-ENV-VAR: ld.lld
// AUTOTUNE-LTO-ENV-VAR-SAME: -auto-tuning-input=/tmp/test_autotune_datadir/config.yaml

// ERROR-O0: error: -fautotune/-fautotune-generate should not be enabled at -O0
// ERROR-LTO-WITHOUT-LLD: error: LTO requires -fuse-ld=lld
// ERROR-NON-INTEGER-ID: error: invalid integral value 'test' in '-fautotune=test'

// AUTOTUNE-THIN-DEFAULT: {{clang.* "-cc1"}}
// AUTOTUNE-THIN-DEFAULT-SAME: -debug-info-kind=line-tables-only
// AUTOTUNE-THIN-DEFAULT-SAME: -auto-tuning-input=autotune_datadir/config.yaml
// AUTOTUNE-THIN-DEFAULT: "{{.*}}ld.lld"
// AUTOTUNE-THIN-DEFAULT-SAME: -auto-tuning-input=autotune_datadir/config.yaml
// AUTOTUNE-THIN-DEFAULT-SAME: -autotuning-thin-lto=true

// LTO-COMPILE-ONLY: {{clang.*-cc1}}
// LTO-COMPILE-ONLY-SAME: -debug-info-kind=line-tables-only
// LTO-COMPILE-ONLY-SAME: -auto-tuning-input={{.*}}/config.yaml
// LTO-COMPILE-ONLY-NOT: ld.lld
// LTO-COMPILE-ONLY-NOT: error: LTO requires -fuse-ld=lld

// LTO-LINK-ONLY-NOT: {{clang.*-cc1}}
// LTO-LINK-ONLY: ld.lld
// LTO-LINK-ONLY-SAME: -auto-tuning-input={{.*}}/config.yaml
