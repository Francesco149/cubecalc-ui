struct BufHdr {
  size_t len, cap, elementSize;
  char data[0];
};

#define BufHdr(b) ((struct BufHdr*)(b) - 1)

size_t BufLen(void* b) {
  return b ? BufHdr(b)->len : 0;
}

void BufDel(void* b, int i) {
  struct BufHdr* hdr = BufHdr(b);
  --hdr->len;
  memmove(&hdr->data[i * hdr->elementSize],
          &hdr->data[(i + 1) * hdr->elementSize],
          hdr->elementSize * hdr->len);
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

void _BufAlloc(void* p, size_t elementSize) {
  size_t cap = 4, len = 0;
  struct BufHdr* hdr = 0;
  void **b = p;
  if (*b) {
    hdr = BufHdr(*b);
    cap = hdr->cap;
    len = hdr->len;
  }
  if (!hdr || len >= cap) {
    cap *= 2;
    hdr = realloc(hdr, sizeof(struct BufHdr) + elementSize * cap);
    hdr->cap = cap;
    hdr->elementSize = elementSize;
    *b = hdr + 1;
  }
  hdr->len = len + 1;
}

#define BufAlloc(b) \
  (_BufAlloc((b), sizeof((*(b))[0])), (&(*(b))[BufLen(*(b)) - 1]))

#define Pack16to32(hi, lo) (((hi) << 16) & 0xFFFF0000 | ((lo) & 0x0000FFFF))
#define LoWord(dw) ((dw) & 0x0000FFFF)
#define HiWord(dw) (((dw) >> 16) & 0x0000FFFF)
