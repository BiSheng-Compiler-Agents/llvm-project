#include "input.h"
#include "output.h"
#include <stdio.h>
#include <stdlib.h>

void inc_data(int Arr[], int Size) {
  for (int I = 0; I < Size; ++I)
    Arr[I] = Arr[I] + 1;
}

int main(int argc, char **argv) {
  int NI = atoi(argv[1]);
  int Arr[NI];

  input_data(Arr, NI);
  inc_data(Arr, NI);
  output_data(Arr, NI);
  return 0;
}
