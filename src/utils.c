struct BufHdr {
  size_t len, cap;
};

#define BufHdr(b) ((struct BufHdr*)(b) - 1)
#define BufLen(b) BufHdr(b)->len

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
    *b = hdr + 1;
  }
  hdr->len = len + 1;
}

#define BufAlloc(b) \
  (_BufAlloc((b), sizeof((*(b))[0])), (&(*(b))[BufLen(*(b)) - 1]))

#define Pack16to32(hi, lo) (((hi) << 16) & 0xFFFF0000 | ((lo) & 0x0000FFFF))
#define LoWord(dw) ((dw) & 0x0000FFFF)
#define HiWord(dw) (((dw) >> 16) & 0x0000FFFF)
