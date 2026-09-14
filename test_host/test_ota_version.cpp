// Host-side test (g++), mimo PlatformIO build - overuje OtaVersion.{h,cpp}
// z src/ bez nutnosti ESP32 toolchainu. Spustit:
//   g++ -std=c++17 -Wall -Wextra -o test_ota_version.exe test_host/test_ota_version.cpp src/OtaVersion.cpp
//   ./test_ota_version.exe
#include "../src/OtaVersion.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void expectParse(const char* str, bool shouldParse, uint32_t maj = 0, uint32_t min = 0, uint32_t pat = 0) {
  OtaVersion v;
  bool ok = parseOtaVersion(str, v);
  if (ok != shouldParse) {
    printf("[FAIL] parse(\"%s\") ok=%d, expected ok=%d\n", str, ok, shouldParse);
    failures++;
    return;
  }
  if (ok && (v.major != maj || v.minor != min || v.patch != pat)) {
    printf("[FAIL] parse(\"%s\") = %u.%u.%u, expected %u.%u.%u\n", str, v.major, v.minor, v.patch, maj, min, pat);
    failures++;
    return;
  }
  printf("[OK] parse(\"%s\")%s\n", str, ok ? "" : " (correctly rejected)");
}

static void expectCompare(const char* a, const char* b, int expectedSign) {
  bool ok = false;
  int cmp = compareOtaVersionStrings(a, b, &ok);
  int sign = (cmp > 0) - (cmp < 0);
  bool pass = ok && (sign == expectedSign);
  printf("[%s] compare(\"%s\", \"%s\") sign=%d, expected=%d (ok=%d)\n",
         pass ? "OK" : "FAIL", a, b, sign, expectedSign, ok);
  if (!pass) failures++;
}

int main() {
  expectParse("1.0.0", true, 1, 0, 0);
  expectParse("v1.0.0", true, 1, 0, 0);
  expectParse("V2.3.4", true, 2, 3, 4);
  expectParse("1.10.0", true, 1, 10, 0);
  expectParse("1.2", true, 1, 2, 0);
  expectParse("1", true, 1, 0, 0);
  expectParse("v1.2.3-beta", true, 1, 2, 3);
  expectParse("v1.2.3+build5", true, 1, 2, 3);
  expectParse("", false);
  expectParse("v", false);
  expectParse("abc", false);
  expectParse("vX.Y.Z", false);

  // Klicovy pripad: numericke, ne lexikograficke porovnani.
  expectCompare("1.10.0", "1.9.0", 1);
  expectCompare("1.9.0", "1.10.0", -1);
  expectCompare("1.0.0", "1.0.0", 0);
  expectCompare("v1.0.0", "1.0.0", 0);
  expectCompare("2.0.0", "1.99.99", 1);
  expectCompare("1.0.1", "1.0.0", 1);
  expectCompare("1.2.0", "1.10.0", -1);

  if (failures == 0) {
    printf("\nVSECHNY TESTY PROSLY\n");
    return 0;
  } else {
    printf("\n%d TEST(U) SELHALO\n", failures);
    return 1;
  }
}
