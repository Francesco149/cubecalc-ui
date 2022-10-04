#ifndef CUBECALC_H
#define CUBECALC_H

#include "utils.c"

// NOTE: CubeGlobalInit MUST be called before calling anything else from this header
// other functions are thread safe, but GlobalInit/GlobalFree must be called once and not
// concurrently
void CubeGlobalInit();
void CubeGlobalFree();

// lines or line combinations as columns. each column is a Buf
typedef struct _Lines {
  int* lineHi;
  int* lineLo;
  float* onein;
  int* value;
  intmax_t* prime;
  size_t comboSize;
} Lines;

void LinesFree(Lines* l);

// format line into a string such as MESO_ONLY | DROP_ONLY
char* LineToStr(int hi, int lo);

// these return a Buf that you need to free
#define CubeToStr(x) CubeToStrSep(" | ", x)
#define CubeToStrSep(sep, x) \
  _BitEnumToStr(sep, cubeValues, cubeNames, ArrayLength(cubeNames), x)
#define CategoryToStr(x) CategoryToStrSep(" | ", x)
#define CategoryToStrSep(sep, x) \
  _BitEnumToStr(sep, categoryValues, categoryNames, ArrayLength(categoryNames), x)

// these return a const char pointer, no need to free
#define TierToStr(x) _EnumToStr(tierValues, tierNames, ArrayLength(tierNames), x)

// internally used by macros
char* _BitEnumToStr(char const* s, int const* vals, char const* const* names, size_t n, int v);
char const* _EnumToStr(int const* vals, char const* const* names, size_t n, int v);

// deep copy lines. dst struct should be initialized to zero
void LinesDup(Lines* dst, Lines const* src);

// filter lines. mask is a bitmask of the element that should be kept (tip: use BufMask).
// this re-allocates all the b
void LinesFilt(Lines* l, intmax_t* mask);

// replace each index in indices with the corresponding element from each column. these are the
// new column Buf's after this operation.
//
// this is mainly used to go from a list of all possible lines to a list of all possible
// combinations of N lines, using BufCombos
//
// example:
//   if a column is [a, b, c], calling LinesIndex with [0, 0, 0, 1, 1, 1, 2, 2, 2] will replace
//   that column with [a, a, a, b, b, b, c, c, c].
//   this indexing operation is applied identically to all columns
//
void LinesIndex(Lines* l, intmax_t* indices);

// helper macros to define the wantBuf for CubeCalc
#define _WantStatLine(line) .lineLo = line##_LO, .lineHi = line##_HI
#define WantStat(line, val) (Want){ .type = WANT_STAT, _WantStatLine(line), .value = val }
#define Line(x) x##_LO, x##_HI
#define WantOp(opname, n) (Want){ .type = WANT_OP, .op = WANT_##opname, .opCount = (n) }

// enums used by CubeCalc
// values can be found at the top of generated.c
typedef struct _Want Want;
typedef enum Category Category;
typedef enum Cube Cube;
typedef enum Tier Tier;
typedef enum Region Region;

// calculate probability of rolling any line combination matching wantBuf
//
// - lvl: the level of the item
// - outCombos: if non-NULL, this pointer will be set to the matching combos data.
//              you are expected to free this yourself using LinesFree
//
// wantBuf defines the combination of stats we're looking for. it's a simple stack machine
// expression language where each element either pushes an operand on the stack or calls an
// operator. the operator result is also pushed on the stack
//
// it must contain at least one operator. if the operand number is -1, it will operate
// on all remaining operands on the stack.
//
// after evaluating wantBuf, the stack must only contain 1 element, the result. any other
// outcome is considered an error
//
// example: 33+ att
//
//  Buf(Want, want,
//    WantStat(ATT, 33),
//    WantOp(AND, -1),
//  );
//
// example:
// // (((20+ meso or 20+ drop)
// //    and 10+ stat)
// //  or 23+ stat)
//
//  Buf(Want, want,
//    WantStat(MESO, 20), // stack(1): [20_meso                                                 ]
//    WantStat(DROP, 20), // stack(2): [20_meso                                        , 20_drop]
//    WantOp(OR, 2),      // stack(1): [(20_meso or 20_drop)                                    ]
//    WantStat(STAT, 10), // stack(2): [(20_meso or 20_drop)                           , 10_stat]
//    WantOp(AND, 2),     // stack(1): [((20_meso or 20_drop) and 10_stat)                      ]
//    WantStat(STAT, 23), // stack(2): [((20_meso or 20_drop) and 10_stat)             , 23_stat]
//    WantOp(OR, 2),      // stack(1): [(((20_meso or 20_drop) and 10_stat) or 23_stat)         ]
//  );
//
// complete usage example (compile with `tcc -DCUBECALC_DEBUG main.c -lm` to see line combos):
//
//   #define CUBECALC_MONOLITH
//   #include "cubecalc.c"
//
//   #include <stdio.h>
//   #include <math.h>
//
//   int main() {
//     CubeGlobalInit();
//
//     static const BufH(Want, want,
//       WantStat(ATT, 33),
//       WantOp(AND, -1),
//     );
//
//     float p = CubeCalc(want.data, WEAPON, BONUS, LEGENDARY, 200, GMS, 0);
//     if (p > 0) {
//       printf("1 in %.0f\n", round(1 / p));
//     } else {
//       puts("impossible");
//     }
//
//     CubeGlobalFree();
//     return 0;
//   }
//
float CubeCalc(
  Want const* wantBuf,
  Category category,
  Cube cube,
  Tier tier,
  int lvl,
  Region region,
  Lines* outCombos
);

