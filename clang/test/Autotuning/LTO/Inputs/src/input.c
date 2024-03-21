#include <stdio.h>

void input_data(int Arr[], int NI) {
  printf("Input data...\n");
  for (int I = 0; I < NI; ++I)
    scanf("%d", &Arr[I]);
}
