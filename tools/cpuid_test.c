#include <stdio.h>
#include <intrin.h>

int main(void) {
  int info[4];
  __cpuid(info, 0);
  char vendor[13];
  memcpy(vendor, &info[1], 4);
  memcpy(vendor + 4, &info[3], 4);
  memcpy(vendor + 8, &info[2], 4);
  vendor[12] = 0;
  __cpuid(info, 1);
  int sse42 = (info[2] >> 20) & 1;
  int osxsave = (info[2] >> 27) & 1;
  int avx = (info[2] >> 28) & 1;
  unsigned long long xcr0 = _xgetbv(0);
  __cpuidex(info, 7, 0);
  int avx2 = (info[1] >> 5) & 1;
  printf("vendor=%s\n", vendor);
  printf("sse4.2=%d osxsave=%d avx=%d avx2=%d xcr0=0x%llx\n", sse42, osxsave, avx, avx2, xcr0);
  return 0;
}
