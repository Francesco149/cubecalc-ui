#ifndef UTILS_H
#define UTILS_H

//
// Basic Types
//

#include <unistd.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int64_t i64;
typedef uint64_t u64;

//
// Misc Macros
//

#define ArrayElementSize(array) sizeof((array)[0])
#define ArrayLength(array) (sizeof(array) / ArrayElementSize(array))
#define MemZero(p) memset(p, 0, sizeof(*p))

// these are for macros that define a list of things that will be passed to another macro
#define StringifyComma(x) #x,

//
// Allocator: can be passed to some utilities to use a custom allocator
//

typedef struct _Allocator {
  void* param;
  void* (* alloc)(void* param, size_t n);
  void* (* realloc)(void* param, void* p, size_t n);
  void (* free)(void* param, void* p);
} Allocator;

void* allocatorAlloc(Allocator const* allocator, size_t n);
void* allocatorRealloc(Allocator const* allocator, void* p, size_t n);
void allocatorFree(Allocator const* allocator, void* p);

// these versions do nothing without erroring if realloc/free are unavailable
void* allocatorTryRealloc(Allocator const* allocator, void* p, size_t n);
void allocatorTryFree(Allocator const* allocator, void* p);

extern Allocator const allocatorDefault_;

// redefine this to change the default allocator for a block of code without passing it manually
#ifndef allocatorDefault
#define allocatorDefault allocatorDefault_
#endif

// use these when you don't want the allocator to allow realloc or free. if they are called
// an error is thrown and the program exits
void* norealloc(void* para, void* p, size_t n);
void nofree(void* param, void* p);

//
// Buf: resizable array
//
// NOTE: when a function parameter is named pp, that means a _pointer to a buf_
//
//   int* buf = 0;
//   // BufAlloc and BufFree take a "pp" parameter because they can change the pointer
//   *BufAlloc(&buf) = 10;
//   *BufAlloc(&buf) = 20;
//   printf("%d\n", BufLen(buf)); // BufLen doesn't take a pp parameter so it's just the buf
//   BufFree(&buf);
//

// return number of elements
size_t BufLen(void* b);

// fancy indexing. if i is negative, it will start from the end of the array (BufLen(b) - i)
// this is used by other functions that take indices
size_t BufI(void* b, ssize_t i);

// macro to fancy index
//
//   int* a = &BufAt(x, -1);
//
#define BufAt(b, i) \
  ((b)[ BufI(b, i) ])

// remove element at fancy index i
void BufDel(void* b, ssize_t i);

// index of value (-1 if it can't be found)
int BufFindInt(int* b, int value);

// delete value if it can be found and return its index (-1 if it can't be found)
int BufDelFindInt(int* b, int value);

// empty without freeing memory
void BufClear(void* b);

// call free on every element of a buf of pointers, then empty it without freeing memory
void BufFreeClear(void** b);

// release memory. the buf pointer is also zeroed by this
void BufFree(void* pp);

// grows by count, reallocating as needed. length += count. pp can point to a null buf
// returns a pointer to the start of the new elements (fancy index -count)
#define BufReserve(pp, count) \
  (_BufAlloc((pp), (count), ArrayElementSize(*(pp)), &allocatorDefault), \
   &BufAt(*(pp), -(count)))

#define BufAlloc(pp) BufReserve(pp, 1)

// if you want to use a custom allocator, call this on the desired buf before anything else.
// count can be 0
#define BufReserveWithAllocator(pp, count, allocator) \
  (_BufAlloc((pp), (count), ArrayElementSize(*(pp)), allocator), \
   &BufAt(*(pp), -(count)))

// it's recommended to call this through macros like BufReserve and BufAlloc.
// see the description of BufReserve
void _BufAlloc(void* pp, size_t count, size_t elementSize, Allocator const* allocator);

#define BufZero(p) memset((p), 0, ArrayElementSize(p) * BufLen(p))
#define BufAllocZero(b) MemZero(BufAlloc(b))
#define BufReserveZero(pp, count) \
  (_BufAllocZero((pp), (count), ArrayElementSize(*(pp)), &allocatorDefault), \
   &BufAt(*(pp), -(count)))
