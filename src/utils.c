#ifndef UTILS_H
#define UTILS_H

#include <stddef.h> // size_t
#include <stdint.h> // intmax_t
#include <limits.h> // SIZE_MAX and others
#include <stdarg.h>
#include <stdio.h>

//
// Misc Macros
//

#define ArrayElementSize(array) sizeof((array)[0])
#define ArrayLength(array) (sizeof(array) / ArrayElementSize(array))

#define Concat_(x, y) x##y
#define Concat(x, y) Concat_(x, y)

#define Repeat(n) for (size_t Concat(i, __LINE__) = n; Concat(i, __LINE__)--;)

// see BufI
#define ArrayI(arr, i) \
  ((i) < 0 ? ((intmax_t)ArrayLength(arr) + (i)) : (i))

// see BufEach
#define ArrayEach(type, arr, x) \
  ArrayEachRange(type, arr, 0, -1, x)

#define ArrayEachRange(type, arr, start, end, x) \
  EachRange(type, arr, ArrayI(arr, start), ArrayI(arr, end), x)

#define EachRange(type, arr, start, end, x) \
  for (type* x = (arr) + start; x <= (arr) + end; ++x)

// see BufEachi
#define ArrayEachi(arr, i) \
  ArrayEachiRange(arr, 0, -1, i)

#define ArrayEachiRange(arr, start, end, i) \
  Range(ArrayI(arr, start), ArrayI(arr, end), i)

// for (size_t i = 0; i < end; ++i)
#define RangeBefore(end, i) RangeFromBefore(0, end, i)

// for (size_t i = start; i < end; ++i)
#define RangeFromBefore(start, end, i) Range(start, (intmax_t)(end) - 1, i)

// for (size_t i = 0; i <= end; ++i)
#define RangeTill(end, i) Range(0, end, i)

// for (size_t i = start; i <= end; ++i)
#define Range(start, end, i) \
  for (intmax_t i = start; i <= end; ++i)

// see BufOp
#define ArrayOp(op, arr, paccum) \
  ArrayOpRange(op, arr, 0, -1, paccum)

#define ArrayOpRange(op, arr, start, end, paccum) \
  OpRange(op, arr, ArrayI(arr, start), ArrayI(arr, end), paccum)

#define OpRange(op, arr, start, end, paccum) \
  Range(start, end, Concat(i, __LINE__)) { \
    *(paccum) op##= (arr)[Concat(i, __LINE__)]; \
  }

#define ArgsLength(type, ...) (sizeof((type[]){__VA_ARGS__}) / sizeof(type))
#define MemZero(p) memset(p, 0, sizeof(*p))
#define Min(a, b) ((a) < (b) ? (a) : (b))
#define Max(a, b) ((a) > (b) ? (a) : (b))
#define Clamp(x, lo, hi) Max(lo, Min(hi, x))

// this treats the int array as a bitmask (32bit per element for int) and checks if bit i is set
#define ArrayBit(array, bit)     (ArrayBitSlot(array, bit) & ArrayBitMask(array, bit))
#define ArrayBitSet(array, bit)   ArrayBitSlot(array, bit) |= ArrayBitMask(array, bit)
#define ArrayBitClear(array, bit) ArrayBitSlot(array, bit) &= ~ArrayBitMask(array, bit)

#define ArrayBitVal(array, bit) \
  ((ArrayBitSlot(array, bit) & ArrayBitMask(array, bit)) >> ArrayBitShift(array, bit))

// NOTE: this doesn't clear if val is 0
#define ArrayBitSetVal(array, bit, val) \
  ArrayBitSlot(array, bit) |= ArrayBitMask(array, bit) * (val & 1)

// returns the allocation size for an array bitmask that will be stored in arr. arr is an integer
// pointer of any size, the purpose of this function is to figure out how many bits can be stored
// per element and therefore how big the array should be for nbits
#define ArrayBitSize(arr, nbits) \
  _ArrayBitSize(ArrayElementBitSize(arr), ArrayElementSize(arr), nbits)
size_t _ArrayBitSize(size_t elementBitSize, size_t elementSize, size_t nbits);

#define ArrayBitElements(arr, nbits) \
  (ArrayBitSize(arr, nbits) / ArrayElementSize(arr))

// see BufFindInt
#define ArrayFindInt(arr, value) _ArrayFindInt(arr, ArrayLength(arr), value)
intmax_t _ArrayFindInt(int const* arr, size_t n, int value);

