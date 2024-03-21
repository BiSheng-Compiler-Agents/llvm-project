#include <stdio.h>

void output_data(int *Arr, int NI) {
  printf("Printing data...\n");
  for (int I = 0; I < NI; ++I)
    printf("%d\t", Arr[I]);
  printf("\nPrinting done...\n");
}
