#ifndef CUBECALC_COMMON_H
#define CUBECALC_COMMON_H

#include <stddef.h>

typedef enum _ContainerType {
  CONTAINER_NULL,
  CONTAINER_BUF,
  CONTAINER_MAP,
} ContainerType;

typedef struct _Container {
  ContainerType type;
  void* data;
} Container;

typedef struct _LineData {
  int const* lineHi;
  int const* lineLo;
  float const* onein;
} LineData;

void primeChancesFree();
void kmsFree();
void tmsFree();
void famsFree();
void famsCardFree();
void valueGroupsFree();

// i is the index into valueGroups
void valueGroupsSet(size_t i, int tier,
    size_t count, int const* linesHi, int const* linesLo, int const* vals);

size_t valueGroupFind(int cubeMask, int categoryMask, int regionMask, int maxLevel);

extern const size_t valueGroupsLen;

#endif

#if defined(CUBECALC_COMMON_IMPLEMENTATION) && !defined(CUBECALC_COMMON_UNIT)
#define CUBECALC_COMMON_UNIT

#include "generated.c"
#include "utils.c"
#include <stdio.h>

const size_t valueGroupsLen = ArrayLength(valueGroupsCubeMask);

void primeChancesFree() {
  int* keys = MapKeys(primeChances);
  BufEach(int, keys, k) {
    Container* c = MapGet(primeChances, *k);
    switch (c->type) {
      case CONTAINER_BUF: break;
      case CONTAINER_MAP: MapFree(c->data); break;
      default:
        fprintf(stderr, "unexpected container type %d (%x) at %p, key %x\n",
            c->type, c->type, c, *k);
        *(int*)1 = 0;
    }
  }
  MapFree(primeChances);
  primeChances = 0;
  BufFree(&keys);
}

static void linesFree(Map* cubeData) {
  int* cubes = MapKeys(cubeData);
  BufEach(int, cubes, cubeMask) {
    Map* categoryData = MapGet(cubeData, *cubeMask);
    int* categories = MapKeys(categoryData);
    BufEach(int, categories, categoryMask) {
      Map* tierData = MapGet(categoryData, *categoryMask);
      MapFree(tierData);
    }
    BufFree(&categories);
  }
  BufFree(&cubes);
}

void kmsFree() { linesFree(kms); }
void tmsFree() { linesFree(tms); }
void famsFree() { linesFree(fams); }
void famsCardFree() { linesFree(famsCard); }

void valueGroupsFree() {
  ArrayEachi(valueGroups, i) {
    Map* tiers = valueGroups[i];
    if (!tiers) continue;
    int* tierKeys = MapKeys(tiers);
    BufEach(int, tierKeys, tier) {
      Map* hi = MapGet(tiers, *tier);
      if (!hi) continue;
      int* hiKeys = MapKeys(hi);
      BufEach(int, hiKeys, lineHi) {
        MapFree(MapGet(hi, *lineHi));
      }
      MapFree(hi);
      BufFree(&hiKeys);
    }
    BufFree(&tierKeys);
  }
}

static Map* MapGetOrInit(Map* map, int key) {
  Map* m = MapGet(map, key);
  if (!m) {
    m = MapInit();
    MapSet(map, key, m);
  }
  return m;
}

void valueGroupsSet(size_t i, int tier,
    size_t count, int const* linesHi, int const* linesLo, int const* vals)
{
  if (!valueGroups[i]) {
    valueGroups[i] = MapInit();
  }
  Map* hi = MapGetOrInit(valueGroups[i], tier);
  RangeBefore(count, j) {
    Map* lo = MapGetOrInit(hi, linesHi[j]);
    MapSet(lo, linesLo[j], (void*)(intptr_t)vals[j]);
  }
}

#endif