// array bitmask macros used internally
#define ArrayElementBitSize(array) (ArrayElementSize(array) << 3)
#define ArrayBitSlot(array, bit) (array)[(bit) / ArrayElementBitSize(array)]
#define ArrayBitShift(array, bit) ((bit) % ArrayElementBitSize(array))
#define ArrayBitMask(array, bit) ((uintmax_t)1 << ArrayBitShift(array, bit))

// these are for macros that define a list of things that will be passed to another macro
#define DEF_PREFIX(x) x
#define DEF_SUFFIX(x) x
#define PREFIX(x) DEF_PREFIX(x)
#define SUFFIX(x) DEF_SUFFIX(x)
#define Stringify_(x) #x
#define Stringify(x) Stringify_(x)
#define StringifyComma(x) Stringify(SUFFIX(PREFIX(x))),
#define AppendComma(x) SUFFIX(PREFIX(x)),
#define CountMacroCalls(x) 1+

//
// shortcut to define an enum and a table of the value names
// x should be a macro that calls the macro it gets passed on every value.
// redefine PREFIX and/or SUFFIX to add a prefix/suffix to the enum values (not the strings)
//

/*
     #define MyEnum(f) \
       f(VALUE_ONE) \
       f(VALUE_TWO) \

     DefEnum(MyEnum);
*/

#define DefEnum(x) \
  typedef enum _##x { x##s(AppendComma) } x; \
  DefEnumNames(x) \

// same as above but the names is external for headers
#define DefEnumHdr(x) \
  typedef enum _##x { x##s(AppendComma) } x; \
  extern char const* const x##Names[x##s(CountMacroCalls) 0]

#define DefEnumNames(x) \
  char const* const x##Names[] = { x##s(StringifyComma) }

//
// Allocator: can be passed to some utilities to use a custom allocator
//

typedef struct _Allocator {
  void* param;
  void* (* alloc)(void* param, size_t n);
  void* (* realloc)(void* param, void* p, size_t n);
  void (* free)(void* param, void* p);
} Allocator;

void* AllocatorAlloc(Allocator const* allocator, size_t n);
void* AllocatorRealloc(Allocator const* allocator, void* p, size_t n);
void AllocatorFree(Allocator const* allocator, void* p);

// these versions do nothing without erroring if realloc/free are unavailable
void* AllocatorTryRealloc(Allocator const* allocator, void* p, size_t n);
void AllocatorTryFree(Allocator const* allocator, void* p);

extern Allocator const allocatorDefault_;
extern Allocator const allocatorNull_; // this allocator errors on any attempt to (re)alloc/free

// redefine this to change the default allocator for a block of code without passing it manually
#ifndef allocatorDefault
#define allocatorDefault allocatorDefault_
#endif

// use these when you don't want the allocator to allow realloc or free. if they are called
// an error is thrown and the program exits
void* noalloc(void* para, size_t n);
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

// number of elements
#define BufLen(b) \
  ((b) ? BufHdr(b)->len : 0)

// fancy indexing. if i is negative, it will start from the end of the array (ArrayLength(arr) - i)
// this is used by other functions that take indices
#define BufI(b, i) \
  ((i) < 0 ? ((intmax_t)BufLen(b) + (i)) : (i))

// macro to fancy index
//
//   BufAt(x, -1) = 10;
//
#define BufAt(b, i) \
  ((b)[ BufI(b, i) ])

// remove element at fancy index i
void BufDel(void* b, intmax_t i);

// index of value (-1 if it can't be found)
#define BufFindInt(b, value) _ArrayFindInt(b, BufLen(b), value)

// delete value if it can be found and return its index (-1 if it can't be found)
intmax_t BufDelFindInt(int* b, int value);

// empty without freeing memory
void BufClear(void* b);

// call free on every element of a buf of pointers, then empty it without freeing memory
#define BufFreeClear(b) _BufFreeClear(b, &allocatorDefault)
void _BufFreeClear(void** b, Allocator const* allocator);

// release memory. the buf pointer is also zeroed by this
void BufFree(void* pp);

// grows by count, reallocating as needed. length += count. pp can point to a null buf
// returns a pointer to the start of the new elements (fancy index -count)
#define BufReserve(pp, count) \
  (_BufAlloc((pp), (count), ArrayElementSize(*(pp)), &allocatorDefault), \
   &BufAt(*(pp), -(intmax_t)(count)))

