#include <math.h>

struct BufHdr {
  size_t len, cap, elementSize;
  char data[0];
};

#define BufHdr(b) ((struct BufHdr*)(b) - 1)

size_t BufLen(void* b) {
  return b ? BufHdr(b)->len : 0;
}

void BufDel(void* b, int i) {
  if (b) {
    struct BufHdr* hdr = BufHdr(b);
    --hdr->len;
    memmove(&hdr->data[i * hdr->elementSize],
            &hdr->data[(i + 1) * hdr->elementSize],
            hdr->elementSize * (hdr->len - i));
  }
}

int BufDelFindInt(int* b, int value) {
  int i;
  for (i = 0; i < BufLen(b); ++i) {
    if (b[i] == value) {
      break;
    }
  }
  if (i < BufLen(b)) {
    BufDel(b, i);
  }
  return i;
}

void BufClear(void* b) {
  if (b) {
    BufHdr(b)->len = 0;
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

#define Pack16to32(hi, lo) (((hi) << 16) & 0xFFFF0000 | ((lo) & 0x0000FFFF))
#define LoWord(dw) ((dw) & 0x0000FFFF)
#define HiWord(dw) (((dw) >> 16) & 0x0000FFFF)

int ProbToOneIn(float p) {
  if (p <= 0) return 0;
  return roundf(1 / p + 0.5);
}

// examples:
// percent=75 returns the num of attempts to have 75% chance for an event of probability p to occur
// percent=50 is the median
int ProbToGeoDistrQuantileDingle(float p, float percent) {
  if (p <= 0) return 0;
  return roundf(logf(1 - percent / 100) / logf(1 - p));
}
