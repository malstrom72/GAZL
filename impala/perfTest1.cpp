#include <stdio.h>
unsigned int myrand() { static unsigned int seed = 2463534242U; unsigned int y = seed; y ^= y << 13; y ^= y >> 17; seed = y ^ (y << 5); return seed; }
unsigned int array[20000];
int main(int, char**) { volatile static int result; for (int i = 0; i < 20000; ++i) { array[i] = myrand(); } for (int i = 0; i < 2000; ++i) { int minj = 0; for (int j = 1; j < 20000; ++j) { if (array[j] < array[minj]) minj = j; } result = minj; } printf("%d: %u\n", result, array[result]); return 0; }