#define BufAlloc(pp) BufReserve(pp, 1)

// if you want to use a custom allocator, call this on the desired buf before anything else.
// count can be 0
#define BufReserveWithAllocator(pp, count, allocator) \
  (_BufAlloc((pp), (count), ArrayElementSize(*(pp)), allocator), \
   &BufAt(*(pp), -(intmax_t)(count)))

// it's recommended to call this through macros like BufReserve and BufAlloc.
// see the description of BufReserve
void _BufAlloc(void* pp, size_t count, size_t elementSize, Allocator const* allocator);

#define BufZero(p) memset((p), 0, ArrayElementSize(p) * BufLen(p))
#define BufAllocZero(b) MemZero(BufAlloc(b))
#define BufReserveZero(pp, count) \
  (_BufAllocZero((pp), (count), ArrayElementSize(*(pp)), &allocatorDefault), \
   &BufAt(*(pp), -(intmax_t)(count)))
void _BufAllocZero(void* pp, size_t count, size_t elementSize, Allocator const* allocator);

// append other (Buf ptr) to pp (ptr to Buf ptr) in place. return *pp
void* BufCat(void* pp, void const* other);

#define BufCpy(pdst, src) \
  BufReserve(pdst, BufLen(src)), \
  memcpy(*(pdst), src, BufLen(src) * BufHdr(src)->elementSize)

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
  BufEachRange(type, b, 0, -1, x)

#define BufEachRange(type, b, start, end, x) \
  if (b) EachRange(type, b, BufI(b, start), BufI(b, end), x)

#define BufCount(type, b, condition, countVar) \
  BufCountRange(type, b, 0, -1, condition, countVar)

#define BufCountRange(type, b, start, end, condition, countVar) \
  BufEachRange(type, b, start, end, x) { \
    if (condition) { \
      ++(countVar); \
    } \
  }

// allocates a bit mask at the end of pmaskBuf and fills it according to condition.
// if elements of buffer b match condition, the corresponding bit in maskBuf will be set.
#define BufMask(type, b, condition, pmaskBuf) { \
  intmax_t numElements = ArrayBitElements(*(pmaskBuf), BufLen(b)); \
  (void)BufReserve(pmaskBuf, numElements); \
  size_t bi = BufI(*(pmaskBuf), -numElements); \
  BufEachi(b, i) { \
    type* x = &(b)[i]; \
    if (condition) { \
      ArrayBitSet(*(pmaskBuf) + bi, i); \
    } else { \
      ArrayBitClear(*(pmaskBuf) + bi, i); \
    } \
  } \
}

intmax_t* BufAND(intmax_t* a, intmax_t* b); // a &= b; return a
intmax_t* BufOR(intmax_t* a, intmax_t* b); // a |= b; return a
intmax_t* BufNOR(intmax_t* a, intmax_t* b); // a = ~(a | b); return a
intmax_t* BufNOT(intmax_t* a); // a = ~a; return a

// foreach (x in b)
//   *paccum op= x
#define BufOp(op, b, paccum) \
  BufOpRange(op, b, 0, -1, paccum)

#define BufOpRange(op, b, start, end, paccum) \
  OpRange(op, b, BufI(b, start), BufI(b, end), paccum)

// a op= b
#define BufOp2(op, a, b) \
  BufEachi(a, Concat(i, __LINE__)) { \
    (a)[Concat(i, __LINE__)] op##= (b)[Concat(i, __LINE__)]; \
  }

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
  BufEachiRange(b, 0, -1, i)
#define BufEachiRange(b, start, end, i) \
  Range(BufI(b, start), BufI(b, end), i)

// pp points to a buf of char pointers.
// malloc and format a string and append it to the buf.
// returns the formatted string (same pointer that will be appended to the buf).
#define BufAllocStrf(b, fmt, ...) _BufAllocStrf(&allocatorDefault, b, fmt, __VA_ARGS__)
#define BufAllocVStrf(b, fmt, va) _BufAllocVStrf(&allocatorDefault, b, fmt, va)
char* _BufAllocStrf(Allocator const* allocator, char*** pp, char* fmt, ...);
char* _BufAllocVStrf(Allocator const* allocator, char*** pp, char* fmt, va_list va);

// append formatted string to a char buf. the buf is kept zero terminated
char* BufAllocCharsf(char** pp, char* fmt, ...);

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