// structs and enums used for wantBuf. usually you don't need to use these directly
#define WantOps(f) \
  f(NULLOP) \
  f(AND) \
  f(OR) \

#define WantTypes(f) \
  f(NULLTYPE) \
  f(STAT) \
  f(OP) \
  f(MASK) \

#define WantPrefix(x) WANT_##x

#undef PREFIX
#define PREFIX(x) WantPrefix(x)
DefEnumHdr(WantOp);
DefEnumHdr(WantType);
#undef PREFIX
#define PREFIX(x) DEF_PREFIX(x)

typedef struct _Want {
  WantType type;
  union {
    struct {
      int lineLo;
      int lineHi;
      int value;
    };
    struct {
      WantOp op;
      int opCount;
    };
    intmax_t* mask;
  };
} Want;

#endif

#ifdef CUBECALC_MONOLITH
#define CUBECALC_IMPLEMENTATION
#define CUBECALC_GENERATED_IMPLEMENTATION
#define CUBECALC_COMMON_IMPLEMENTATION
#define UTILS_IMPLEMENTATION
#include "common.c"
#include "utils.c"
#endif

#if defined(CUBECALC_IMPLEMENTATION) && !defined(CUBECALC_UNIT)
#define CUBECALC_UNIT

#undef PREFIX
#define PREFIX(x) WantPrefix(x)
DefEnumNames(WantOp);
DefEnumNames(WantType);
#undef PREFIX
#define PREFIX(x) DEF_PREFIX(x)

#include "generated.c"

#ifdef CUBECALC_DEBUG
#include "debug.c"
#endif

#include <string.h>

char* LineToStr(int hi, int lo) {
  char* res = 0;
  size_t nflags = 0;
  ArrayEachiRange(allLinesHi, ALL_LINES_NUM_MASKS, -1, i) {
    if ((hi & allLinesHi[i]) || (lo & allLinesLo[i])) {
      BufAllocCharsf(&res, "%s | ", allLineNames[i]);
      ++nflags;
    }
  }
  // remove trailing separator
  if (nflags) {
    BufHdr(res)->len -= 3; // 2 chars + null char
    BufAt(res, -1) = 0;
  }
  return res;
}

char* _BitEnumToStr(char const* s, int const* vals, char const* const* names, size_t n, int v) {
  // TODO: DRY this with the LineToStr code
  char* res = 0;
  size_t nflags = 0;
  RangeBefore(n, i) {
    if ((v & vals[i]) == vals[i]) {
      BufAllocCharsf(&res, "%s%s", names[i], s);
      v &= ~vals[i];
      ++nflags;
    }
  }
  if (nflags) {
    BufHdr(res)->len -= strlen(s);
    BufAt(res, -1) = 0;
  }
  return res;
}

char const* _EnumToStr(int const* vals, char const* const* names, size_t n, int v) {
  // TODO: binary search or hashmap or something
  RangeBefore(n, i) {
    if (vals[i] == v) {
      return names[i];
    }
  }
  return 0;
}

