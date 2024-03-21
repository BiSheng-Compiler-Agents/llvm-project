// Loop unrolling opportunity.

void func3(int *a, int size) {
  for (int i = 0; i < size; i++)
    a[i]++;
}