// declares a statically initialized buf with the given name.
#define Buf(type, name, ...) \
  BufH(type, name##Hdr, __VA_ARGS__); \
  type* const name = &name##Hdr.data[0]

// if you don't want the pointer to be automatically declared, use this and use name.data as a buf
#define BufH(type, name, ...) \
  BufH_(type, name, ArgsLength(type, __VA_ARGS__), __VA_ARGS__)

#define BufH_(type, name, n, ...) \
struct { \
  struct BufHdr hdr; \
  type data[n]; \
} name = { \
  .hdr = (struct BufHdr){ \
    .allocator = &allocatorNull_, \
    .len = n, \
    .cap = n, \
    .elementSize = sizeof(type), \
  }, \
  .data = { __VA_ARGS__ }, \
};

// header internally used by Bufs. this is only exposed so that we can declare const/static Buf's
// must be aligned so that the data is also aligned (which is right after the header)
struct BufHdr {
  Allocator const* allocator;
  size_t len, cap, elementSize;
} __attribute__ ((aligned (8)));

#define BufHdr(b) ((struct BufHdr*)(b) - 1)

// replace indices in indicesBuf with corresponding elements from sourceBuf
// store result in presultBuf (a pointer to a buf)
// note: indices are NOT fancy indices. if you want to do fancy indexing you need to do it in
//       advance before calling this
#define BufIndex(sourceBuf, indicesBuf, presultBuf) \
  BufEachi(indicesBuf, i) { \
    *BufAlloc(presultBuf) = (sourceBuf)[(indicesBuf)[i]]; \
  } \

// same as BufIndex but on a bitmask array (indices are bit indices)
// the result is allocated at the end of presultBuf
void BufIndexBit(intmax_t* sourceBuf, intmax_t* indices, intmax_t** presultBuf);

//
// generate all possible combination of ranges of integers.
// ranges is an array of ranges for each element (min, max inclusive)
// n is the number of elements
// the result is a flattened buf containing all the combinations
//
// example:
//   ranges = {
//     0, 1,
//     10, 11,
//     20, 21,
//   }
//   n = 3
//
//   result = {
//     0, 10, 20,
//     0, 10, 21,
//     0, 11, 20,
//     0, 11, 21,
//     1, 10, 20,
//     1, 10, 21,
//     1, 11, 20,
//     1, 11, 21,
//   }
//
intmax_t* BufCombos(intmax_t* ranges, size_t n);

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
intmax_t ProbToOneIn(double p);

//
// return the geometric quantile for p, in rounded "one in" form
//
// examples:
//   percent=75: the num of attempts to have 75% chance for an event of probability p to occur
//   percent=50: the median
//
intmax_t ProbToGeoDistrQuantileDingle(double p, double percent);

//
// Humanize: write numbers in a human friendly form (for example 2.14b instead of 2147483647
//

void Humanize(char* buf, size_t sz, intmax_t value);

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
// Map
//
// map with integer keys. allocator only requires alloc until the map needs to be
// re-allocated, in which case free is also needed. if you use an allocator with no free, you can
// pre-allocate a larger map with MapInitCap. free+alloc happens when the number of values is half
// of cap
//

typedef struct _Map Map;

#define MapInit() _MapInit(&allocatorDefault)
#define MapInitCap(cap) _MapInitCap(&allocatorDefault, cap)
Map* _MapInit(Allocator const* allocator);
void MapFree(Map* m);

// returns 0 if key is missing. use MapHas to test for presence
void* MapGet(Map* m, int key);
int MapHas(Map* m, int key);

// delete a key and return the value it had
void MapDel(Map* m, int key);

// set a key, return non-zero when it succeeds. should only fail if out of memory or can't realloc
int MapSet(Map* m, int key, void* value);

// returns a Buf with all the keys
#define MapKeys(m) _MapKeys(m, &allocatorDefault)
int* _MapKeys(Map* m, Allocator const* allocator);

//
// Math
//

// fast log base 2 of a 32-bit integer
int Log2i(int n);

// round to the next higher power of 2
size_t RoundUp2(size_t v);

// number of set bits in arbitrary array of bytes
size_t BitCount(void* data, size_t bytes);

// hash functions
unsigned HashInt(unsigned x);

//
// Align: right justifies a group of lines
//
// example usage:
//
//   Align* a = AlignInit();
//   Range(1, 3, i) {
//     AlignFeed(a, "%d", " <- look at this cool power of two!", 1 << (i * 8));
//   }
//   AlignPrint(a, stdout);
//   AlignFree(a);
//
// output:
//       256 <- look at this cool power of two!
//     65536 <- look at this cool power of two!
//  16777216 <- look at this cool power of two!
//

typedef struct _Align Align;

#define AlignFeed(al, alignFmt, restFmt, ...) \
  _AlignFeed(al, alignFmt, alignFmt restFmt, __VA_ARGS__)

#define AlignInit() _AlignInit(&allocatorDefault)
Align* _AlignInit(Allocator const* allocator);
void _AlignFeed(Align* al, char* alignFmt, char* fmt, ...);
void AlignPrint(Align* al, FILE* f);
void AlignFree(Align* al);

#endif

#if defined(UTILS_IMPLEMENTATION) && !defined(UTILS_UNIT)
#define UTILS_UNIT

#include <math.h>
#include <string.h>
#include <stdlib.h>

//
// Misc
//

size_t _ArrayBitSize(size_t elementBitSize, size_t elementSize, size_t count) {
  size_t alignBits = elementBitSize - 1;
  size_t bitCnt = (count + alignBits) & ~(alignBits);
  size_t elements = bitCnt / elementBitSize;
  return elements * elementSize;
}

intmax_t _ArrayFindInt(int const* arr, size_t n, int value) {
  RangeBefore(n, i) {
    if (arr[i] == value) {
      return i;
    }
  }
  return -1;
}

//
// Allocator
//

void* AllocatorAlloc(Allocator const* allocator, size_t n) {
  return allocator->alloc(allocator->param, n);
}

void* AllocatorRealloc(Allocator const* allocator, void* p, size_t n) {
  return allocator->realloc(allocator->param, p, n);
}

void* AllocatorTryRealloc(Allocator const* allocator, void* p, size_t n) {
  if (allocator->realloc == norealloc) {
    return 0;
  }
  return AllocatorRealloc(allocator, p, n);
}

void AllocatorTryFree(Allocator const* allocator, void* p) {
  if (allocator->free != nofree) {
    AllocatorFree(allocator, p);
  }
}

void AllocatorFree(Allocator const* allocator, void* p) {
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

void* noalloc(void* para, size_t n) {
  fprintf(stderr, "noalloc(%p, %zu): this allocator does not support alloc\n", para, n);
  exit(1);
  return 0;
}

void* norealloc(void* para, void* p, size_t n) {
  fprintf(stderr, "norealloc(%p, %p, %zu): this allocator does not support realloc\n", para, p, n);
  exit(1);
  return 0;
}

void nofree(void* param, void* p) {
  fprintf(stderr, "nofree(%p, %p): this allocator does not support free\n", param, p);
  exit(1);
}

Allocator const allocatorNull_ = {
  .param = 0,
  .alloc = noalloc,
  .realloc = norealloc,
  .free = nofree,
};

#ifndef allocatorDefault
#define allocatorDefault allocatorDefault_
#endif

//
// Buf
//

void BufDel(void* b, intmax_t i) {
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

intmax_t BufDelFindInt(int* b, int value) {
  intmax_t i = BufFindInt(b, value);
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

void _BufFreeClear(void** b, Allocator const* allocator) {
  if (b) {
    BufEach(void*, b, pp) {
      AllocatorFree(allocator, *pp);
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

size_t RoundUp2(size_t v);

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
      hdr = AllocatorRealloc(hdr->allocator, hdr, allocSize);
    } else {
      hdr = AllocatorAlloc(allocator, allocSize);
      hdr->allocator = allocator;
    }
    hdr->cap = cap;
    hdr->elementSize = elementSize;
    *b = hdr + 1;
  }
  hdr->len = len + count;
}

void _BufAllocZero(void* pp, size_t count, size_t elementSize, Allocator const* allocator) {
  char** b = pp;
  _BufAlloc(pp, count, elementSize, allocator);
  memset(*b + BufI(*b, -(intmax_t)count) * elementSize, 0, count * elementSize);
}

void* BufCat(void* b, void const* other) {
  char** pp = b;
  if (BufLen(other)) {
    Allocator const* allocator = *pp ? BufHdr(*pp)->allocator : &allocatorDefault_;
    intmax_t len = BufLen(other);
    size_t es = BufHdr(other)->elementSize;
    _BufAlloc(pp, len, es, allocator);
    memcpy(*pp + BufI(*pp, -len) * es, other, len * es);
  }
  return *pp;
}

intmax_t* BufAND(intmax_t* a, intmax_t* b) {
  BufOp2(&, a, b);
  return a;
}

intmax_t* BufOR(intmax_t* a, intmax_t* b) {
  BufOp2(|, a, b);
  return a;
}

intmax_t* BufNOR(intmax_t* a, intmax_t* b) {
  BufEachi(a, i) {
    a[i] = ~(a[i] | b[i]);
  }
  return a;
}

intmax_t* BufNOT(intmax_t* a) {
  BufEach(intmax_t, a, x) {
    *x = ~(*x);
  }
  return a;
}

char* _BufAllocStrf(Allocator const* allocator, char*** b, char* fmt, ...) {
  va_list va;
  va_start(va, fmt);
  char* res = _BufAllocVStrf(allocator, b, fmt, va);
  va_end(va);
  return res;
}

char* _BufAllocVStrf(Allocator const* allocator, char*** pp, char* fmt, va_list va) {
  va_list va2;
  va_copy(va2, va);
  int n = vsnprintf(0, 0, fmt, va);
  int sz = n + 1;
  char* p = *BufAlloc(pp) = AllocatorAlloc(allocator, sz);
  vsnprintf(p, sz, fmt, va2);
  return p;
}

char* BufAllocCharsf(char** pp, char* fmt, ...) {
  va_list va;
  // remove null terminator if buf is not empty
  if (BufLen(*pp)) {
    --BufHdr(*pp)->len;
  }
  va_start(va, fmt);
  int n = vsnprintf(0, 0, fmt, va);
  va_end(va);
  int sz = n + 1;
  char* p = BufReserve(pp, sz);
  va_start(va, fmt);
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
  struct BufHdr* copy = AllocatorAlloc(allocator, allocSize);
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
  (void)BufReserveWithAllocator(&res, len + 1, allocator);
  memcpy(res, p, len);
  res[len] = 0;
  return res;
}

char* _BufStrDup(char* s, Allocator const* allocator) {
  return _BufStrDupn(s, strlen(s), allocator);
}

void* _BufToProto(void* b, size_t *pn, Allocator const* allocator) {
  if (!BufLen(b)) return 0;
  char* p = b;
  void** res = 0;
  struct BufHdr* hdr = BufHdr(b);
  *pn = hdr->len;
  (void)BufReserveWithAllocator(&res, hdr->len, allocator);
  BufEachi(b, i) {
    res[i] = p + i * hdr->elementSize;
  }
  return res;
}

void BufIndexBit(intmax_t* sourceBuf, intmax_t* indices, intmax_t** presultBuf) {
  size_t elements = ArrayBitElements(*presultBuf, BufLen(indices));
  (void)BufReserve(presultBuf, elements);
  BufEachi(indices, i) {
    if (ArrayBit(sourceBuf, indices[i])) {
      ArrayBitSet(*presultBuf, i);
    } else {
      ArrayBitClear(*presultBuf, i);
    }
  }
}

intmax_t* BufCombos(intmax_t* ranges, size_t n) {
  intmax_t* thisRange = 0;
  Range(ranges[0], ranges[1], i) {
    *BufAlloc(&thisRange) = i;
  }
  if (n <= 1) {
    return thisRange;
  }
  intmax_t* res = 0;
  intmax_t* r = BufCombos(ranges + 2, n - 1);
  BufEach(intmax_t, thisRange, y) {
    BufEach(intmax_t, r, x) {
      *BufAlloc(&res) = *y;
      RangeBefore(n - 1, i) {
        *BufAlloc(&res) = x[i];
      }
      x += n - 2;
    }
  }
  BufFree(&thisRange);
  BufFree(&r);
  return res;
}

//
// Statistics
//

intmax_t ProbToOneIn(double p) {
  if (p <= 0) return 0;
  return round(1 / p);
}

intmax_t ProbToGeoDistrQuantileDingle(double p, double percent) {
  if (p <= 0) return 0;
  return round(log(1 - percent / 100) / log(1 - p));
}

//
// Humanize
//

static
int HumanizeSnprintf(intmax_t x, char* buf, size_t sz, char const* suff) {
  return snprintf(buf, sz, "%jd%s ", x, suff);
}

static
int HumanizeSnprintfWithDot(double x, char* buf, size_t sz, char const* suff) {
  intmax_t mod = (intmax_t)(x * 10) % 10;
  if (mod) {
    return snprintf(buf, sz, "%jd.%jd%s ", (intmax_t)x, mod, suff);
  }
  return HumanizeSnprintf((intmax_t)x, buf, sz, suff);
}

static
int HumanizeStepWithDot(intmax_t mag, char const* suff, char* buf, size_t sz, int value) {
  if (value >= mag) {
    double x = value / (double)mag;
    int n = HumanizeSnprintfWithDot(x, 0, 0, suff);
    if (n >= sz) {
      snprintf(buf, sz, "...");
    } else {
      HumanizeSnprintfWithDot(x, buf, sz, suff);
    }
    return n;
  }
  return 0;
}

void Humanize(char* buf, size_t sz, intmax_t value) {
  const intmax_t k = 1000;
  const intmax_t m = k * k;
  const intmax_t b = k * m;

  if (value < 0) {
    int n = snprintf(buf, sz, "-");
    buf += n;
    sz -= n;
    value *= -1;
  }

  if (value < k) {
    snprintf(buf, sz, "%jd", value);
    return;
  }

  HumanizeStepWithDot(b, "b", buf, sz, value) ||
  HumanizeStepWithDot(m, "m", buf, sz, value) ||
  HumanizeStepWithDot(k, "k", buf, sz, value);
}

//
// Memory arena
//

struct _Arena {
  Allocator const* allocator;
  char** chunks;
  size_t align;
};

Arena* _ArenaInit(Allocator const* allocator) {
  Arena* a = AllocatorAlloc(allocator, sizeof(Arena));
  a->allocator = allocator;
  a->chunks = 0;
  a->align = 4096;
  return a;
}

void* ArenaAlloc(Arena* a, size_t x) {
  // look for empty space in existing chunks
  BufEach(char*, a->chunks, chunk) {
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
  char** pchunk = BufAllocZero(&a->chunks);
  (void)BufReserveWithAllocator(pchunk, x, a->allocator);
  return *pchunk;
}

void ArenaFree(Arena* a) {
  if (a) {
    BufEach(char*, a->chunks, chunk) {
      BufFree(chunk);
    }
    BufFree(&a->chunks);
  }
  AllocatorTryFree(a->allocator, a);
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
// Map
//

struct _Map {
  Arena* arena;
  size_t cap, len;
  int* present; // bitmask. so that we can have zero keys/values that count as present
  int* keys;
  void** values;
};

static Map* MapRealloc(Allocator const* allocator, Map* m, size_t newCap) {
  Map* newMap = AllocatorAlloc(allocator, sizeof(Map));
  Arena* arena = _ArenaInit(allocator);
  memset(newMap, 0, sizeof(*newMap));
  newMap->arena = arena;
  newMap->cap = newCap;
  newMap->keys = ArenaAlloc(arena, ArrayElementSize(newMap->keys) * newCap);
  newMap->values = ArenaAlloc(arena, ArrayElementSize(newMap->values) * newCap);
  size_t allocSize = ArrayBitSize(newMap->present, newCap);
  newMap->present = ArenaAlloc(arena, allocSize);
  memset(newMap->present, 0, allocSize);
  if (m) {
    RangeBefore(m->cap, i) {
      if (ArrayBit(m->present, i)) {
        MapSet(newMap, m->keys[i], m->values[i]);
      }
    }
  }
  return newMap;
}

#define MAP_BASE_CAP 16

Map* _MapInitCap(Allocator const* allocator, size_t cap) {
  cap = RoundUp2(cap);
  cap = Max(MAP_BASE_CAP, cap);
  return MapRealloc(allocator, 0, cap);
}

Map* _MapInit(Allocator const* allocator) {
  return _MapInitCap(allocator, MAP_BASE_CAP);
}

void MapFree(Map* m) {
  Allocator const* allocator = m->arena->allocator;
  ArenaFree(m->arena);
  AllocatorFree(allocator, m);
}

static size_t MapNextIndex(size_t cap, size_t i) {
  return (i + 1) & (cap - 1); // cap should always be a power of two so we AND instead of %
}

#define MapScanEx(hash) \
  for (size_t i = MapNextIndex(m->cap, hash), j = 0; j < m->cap; ++j, i = MapNextIndex(m->cap, i))

#define MapScan MapScanEx(HashInt(key))

int* _MapKeys(Map* m, Allocator const* allocator) {
  int* res = 0;
  _BufAlloc(&res, 0, ArrayElementSize(res), allocator);
  RangeBefore(m->cap, i) {
    if (ArrayBit(m->present, i)) {
      *BufAlloc(&res) = m->keys[i];
    }
  }
  return res;
}

static void** MapGetRef(Map* m, int key) {
  MapScan {
    if (ArrayBit(m->present, i) && m->keys[i] == key) {
      return &m->values[i];
    }
  }
  return 0;
}

int MapSet(Map* m, int key, void* value) {
  if (m->len * 2 >= m->cap) {
    // if the map is getting too full, reallocate it with twice the capacity
    Map* newMap = MapRealloc(m->arena->allocator, m, m->cap * 2);
    ArenaFree(m->arena);
    *m = *newMap;
    AllocatorFree(newMap->arena->allocator, newMap);
  }

  // first, just see if the key is already there
  // we have to do this in case we remove a key that used to collide with this key
  void** ref = MapGetRef(m, key);
  if (ref) {
    *ref = value;
    return 1;
  }

  // key is missing
  // start scanning from MapNextIndex(hash), keep scanning forward (with wrap) until no more
  // collisions
  MapScan {
    if (!ArrayBit(m->present, i)) {
      m->keys[i] = key;
      m->values[i] = value;
      ArrayBitSet(m->present, i);
      ++m->len;
      return 1;
    }
  }

  return 0;
}

int MapHas(Map* m, int key) {
  return MapGetRef(m, key) != 0;
}

void* MapGet(Map* m, int key) {
  void** ref = MapGetRef(m, key);
  if (ref) {
    return *ref;
  }
  return 0;
}

void MapDel(Map* m, int key) {
  void** ref = MapGetRef(m, key);
  if (ref) {
    size_t i = ref - m->values;
    ArrayBitClear(m->present, i);
    --m->len;
  }
}


//
// Math
//

// https://stackoverflow.com/questions/11376288/fast-computing-of-log2-for-64-bit-integers
// https://graphics.stanford.edu/~seander/bithacks.html

int Log2i(int n) {
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
  return tab32[(n * 0x07C4ACDD) >> 27];
}

size_t RoundUp2(size_t v) {
  --v;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
#if SIZE_MAX > USHORT_MAX
  v |= v >> 16;
#endif
#if SIZE_MAX > UINT_MAX
  v |= v >> 32;
#endif
  return ++v;
}

size_t BitCount(void* data, size_t bytes) {
  size_t res = 0;
  for (unsigned char* p = data; bytes--; ) {
    for (unsigned char c = *p++; c; c >>= 1) {
      res += c & 1;
    }
  }
  return res;
}

unsigned HashInt(unsigned x) {
  x *= 0x85ebca6b;
  x ^= x >> 16;
  return x;
}

//
// Align
//

struct _Align {
  Allocator const* allocator;
  size_t maxlen;
  size_t* lens;
  char** ss;
};

Align* _AlignInit(Allocator const* allocator) {
  Align* a = AllocatorAlloc(allocator, sizeof(Align));
  MemZero(a);
  a->allocator = allocator;
  return a;
}

#undef allocatorDefault
#define allocatorDefault (*al->allocator)
void _AlignFeed(Align* al, char* alignFmt, char* fmt, ...) {
  va_list va;
  va_start(va, fmt);
  size_t len = vsnprintf(0, 0, alignFmt, va);
  va_end(va);
  al->maxlen = Max(len, al->maxlen);
  *BufAlloc(&al->lens) = len;
  va_start(va, fmt);
  BufAllocVStrf(&al->ss, fmt, va);
  va_end(va);
}

void AlignPrint(Align* al, FILE* f) {
  BufEachi(al->ss, i) {
    Repeat(al->maxlen - al->lens[i]) putc(' ', f);
    fputs(al->ss[i], f);
    putc('\n', f);
  }
}

void AlignFree(Align* al) {
  BufFreeClear((void**)al->ss);
  BufFree(&al->ss);
  BufFree(&al->lens);
  AllocatorFree(al->allocator, al);
}
#undef allocatorDefault
#define allocatorDefault allocatorDefault_

#endif
