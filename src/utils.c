#include <math.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int64_t i64;
typedef uint64_t u64;

//
// Allocator: can be passed to some utilities to use a custom allocator
//

typedef struct _Allocator {
  char* name;
  void* param;
  void* (* alloc)(void* param, size_t n);
  void* (* realloc)(void* param, void* p, size_t n);
  void (* free)(void* param, void* p);
} Allocator;

void* allocatorAlloc(Allocator const* allocator, size_t n) {
  return allocator->alloc(allocator->param, n);
}

void* allocatorRealloc(Allocator const* allocator, void* p, size_t n) {
  return allocator->realloc(allocator->param, p, n);
}

void allocatorFree(Allocator const* allocator, void* p) {
  return allocator->free(allocator->param, p);
}

void* xmalloc(void* param, size_t n) {
  void* res = malloc(n);
  if (!res) {
    perror("malloc");
  }
  return res;
}

void* xrealloc(void* param, void* p, size_t n) {
  void* res = realloc(p, n);
  if (!res) {
    perror("realloc");
  }
  return res;
}

void xfree(void* param, void* p) {
  free(p);
}

Allocator const allocatorDefault_ = {
  .param = 0,
  .alloc = xmalloc,
  .realloc = xrealloc,
  .free = xfree,
};

void* norealloc(void* para, void* p, size_t n) {
  fprintf(stderr, "norealloc(%p, %p, %zu): this allocator does not support realloc\n", para, p, n);
  exit(1);
  return 0;
}

void nofree(void* param, void* p) {
  fprintf(stderr, "nofree(%p, %p): this allocator does not support free\n", param, p);
  exit(1);
}

#ifndef allocatorDefault
#define allocatorDefault allocatorDefault_
#endif

//
// Buf: resizable array
//

struct BufHdr {
  Allocator const* allocator;
  size_t len, cap, elementSize;
} __attribute__ ((aligned (8)));

// IMPORTANT: asm.js requires loads and stores to be 8-byte aligned
//            wasm can handle it but might be slower

#define BufHdr(b) ((struct BufHdr*)(b) - 1)

#define BufAt(b, i) \
  ((b)[ BufI(b, i) ])

size_t BufLen(void* b) {
  return b ? BufHdr(b)->len : 0;
}

size_t BufI(void* b, ssize_t i) {
  return i < 0 ? (BufLen(b) + i) : i;
}

void BufDel(void* b, ssize_t i) {
  if (b) {
    i = BufI(b, i);
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

u64 RoundUp2(u64 v);

void _BufAlloc(void* p, size_t count, size_t elementSize, Allocator const* allocator) {
  size_t cap, len = 0;
  struct BufHdr* hdr = 0;
  void **b = p;
  if (*b) {
    hdr = BufHdr(*b);
    cap = hdr->cap;
    len = hdr->len;
  } else {
    size_t r = RoundUp2(count);
    // if count is small, we just default to 4, otherwise we do count / 2 because it get multiplied
    // by 2 later
    cap = r < 8 ? 4 : (r >> 1);
  }
  while (!hdr || len + count > cap) {
    cap <<= 1;
    size_t allocSize = sizeof(struct BufHdr) + elementSize * cap;
    if (hdr) {
      hdr = allocatorRealloc(hdr->allocator, hdr, allocSize);
    } else {
      hdr = allocatorAlloc(allocator, allocSize);
      hdr->allocator = allocator;
    }
    hdr->cap = cap;
    hdr->elementSize = elementSize;
    *b = hdr + 1;
  }
  hdr->len = len + count;
}

#define PreElementSize(array) sizeof((array)[0])

#define BufReserve(b, count) \
  (_BufAlloc((b), (count), PreElementSize(*(b)), &allocatorDefault), \
   &BufAt(*(b), -(count)))

// if you want to use a custom allocator, call this on the desired buf before anything else.
// count can be 0
#define BufReserveWithAllocator(b, count, allocator) \
  (_BufAlloc((b), (count), PreElementSize(*(b)), allocator), \
   &BufAt(*(b), -(count)))

#define BufAlloc(b) \
  (_BufAlloc((b), 1, PreElementSize(*(b)), &allocatorDefault), \
   &BufAt(*(b), -1))

#define MemZero(p) _MemZero(p, sizeof(*p))

void* _MemZero(void* p, size_t size) {
  memset(p, 0, size);
  return p;
}

#define BufZero(p) \
  memset((p), 0, PreElementSize(p) * BufLen(p))

#define BufAllocZero(b) MemZero(BufAlloc(b))
#define BufReserveZero(b, count) MemZero(BufReserve(b, count))

#define BufEach(type, b, x) \
  if (b) for (type* x = b; x < b + BufLen(b); ++x)

#define BufEachi(b, i) \
  for (size_t i = 0; i < BufLen(b); ++i)

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

char* BufStrDupn(char* p, size_t len) {
  char* res = 0;
  BufReserve(&res, len + 1);
  memcpy(res, p, len);
  res[len] = 0;
  return res;
}

#define BufStrDup(s) BufStrDupn(s, strlen(s))

//
// Statistics
//

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

//
// Memory arena
//

typedef struct _Arena {
  Allocator const* allocator;
  u8** chunks;
  size_t align;
} Arena;

#define ArenaInitializer { \
  .allocator = &allocatorDefault, \
  .chunks = 0, \
  .align = 4096, \
}

void* ArenaAlloc(Arena* a, size_t x) {
  // look for empty space in existing chunks
  BufEach(u8*, a->chunks, chunk) {
    struct BufHdr* hdr = BufHdr(*chunk);
    if (hdr->cap - hdr->len >= x) {
      void* res = chunk + hdr->len;
      hdr->len += x;
      return res;
    }
  }
  // no space found, alloc a new chunk
  size_t n = a->align;
  x = (x + (n - 1)) & ~(n - 1); // round up to alignment
  u8** pchunk = BufAllocZero(&a->chunks);
  BufReserveWithAllocator(pchunk, x, a->allocator);
  return *pchunk;
}

void* ArenaAlloc_(void* param, size_t x) {
  return ArenaAlloc(param, x);
}

void ArenaFree(Arena* a) {
  BufEach(u8*, a->chunks, chunk) {
    BufFree(chunk);
  }
  BufFree(&a->chunks);
}

Allocator ArenaAllocator(Arena* a) {
  return (Allocator){
    .param = a,
    .alloc = ArenaAlloc_,
    .realloc = norealloc,
    .free = nofree,
  };
}

//
// Math
//

// https://stackoverflow.com/questions/11376288/fast-computing-of-log2-for-64-bit-integers
// https://graphics.stanford.edu/~seander/bithacks.html

int Log2i64(u64 n) {
  const int table[64] = {
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

int Log2i(u32 n) {
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

u64 RoundUp2(u64 v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v |= v >> 32;
  v++;
  return v;
}
