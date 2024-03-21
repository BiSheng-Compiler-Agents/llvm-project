// Inlining opportunity.

int mul(int a) { return a * a; }

int add(int a) { return a + a; }

int inc(int a) { return ++a; }

int func(int a) {
  int x = add(a);
  int y = mul(a);
  int z = x + y;

  z += inc(a);

  return z;
}