static
Map* DataFindMap(int cubeMask) {
  if (cubeMask & FAMILIAR) {
    return fams;
  }
  if (cubeMask & RED_FAM_CARD) {
    return famsCard;
  }
  if (cubeMask & (VIOLET | EQUALITY | UNI)) {
    return tms;
  }
  return kms;
}

static
LineData const* DataFind(int categoryMask, int cubeMask, int tier) {
  LineData const* res = 0;
  Map* data = DataFindMap(cubeMask);
  int* cubes = MapKeys(data);
  BufEach(int, cubes, cube) {
    if (*cube & cubeMask) {
      Map* categoryMap = MapGet(data, *cube);
      int* categories = MapKeys(categoryMap);
      BufEach(int, categories, category) {
        if (*category & categoryMask) {
          Map* tiers = MapGet(categoryMap, *category);
          if (MapHas(tiers, tier)) {
            res = MapGet(tiers, tier);
            break;
          }
        }
      }
      BufFree(&categories);
      if (res) {
        break;
      }
    }
  }
  BufFree(&cubes);
  return res;
}

typedef union _LineFields {
  Lines data;
  int* fields[offsetof(Lines, prime) / sizeof(void*)];
  int* allFields[offsetof(Lines, comboSize) / sizeof(void*)];
} LineFields;

#define F(x) ((LineFields*)(x))

void LinesFree(Lines* l) {
  ArrayEach(int*, F(l)->allFields, x) {
    BufFree(x);
  }
}

void LinesDup(Lines* dst, Lines const* src) {
  ArrayEachi(F(dst)->allFields, i) {
    F(dst)->allFields[i] = BufDup(F(src)->allFields[i]);
  }
}

static
size_t LinesNumPrimes(Lines* l) {
  return BitCount(l->prime, ArrayElementSize(l->prime) * BufLen(l->prime));
}

void LinesFilt(Lines* l, intmax_t* mask) {
  size_t j = 0;
  BufEachi(l->lineHi, i) {
    if (ArrayBit(mask, i)) {
      ArrayEachi(F(l)->fields, k) {
        // NOTE: this will not work on platforms where float is not representable as a 32-bit int
        // because we are casting onein to an int array
        F(l)->fields[k][j] = F(l)->fields[k][i];
      }
      if (ArrayBit(l->prime, i)) {
        ArrayBitSet(l->prime, j);
      } else {
        ArrayBitClear(l->prime, j);
      }
      ++j;
    }
  }
  ArrayEach(int*, F(l)->fields, x) {
    BufHdr(*x)->len = j;
  }
  BufHdr(l->prime)->len = ArrayBitElements(l->prime, j);

  // important: fill the rest of the bit mask with zeros so they dont get counted by NumPrimes
  for (; j % ArrayElementBitSize(l->prime); ++j) {
    ArrayBitClear(l->prime, j);
  }
}

static
void BufIndexFreeInt(int** buf, intmax_t* indices) {
  int* result = 0;
  BufIndex(*buf, indices, &result);
  BufFree(buf);
  *buf = result;
}

void LinesIndex(Lines* l, intmax_t* indices) {
  ArrayEach(int*, F(l)->fields, x) {
    BufIndexFreeInt(x, indices);
  }
  intmax_t* result = 0;
  BufIndexBit(l->prime, indices, &result);
  BufFree(&l->prime);
  l->prime = result;
}

#undef F

static
int LinesCatData(Lines* l, LineData const* ld, size_t group, int tier) {
  Map* hi = MapGet(valueGroups[group], tier);
  if (!hi) {
    fprintf(stderr, "no data for tier %d\n", tier);
    return 0;
  }
  if (ld) {
    size_t start = BufLen(l->lineHi);
    BufCat(&l->lineHi, ld->lineHi);
    BufCat(&l->lineLo, ld->lineLo);
    BufCat(&l->onein, ld->onein);

    // ANY line
    *BufAlloc(&l->lineHi) = ANY_HI;
    *BufAlloc(&l->lineLo) = ANY_LO;
    *BufAlloc(&l->onein) = 1;

    BufEachiRange(l->lineHi, start, -1, i) {
      int lineHi = l->lineHi[i];
      int lineLo = l->lineLo[i];
      Map* lo = MapGet(hi, lineHi);
      if (!lo || !MapHas(lo, lineLo)) {
        char* s = LineToStr(lineHi, lineLo);
        fprintf(stderr, "no value for %s\n", s);
        BufFree(&s);
        return 0;
      }
      // int value = valueGroups[group][tier][lineHi][lineLo]
      *BufAlloc(&l->value) = (int)(intptr_t)MapGet(lo, lineLo);
    }
  }
  return 1;
}

