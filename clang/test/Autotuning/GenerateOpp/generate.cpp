// RUN: rm -rf %t.other
// RUN: export AUTOTUNE_DATADIR=%t.other

// Test coarse-grain code region generation process and 'Name' field have
// complete path.
// RUN: %clang %s -S -O3 -fautotune-generate=Other -o -
// RUN: grep "Name: \+'%S/generate.cpp'" %t.other/opp/generate.cpp.yaml
// RUN: not grep "Name: \+generate.cpp" %t.other/opp/generate.cpp.yaml

// Use environment variable 'AUTOTUNE_PROJECT_DIR' to truncate the complete
// prefix and only use filename as 'Name' field for code region.
// RUN: rm -rf %t.other
// RUN: export AUTOTUNE_PROJECT_DIR=%S/
// RUN: %clang %s -S -O3 -fautotune-generate=Other -o -
// RUN: not grep "Name: \+'%S/generate.cpp'" %t.other/opp/generate.cpp.yaml
// RUN: grep "Name: \+generate.cpp" %t.other/opp/generate.cpp.yaml

// A simple cpp file.
int main() {
  int i = 8;
  for (; i < 20;) {
    int a = i - 5;
    i = i + 2;
  }
}
