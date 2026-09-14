// Host-side test (g++), mimo PlatformIO build - overuje Sha256.{h,cpp} z src/
// proti znamym testovacim vektorum FIPS 180-4 + streamovani po castech.
// Spustit:
//   g++ -std=c++17 -Wall -Wextra -o test_sha256.exe test_host/test_sha256.cpp src/Sha256.cpp
//   ./test_sha256.exe
#include "../src/Sha256.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(const char* name, const char* expectedHex, const uint8_t digest[32]) {
  char hex[65];
  Sha256::toHex(digest, hex);
  bool ok = strcmp(hex, expectedHex) == 0;
  printf("[%s] %s\n  got:      %s\n  expected: %s\n", ok ? "OK" : "FAIL", name, hex, expectedHex);
  if (!ok) failures++;
  if (!Sha256::hexEquals(digest, expectedHex)) {
    printf("[FAIL] %s hexEquals() disagreed with toHex() comparison\n", name);
    failures++;
  }
}

int main() {
  // Test 1: prazdny retezec
  {
    Sha256 sha;
    uint8_t digest[32];
    sha.finish(digest);
    check("empty string", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", digest);
  }

  // Test 2: "abc" v jednom volani update()
  {
    Sha256 sha;
    uint8_t digest[32];
    sha.update(reinterpret_cast<const uint8_t*>("abc"), 3);
    sha.finish(digest);
    check("\"abc\" single update", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", digest);
  }

  // Test 3: stejny vstup "abc", ale po jednotlivych bajtech (simulace streamovani po sitovych chunkech)
  {
    Sha256 sha;
    uint8_t digest[32];
    const char* s = "abc";
    for (size_t i = 0; i < 3; i++) {
      sha.update(reinterpret_cast<const uint8_t*>(s + i), 1);
    }
    sha.finish(digest);
    check("\"abc\" byte-by-byte streaming", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", digest);
  }

  // Test 4: vstup presne na hranici bloku (56 bajtu - vynuti padding pres 2 bloky) a vetsi (>64 bajtu, vice bloku)
  {
    Sha256 sha;
    uint8_t digest[32];
    // 56 'a' znaku
    char buf[56];
    memset(buf, 'a', sizeof(buf));
    sha.update(reinterpret_cast<const uint8_t*>(buf), sizeof(buf));
    sha.finish(digest);
    char hex[65];
    Sha256::toHex(digest, hex);
    printf("[INFO] 56x'a' digest: %s (jen sanity - overuje se, ze nedojde k padu/nekonecne smycce)\n", hex);
  }

  // Test 5: vstup >64 bajtu rozdeleny do nerovnomernych chunku (simulace realneho HTTP streamu)
  {
    Sha256 sha;
    uint8_t digest[32];
    char big[200];
    for (int i = 0; i < 200; i++) big[i] = char('a' + (i % 26));

    Sha256 shaOneShot;
    uint8_t digestOneShot[32];
    shaOneShot.update(reinterpret_cast<const uint8_t*>(big), 200);
    shaOneShot.finish(digestOneShot);

    size_t offset = 0;
    size_t chunkSizes[] = {7, 13, 1, 64, 50, 65};
    for (size_t c : chunkSizes) {
      size_t take = c;
      if (offset + take > 200) take = 200 - offset;
      if (take == 0) break;
      sha.update(reinterpret_cast<const uint8_t*>(big + offset), take);
      offset += take;
    }
    if (offset < 200) {
      sha.update(reinterpret_cast<const uint8_t*>(big + offset), 200 - offset);
    }
    sha.finish(digest);

    char hexA[65], hexB[65];
    Sha256::toHex(digest, hexA);
    Sha256::toHex(digestOneShot, hexB);
    bool ok = strcmp(hexA, hexB) == 0;
    printf("[%s] 200B irregular chunking matches one-shot hash\n  chunked:  %s\n  one-shot: %s\n",
           ok ? "OK" : "FAIL", hexA, hexB);
    if (!ok) failures++;
  }

  if (failures == 0) {
    printf("\nVSECHNY TESTY PROSLY\n");
    return 0;
  } else {
    printf("\n%d TEST(U) SELHALO\n", failures);
    return 1;
  }
}
