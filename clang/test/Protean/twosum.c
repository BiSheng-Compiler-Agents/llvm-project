// RUN: env BISHENG_ACPO_DIR=%S/../../../acpo %clang -OP -mllvm -protean -Wprotean,-protean-output-table=true %s 2>&1 | FileCheck %s

#include <stdlib.h>

int *twoSum(int *nums, int numsSize, int target, int *returnSize) {
  *returnSize = 2;
  int *result = (int *)malloc(*returnSize * sizeof(int));
  for (int i = 0; i < numsSize; i++) {
    for (int j = i + 1; j < numsSize; j++) {
      if (nums[i] + nums[j] == target) {
        result[0] = i;
        result[1] = j;
        return result;
      }
    }
  }
  *returnSize = 0;
  return NULL;
}

int arr[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
int targ = 21;

int main() {
  int returnSize;
  int *rez = twoSum(arr, 11, targ, &returnSize);
  free(rez);
}

// CHECK: Iteration{{[[:space:]]*}}Current State{{[[:space:]]*}}Next State{{[[:space:]]*}}Best State{{[[:space:]]*}}Current Cost{{[[:space:]]*}}Next Cost{{[[:space:]]*}}Best Cost{{[[:space:]]*}}Accepted?{{[[:space:]]*}}Temperature