static
int LinesInit(Lines* l, LineData const* dataPrime, LineData const* dataNonPrime,
  size_t group, int tier)
{
  l->comboSize = 1;
  if (!LinesCatData(l, dataPrime, group, tier)) return 0;
  size_t numPrimes = BufLen(l->lineHi);
  if (numPrimes == 1) return 0; // no lines found
  if (!LinesCatData(l, dataNonPrime, group, tier - 1)) return 0;
  (void)BufReserve(&l->prime, ArrayBitElements(l->prime, BufLen(l->lineHi)));
  BufZero(l->prime);
  RangeBefore(numPrimes, i) {
    ArrayBitSet(l->prime, i);
  }
  return 1;
}

static
size_t ValueGroupFind(int cubeMask, int categoryMask, int regionMask, int level) {
  int minLevel = 301;
  size_t match = valueGroupsLen;
  RangeBefore(valueGroupsLen, i) {
    if ((valueGroupsCubeMask[i] & cubeMask) == cubeMask &&
        (valueGroupsCategoryMask[i] & categoryMask) == categoryMask &&
        (valueGroupsRegionMask[i] & regionMask) == regionMask &&
        (valueGroupsMaxLevel[i] >= level))
    {
      if (valueGroupsMaxLevel[i] < minLevel) {
        minLevel = valueGroupsMaxLevel[i];
        match = i;
      }
    }
  }
  if (match >= valueGroupsLen) {
    fprintf(stderr, "couldn't match cube 0x%x category 0x%x region 0x%x level %d\n",
      cubeMask, categoryMask, regionMask, level);
  }
  return match;
}

static
void WantFree(Want* w) {
  if (w->type == WANT_MASK) BufFree(&w->mask);
}

static
void WantStackPop(Want* stack, intmax_t n) {
  BufEachRange(Want, stack, -n, -1, w) {
    WantFree(w);
  }
  BufHdr(stack)->len -= n;
}

static
void WantStackFree(Want** pstack) {
  WantStackPop(*pstack, BufLen(*pstack));
  BufFree(pstack);
}

// NOTE: LINE_A/B/C should NEVER be used with this
static
void LinesMatch(Lines* l, int maskHi, int maskLo, intmax_t** pmatch, intmax_t** pmatchLo) {
  BufClear(*pmatch);
  BufClear(*pmatchLo);
  BufMask(int, l->lineHi, *x & maskHi, pmatch);
  BufMask(int, l->lineLo, *x & maskLo, pmatchLo);
  BufOR(*pmatch, *pmatchLo);
}

// match any combo that has at least numLines lines matching maskHi maskLo 
// result stored in *pmatch
static
void LinesAnyNCombo(
  Lines* combos,
  int numLines,
  int maskHi, int maskLo,
  intmax_t** pmatch, intmax_t** pmatchLo,
  int** pcounts
) {
  LinesMatch(combos, maskHi, maskLo, pmatch, pmatchLo);

  // create a Buf with repeated line counts for each combo
  BufClear(*pcounts);
  (void)BufReserve(pcounts, BufLen(combos->lineHi));
  BufEachi(combos->lineHi, i) {
    int count = 0;
    RangeBefore(combos->comboSize, j) {
      count += ArrayBitVal(*pmatch, i + j);
    }
    RangeBefore(combos->comboSize, j) {
      (*pcounts)[i + j] = count;
    }
    i += combos->comboSize - 1;
  }

  // mask by line count on the repeated Buf. this will keep combos matching the line count
  BufClear(*pmatch);
  BufMask(int, *pcounts, *x >= numLines, pmatch);
}