void _BufAllocZero(void* pp, size_t count, size_t elementSize, Allocator const* allocator);

//
// shortcut to loop over every element
//
//   int* x = 0;
//   *BufAlloc(&x) = 10;
//   *BufAlloc(&x) = 20;
//   BufEach(int, x, pval) {
//     printf("%d\n", *pval);
//   }
//
#define BufEach(type, b, x) \
  if (b) for (type* x = b; x < b + BufLen(b); ++x)

//
// shortcut to loop over every index
//
//   int* x = 0;
//   *BufAlloc(&x) = 10;
//   *BufAlloc(&x) = 20;
//   BufEachi(x, i) {
//     printf("x[%zu] = %d\n", i, x[i]);
//   }
//
#define BufEachi(b, i) \
  for (size_t i = 0; i < BufLen(b); ++i)

// pp points to a buf of char pointers.
// malloc and format a string and append it to the buf.
// returns the formatted string (same pointer that will be appended to the buf).
char* BufAllocStrf(char*** pp, char* fmt, ...);

// shallow copy
#define BufDup(p) _BufDup(p, &allocatorDefault)
void* _BufDup(void* p, Allocator const* allocator);

// returns a zero-terminated char buf containing a copy of len bytes at p
#define BufStrDupn(p, len) _BufStrDupn(p, len, &allocatorDefault)
char* _BufStrDupn(char* p, size_t len, Allocator const* allocator);

//returns a zero-terminated char buf containing a copy of null-terminated string p
#define BufStrDup(p) _BufStrDup(p, &allocatorDefault)
char* _BufStrDup(char* p, Allocator const* allocator);

// convert a Buf of structs to a Buf of pointers to the structs (no copying)
// this is useless normally, it's mainly for protobuf

