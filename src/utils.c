#include <math.h>
#include <stdint.h>

typedef uint32_t u32;
typedef int64_t i64;
typedef uint64_t u64;

struct BufHdr {
  size_t len, cap, elementSize;
} __attribute__ ((aligned (8)));

// IMPORTANT: asm.js requires loads and stores to be 8-byte aligned
//            wasm can handle it but might be slower

#define BufHdr(b) ((struct BufHdr*)(b) - 1)

size_t BufLen(void* b) {
  return b ? BufHdr(b)->len : 0;
}

void BufDel(void* b, size_t i) {
  if (b) {
    struct BufHdr* hdr = BufHdr(b);
    char* data = (char*)(hdr + 1);
    --hdr->len;
    memmove(&data[i * hdr->elementSize],
            &data[(i + 1) * hdr->elementSize],
            hdr->elementSize * (hdr->len - i));
  }
}

int BufFindInt(int* b, int value) {
  for (int i = 0; i < BufLen(b); ++i) {
    if (b[i] == value) {
      return i;
    }
  }
  return -1;
}

int BufDelFindInt(int* b, int value) {
  int i = BufFindInt(b, value);
  if (i >= 0) {
    BufDel(b, i);
  }
  return i;
}

void BufPrintInt(FILE* f, int* b) {
  fprintf(f, "[");
  for (size_t i = 0; i < BufLen(b); ++i) {
    fprintf(f, "%d ", b[i]);
  }
  fprintf(f, "]");
}

void BufClear(void* b) {
  if (b) {
    BufHdr(b)->len = 0;
  }
}

void BufFreeClear(void** b) {
  if (b) {
    for (size_t i; i < BufLen(b); ++i) {
      free(b[i]);
    }
    BufClear(b);
  }
}

void BufFree(void *p) {
  void** b = p;
  if (*b) {
    free(BufHdr(*b));
    *b = 0;
  }
}

void _BufAlloc(void* p, size_t count, size_t elementSize) {
  size_t cap = 4, len = 0;
  struct BufHdr* hdr = 0;
  void **b = p;
  if (*b) {
    hdr = BufHdr(*b);
    cap = hdr->cap;
    len = hdr->len;
  }
  while (!hdr || len + count > cap) {
    cap *= 2;
    hdr = realloc(hdr, sizeof(struct BufHdr) + elementSize * cap);
    hdr->cap = cap;
    hdr->elementSize = elementSize;
    *b = hdr + 1;
  }
  hdr->len = len + count;
}

#define BufReserve(b, count) \
  (_BufAlloc((b), (count), sizeof((*(b))[0])), (&(*(b))[BufLen(*(b)) - (count)]))

#define BufAlloc(b) \
  (_BufAlloc((b), 1, sizeof((*(b))[0])), (&(*(b))[BufLen(*(b)) - 1]))

#define BufAllocZero(b) \
  memset(BufAlloc(b), 0, sizeof((*b)[0]))

#define MemZero(p) \
  memset((p), 0, sizeof(p));

#define BufZero(p) \
  memset((p), 0, sizeof((p)[0]) * BufLen(p));

char* BufAllocStrf(char*** b, char* fmt, ...) {
  va_list va;
  va_start(va, fmt);
  int n = vsnprintf(0, 0, fmt, va);
  int sz = n + 1;
  char* p = *BufAlloc(b) = malloc(sz);
  vsnprintf(p, sz, fmt, va);
  va_end(va);
  return p;
}

#define Pack16to32(hi, lo) (((hi) << 16) & 0xFFFF0000 | ((lo) & 0x0000FFFF))
#define LoWord(dw) ((dw) & 0x0000FFFF)
#define HiWord(dw) (((dw) >> 16) & 0x0000FFFF)

i64 ProbToOneIn(double p) {
  if (p <= 0) return 0;
  return round(1 / p + 0.5);
}

// examples:
// percent=75 returns the num of attempts to have 75% chance for an event of probability p to occur
// percent=50 is the median
i64 ProbToGeoDistrQuantileDingle(double p, double percent) {
  if (p <= 0) return 0;
  return round(log(1 - percent / 100) / log(1 - p));
}

// this is just a lookup table with a perfect hash
// https://stackoverflow.com/questions/11376288/fast-computing-of-log2-for-64-bit-integers
int Log2i64(u64 n) {
  static const int table[64] = {
    0, 58, 1, 59, 47, 53, 2, 60, 39, 48, 27, 54, 33, 42, 3, 61,
    51, 37, 40, 49, 18, 28, 20, 55, 30, 34, 11, 43, 14, 22, 4, 62,
    57, 46, 52, 38, 26, 32, 41, 50, 36, 17, 19, 29, 10, 13, 21, 56,
    45, 25, 31, 35, 16, 9, 12, 44, 24, 15, 8, 23, 7, 6, 5, 63
  };
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n |= n >> 32;
  return table[(n * UINT64_C(0x03f6eaf2cd271461)) >> 58];
}

int log2i(u32 n) {
  const int tab32[32] = {
     0,  9,  1, 10, 13, 21,  2, 29,
    11, 14, 16, 18, 22, 25,  3, 30,
     8, 12, 20, 28, 15, 17, 24,  7,
    19, 27, 23,  6, 26,  5,  4, 31
  };
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  return tab32[(n * UINT32_C(0x07C4ACDD)) >> 27];
}