static
int WantEval(int category, int cube, Lines* combos, Want const* wantBuf) {
  Want* stack = 0;
  int res = 0;

  // filter all lines that don't match these stats
  int maskHi = ANY_HI;
  int maskLo = ANY_LO;
  BufEach(Want const, wantBuf, s) {
    if (s->type == WANT_STAT) {
      maskHi |= s->lineHi;
      maskLo |= s->lineLo;
    }
  }

  intmax_t* match = 0;
  intmax_t* matchLo = 0;
  int* counts = 0; // used for "any combination of N lines"

  LinesMatch(combos, maskHi, maskLo, &match, &matchLo);
  LinesFilt(combos, match);

  intmax_t numPrimes = LinesNumPrimes(combos);

  // convert "one in" to probability (onein = 1/onein)
  BufEach(float, combos->onein, x) {
    *x = 1 / *x;
  }

  // calculate prime ANY line chance
  float otherLinesChance = 0;
  if (numPrimes >= 2) {
    // only if there's at least 1 line other than the ANY prime line
    BufOpRange(+, combos->onein, 0, numPrimes - 2, &otherLinesChance);
  }
  combos->onein[numPrimes - 1] = 1 - otherLinesChance;

  // calculate non-prime ANY line chance
  otherLinesChance = 0;
  if (BufLen(combos->onein) - numPrimes >= 2) {
    // only if there's at least 1 line other than the ANY non prime line
    BufOpRange(+, combos->onein, numPrimes, -2, &otherLinesChance);
  }
  BufAt(combos->onein, -1) = 1 - otherLinesChance;

  // generate combinations (array of indices)

#define P 0, -2
#define N 0, -1
#define O -3, -1
// O is non-prime only

#define rangeIf(cond, ...) if (cond) { range(__VA_ARGS__); }
#define range(...) \
  static const Buf(intmax_t const, r, __VA_ARGS__); \
  ranges = BufDup((void*)r)

  intmax_t* ranges;

  rangeIf(cube & VIOLET, P, N, N, N, N, N)
  else rangeIf(cube & UNI, N)
  else rangeIf(cube & EQUALITY, P, P, P)
  else if (cube & (FAMILIAR | RED_FAM_CARD)) {
    // assuming no dbl primes on fam reveal (we don't know if this is accurate)
    rangeIf(cube & FAMILIAR, P, O)
    else {
      range(P, N);
    }
  } else {
    range(P, N, N);
  }

#undef range
#undef rangeIf

#undef P
#undef N
#undef O

  BufEach(intmax_t, ranges, x) {
    if (*x == -2) {
      // primes end
      *x = numPrimes - 1;
    } else if (*x == -3) {
      // non-primes start
      *x = numPrimes;
    } else {
      *x = BufI(combos->lineHi, *x);
    }
  }

  // convert list of lines to flattened array of all possible line combinations
  combos->comboSize = BufLen(ranges) / 2;
  intmax_t* tmpIntMax = BufCombos(ranges, combos->comboSize);
  LinesIndex(combos, tmpIntMax);
  BufFree(&ranges);

  // filter out impossible combos

  static const BufH(Want, forbiddenCombos,
    WantStat(DECENTS, 0), // 2+ lines of any decent impossible
    WantStat(INVIN, 0),   // 2+ lines of invincibility impossible
    WantStat(LINES, 2),

    // same as above but 3+ lines
    WantStat(BOSS, 0),
    WantStat(IED, 0),
    WantStat(DROP, 0),
    WantStat(LINES, 3),
  );

  // tmpIntMax stores the final mask
  BufClear(tmpIntMax);
  (void)BufReserve(&tmpIntMax, ArrayBitElements(tmpIntMax, BufLen(combos->lineHi)));
  BufZero(tmpIntMax);

  Want const* lastLines = forbiddenCombos.data;

  BufEach(Want const, forbiddenCombos.data, w) {
    switch (w->type) {
      case WANT_STAT:
        if ((w->lineHi & LINES_HI) || (w->lineLo & LINES_LO)) {
          for (Want const* s = lastLines; s != w; ++s) {
            LinesAnyNCombo(combos, w->value, s->lineHi, s->lineLo, &match, &matchLo, &counts);
            BufOR(tmpIntMax, match);
          }
          lastLines = w;
        }
        break;

      default:
        fprintf(stderr, "unexpected %s in forbidden lines buf\n", WantTypeNames[w->type]);
        goto cleanup;
    }
  }

  BufNOT(tmpIntMax);
  LinesFilt(combos, tmpIntMax);

  BufEach(Want const, wantBuf, w) {
    switch (w->type) {
      case WANT_STAT:
        *BufAlloc(&stack) = *w;
        break;
      case WANT_OP: {
        intmax_t* result = 0;
        int opCount = w->opCount >= 0 ? w->opCount : BufLen(stack);

        // check if we're looking for "any combination of N lines". also check for masks since
        // this operation can only work on stats
        int numLines = 0;
        size_t masks = 0;
        maskHi = maskLo = 0;
        BufEachRange(Want, stack, -opCount, -1, s) {
          switch (s->type) {
            case WANT_STAT:
              if ((s->lineHi & LINES_HI) || (s->lineLo & LINES_LO)) {
                numLines = s->value;
              } else {
                // create a mask of all the stats, minus LINES (only used by "any combination")
                maskHi |= s->lineHi;
                maskLo |= s->lineLo;
              }
              break;
            case WANT_MASK:
              ++masks;
              break;
            default:
              break;
          }
        }

        if (numLines) {
          // any N lines combination with ANY of these lines
          if (masks) {
            fprintf(stderr, "got LINES=%d with %d operands but there are %zu masks on the stack",
              numLines, opCount, masks);
            goto cleanup;
          }

          LinesAnyNCombo(combos, numLines, maskHi, maskLo, &match, &matchLo, &counts);
          result = BufDup(match);
        }

        // regular operator, mask lines for every stat/mask on the stack
        else BufEachRange(Want, stack, -opCount, -1, s) {
          switch (s->type) {
            case WANT_STAT: {
              // make a mask of lines that match the stat
              LinesMatch(combos, s->lineHi, s->lineLo, &match, &matchLo);

              // multiply all the values by the mask (non matching values will be 0)
              int* relevantValues = BufDup(combos->value);
              BufEachi(relevantValues, i) {
                relevantValues[i] *= ArrayBitVal(match, i);
              }

              // sum every N elements (repeat sum N times in the array to match size)
              BufEachi(relevantValues, i) {
                int sum = 0;
                BufOpRange(+, relevantValues, i, i + combos->comboSize - 1, &sum);
                RangeBefore(combos->comboSize, j) {
                  relevantValues[i + j] = sum;
                }
                i += combos->comboSize - 1;
              }

              // make mask of elements in this sum array that are >= desired value
              BufClear(match);
              BufMask(int, relevantValues, *x >= s->value, &match);
              BufFree(&relevantValues);

              // remember to copy match as it gets reused
              s->type = WANT_MASK;
              s->mask = BufDup(match);

              // fall through to the MASK case
            }
            case WANT_MASK:
              if (!result) {
                result = s->mask;
                s->mask = 0; // prevent result from being freed
                break;
              }
#define c(x) case WANT_##x: Buf##x(result, s->mask); break
              switch (w->op) {
                c(AND);
                c(OR);
                default:
                  fprintf(stderr, "unsupported operator %s\n", WantOpNames[w->op]);
                  goto cleanup;
              }
#undef c
              break;
            default:
              fprintf(stderr, "unsupported operand %s\n", WantTypeNames[s->type]);
              goto cleanup;
          }
        }
        WantStackPop(stack, opCount);
        *BufAlloc(&stack) = (Want){
          .type = WANT_MASK,
          .mask = result,
        };
        break;
      }
      default:
        fprintf(stderr, "%s is only for internal use\n", WantTypeNames[w->type]);
        goto cleanup;
    }
  }

  if (BufLen(stack) > 1) {
    fprintf(stderr, "%zu values on the stack, expected 1\n", BufLen(stack));
#ifdef CUBECALC_DEBUG
    puts("");
    puts("# final stack");
    WantPrint(stack);
    puts("");
#endif
    goto cleanup;
  }

  int typ = stack[0].type;
  if (typ != WANT_MASK) {
    fprintf(stderr, "expected WANT_MASK result, got %s\n", WantTypeNames[typ]);
    goto cleanup;
  }

  if (!stack[0].mask) {
    fprintf(stderr, "NULL line filter mask\n");
    goto cleanup;
  }

  res = 1;

  LinesFilt(combos, stack[0].mask);

cleanup:
  BufFree(&tmpIntMax);
  BufFree(&match);
  BufFree(&matchLo);
  BufFree(&counts);
  WantStackFree(&stack);
  return res;
}