#define BufToProto(protoStruc, member, b) \
  (protoStruc)->member = _BufToProto(b, &(protoStruc)->n_##member, &allocatorDefault)

void* _BufToProto(void* b, size_t *pn, Allocator const* allocator);

//
// Statistics
//

//
// convert a probability between 0.0 and 1.0 to a rounded integer representing the "one in" chance.
//
// example:
//   0.5 -> 2
//   0.45 -> 2
//   0.25 -> 4
//   0.3 -> 3
//   0.33 -> 3
//
i64 ProbToOneIn(double p);

//
// return the geometric quantile for p, in rounded "one in" form
//
// examples:
//   percent=75: the num of attempts to have 75% chance for an event of probability p to occur
//   percent=50: the median
//
i64 ProbToGeoDistrQuantileDingle(double p, double percent);

//
// Memory arena
//
// this allocator is inteded for cases when you will free all allocations at once and no resizing
// is needed. it does not support realloc or free (except for freeing the entire arena)
//

typedef struct _Arena Arena;

#define ArenaInit() _ArenaInit(&allocatorDefault)
Arena* _ArenaInit(Allocator const* allocator);
void* ArenaAlloc(Arena* a, size_t n);
void ArenaFree(Arena* a);
Allocator ArenaAllocator(Arena* a);

//
// Math
//

// fast log base 2 of a 64-bit integer
int Log2i64(u64 n);

// fast log base 2 of a 32-bit integer
int Log2i(u32 n);

// round to the next higher power of 2
u64 RoundUp2(u64 v);

#endif

#if defined(UTILS_IMPLEMENTATION) && !defined(UTILS_UNIT)
#define UTILS_UNIT

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

//
// Allocator
//

void* allocatorAlloc(Allocator const* allocator, size_t n) {
  return allocator->alloc(allocator->param, n);
}

void* allocatorRealloc(Allocator const* allocator, void* p, size_t n) {
  return allocator->realloc(allocator->param, p, n);
}

void* allocatorTryRealloc(Allocator const* allocator, void* p, size_t n) {
  if (allocator->realloc == norealloc) {
    return 0;
  }
  return allocatorRealloc(allocator, p, n);
}

void allocatorTryFree(Allocator const* allocator, void* p) {
  if (allocator->free != nofree) {
    allocatorFree(allocator, p);
  }
}

void allocatorFree(Allocator const* allocator, void* p) {
  return allocator->free(allocator->param, p);
}

static void* xmalloc(void* param, size_t n) {
  void* res = malloc(n);
  if (!res) {
    perror("malloc");
  }
  return res;
}

static void* xrealloc(void* param, void* p, size_t n) {
  void* res = realloc(p, n);
  if (!res) {
    perror("realloc");
  }
  return res;
}

static void xfree(void* param, void* p) {
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
// Buf
//

struct BufHdr {
  Allocator const* allocator;
  size_t len, cap, elementSize;
} __attribute__ ((aligned (8)));

// IMPORTANT:
// asm.js requires loads and stores to be 8-byte aligned.
// wasm can handle it but might be slower.
// either way we want to align this

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

void BufClear(void* b) {
  if (b) {
    BufHdr(b)->len = 0;
  }
}

void BufFreeClear(void** b) {
  if (b) {
    BufEach(void*, b, pp) {
      free(*pp);
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

void _BufAllocZero(void* pp, size_t count, size_t elementSize, Allocator const* allocator) {
  u8** b = pp;
  _BufAlloc(pp, count, elementSize, allocator);
  memset(*b + (BufLen(*b) - count) * elementSize, 0, count * elementSize);
}

void* _MemZero(void* p, size_t size) {
  memset(p, 0, size);
  return p;
}

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

void* _BufDup(void* p, Allocator const* allocator) {
  if (!p) {
    return 0;
  }
  struct BufHdr* hdr = BufHdr(p);
  size_t cap = RoundUp2(hdr->len);
  size_t allocSize = sizeof(struct BufHdr) + hdr->elementSize * cap;
  struct BufHdr* copy = allocatorAlloc(allocator, allocSize);
  copy->allocator = allocator;
  copy->len = hdr->len;
  copy->cap = cap;
  copy->elementSize = hdr->elementSize;
  void* res = copy + 1;
  memcpy(res, p, hdr->len * hdr->elementSize);
  return res;
}

char* _BufStrDupn(char* p, size_t len, Allocator const* allocator) {
  char* res = 0;
  BufReserveWithAllocator(&res, len + 1, allocator);
  memcpy(res, p, len);
  res[len] = 0;
  return res;
}

char* _BufStrDup(char* s, Allocator const* allocator) {
  return _BufStrDupn(s, strlen(s), allocator);
}

void* _BufToProto(void* b, size_t *pn, Allocator const* allocator) {
  if (!BufLen(b)) return 0;
  u8* p = b;
  void** res = 0;
  struct BufHdr* hdr = BufHdr(b);
  *pn = hdr->len;
  BufReserveWithAllocator(&res, hdr->len, allocator);
  BufEachi(b, i) {
    res[i] = p + i * hdr->elementSize;
  }
  return res;
}

//
// Statistics
//

i64 ProbToOneIn(double p) {
  if (p <= 0) return 0;
  return round(1 / p + 0.5);
}

i64 ProbToGeoDistrQuantileDingle(double p, double percent) {
  if (p <= 0) return 0;
  return round(log(1 - percent / 100) / log(1 - p));
}

//
// Memory arena
//

struct _Arena {
  Allocator const* allocator;
  u8** chunks;
  size_t align;
};

Arena* _ArenaInit(Allocator const* allocator) {
  Arena* a = allocatorAlloc(allocator, sizeof(Arena));
  a->allocator = allocator;
  a->chunks = 0;
  a->align = 4096;
  return a;
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

void ArenaFree(Arena* a) {
  BufEach(u8*, a->chunks, chunk) {
    BufFree(chunk);
  }
  BufFree(&a->chunks);
  allocatorTryFree(a->allocator, a);
}

static void* _ArenaAlloc(void* param, size_t x) {
  return ArenaAlloc(param, x);
}

Allocator ArenaAllocator(Arena* a) {
  return (Allocator){
    .param = a,
    .alloc = _ArenaAlloc,
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
#endif