float CubeCalc(
  Want const* wantBuf,
  Category category,
  Cube cube,
  Tier tier,
  int lvl,
  Region region,
  Lines* outCombos
) {
  float res = 0;

#ifdef CUBECALC_DEBUG
  puts("");
  puts("# want");
  WantPrint(wantBuf);
#endif

  LineData const* dataPrime = DataFind(category, cube, tier);
  if (!dataPrime) {
    fprintf(stderr, "prime line data not found\n");
    goto cleanup;
  }

  LineData const* dataNonPrime = DataFind(category, cube, tier - 1);
  if (!dataNonPrime) {
    fprintf(stderr, "non-prime line data not found\n");
    goto cleanup;
  }

  size_t group = ValueGroupFind(cube, category, region, lvl);
  if (group >= valueGroupsLen || !valueGroups[group]) {
    fprintf(stderr, "failed to find value group\n");
    return 0;
  }

  Lines combos = {0};
  if (!LinesInit(&combos, dataPrime, dataNonPrime, group, tier)) {
    goto cleanup;
  }

#ifdef CUBECALC_DEBUG
  size_t numPrimes = LinesNumPrimes(&combos);
  puts("");
  puts("# prime");
  DataPrint(dataPrime, tier, combos.value);
  puts("");
  puts("# nonprime");
  DataPrint(dataNonPrime, tier - 1, combos.value + numPrimes);
#endif

  if (!WantEval(category, cube, &combos, wantBuf)) {
    goto cleanup;
  }

  float const* primeChanceData;
  Container* primeChance = MapGet(primeChances, cube);
  switch (primeChance->type) {
    case CONTAINER_BUF: {
      primeChanceData = primeChance->data;
      break;
    }
    case CONTAINER_MAP: {
      primeChanceData = MapGet(primeChance->data, tier);
      break;
    }
    default:
      fprintf(stderr, "invalid prime chance container %d\n", primeChance->type);
      goto cleanup;
  }

  {
    size_t len = BufLen(primeChanceData);
    if (len != combos.comboSize) {
      fprintf(stderr, "expected %zu prime chances, got %zu\n", combos.comboSize, len);
      goto cleanup;
    }
  }

  // multiply line probabilities by prime chance
  // to make it branchless, we prepend the non-prime chances and then we mul the index by prime bit
  // example:
  //   multipliers = [1 - primeChance1, 1 - primeChance2, 1 - primeChance3,
  //                      primeChance1,     primeChance2,     primeChance3]
  //   index = (line % 3) + IsPrime * 3

  float* primeChanceBuf = 0;

  {
    float* primeMul = 0;
    (void)BufReserve(&primeMul, combos.comboSize * 2);
    RangeBefore(combos.comboSize, i) {
      primeMul[i] = 1 - primeChanceData[i];
      primeMul[i + combos.comboSize] = primeChanceData[i];
    }

    size_t numLines = BufLen(combos.lineHi);
    (void)BufReserve(&primeChanceBuf, numLines);
    RangeBefore(numLines, i) {
      size_t idx = (i % combos.comboSize) + ArrayBitVal(combos.prime, i) * combos.comboSize;
      primeChanceBuf[i] = primeMul[idx];
    }

    BufFree(&primeMul);
  }

  BufOp2(*, combos.onein, primeChanceBuf);
  BufFree(&primeChanceBuf);

#ifdef CUBECALC_DEBUG
  puts("");
  puts("# combos");
#ifdef CUBECALC_PRINTCOMBOS
  LinesPrint(&combos);
#endif
  printf("%zu total combos\n", BufLen(combos.lineHi) / combos.comboSize);
#endif

  // calculate combo probabilities
  float* comboProbs = 0;
  float multiplier = cube == UNI ? 1 / 3.0 : 1;
  // ^ on unicubes, you spend an average of 3 cubes to select the line and roll it once
  BufEach(float, combos.onein, x) {
    float comboProb = 1;
    OpRange(*, x, 0, combos.comboSize - 1, &comboProb);
    *BufAlloc(&comboProbs) = comboProb * multiplier;
    x += combos.comboSize - 1;
  }

  // calculate probability to roll any of the combos
  BufOp(+, comboProbs, &res);
  BufFree(&comboProbs);

cleanup:
  if (outCombos) {
    *outCombos = combos;
  } else {
    LinesFree(&combos);
  }

  return res;
}

void CubeGlobalInit() {
  cubecalcGeneratedGlobalInit();
}

void CubeGlobalFree() {
  cubecalcGeneratedGlobalFree();
}

#endif
