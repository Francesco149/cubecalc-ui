#ifndef GRAPHCALC_H
#define GRAPHCALC_H

#include "graph.c"
#include "multithread.c"

void treeCalcGlobalInit();
void treeCalcGlobalFree();

// start a recalc
void treeCalc(TreeData* g, size_t maxCombos);

// check if the recalc is done and merge results with the tree if so.
// returns nonzero if anything was merged
int treeCalcMerge(TreeData* g);

// returns the number of recalcs in progress
size_t treeCalcJobs();

#endif

#if defined(GRAPHCALC_IMPLEMENTATION) && !defined(GRAPHCALC_UNIT)
#define GRAPHCALC_UNIT

#include "generated.c"
#include "utils.c"
#include "cubecalc.c"

#include <string.h>

// TODO: remove this?
#include <inttypes.h>
extern void dbg(char* fmt, ...);

void treeCalcGlobalInit() {
  MTGlobalInit();
  CubeGlobalInit();
}

void treeCalcMTGlobalFree();
void treeCalcGlobalFree() {
  CubeGlobalFree();
  treeCalcMTGlobalFree();
}

// we want to be able to override stats
//
// for example:
//
// (30 boss)---(20 boss)---(result)
//
// means 20 boss
//
// note that something like
//
// (30 boss)--(or)--(21 att)--+--(result)
// (20 boss)------------------'
//
// still means ((30+ boss or 21+ att) and 20+ boss)
//
// to achieve this, for every operator we collect all stats in a sparse array and we don't
// push them to the wants stack until we encounter an operator

static const size_t numLines = ArrayLength(allLinesHi);

// returns non-zero if it didn't override a previously set value
static
int treeCalcAppendWants(int statMap[numLines], int* values) {
  int key, value;
  // TODO: DRY
  if (values[NSTAT] == -1) {
    key = treeDefaultValue(NSTAT, 0);
    dbg("(assumed) ");
  } else {
    key = values[NSTAT];
  }
  dbg("%s = ", allLineNames[key]);
  if (values[NAMOUNT] == -1) {
    value = treeDefaultValue(NAMOUNT, key);
    dbg("(assumed) ");
  } else {
    value = values[NAMOUNT];
  }
  dbg("%d\n", value);
  values[NSTAT] = -1;
  values[NAMOUNT] = -1;
  int res = statMap[key] < 0;
  statMap[key] = value;
  return res;
}

static
int treeCalcFinalizeWants(int statMap[numLines], int* values, int optionalCondition) {
  // append complete any pending line to wants, or just the default line
  if (optionalCondition || values[NSTAT] != -1 || values[NAMOUNT] != -1) {
    return treeCalcAppendWants(statMap, values);
  }
  return 0;
}

static
int treeTypeToCalcOperator(int type) {
  switch (type) {
    case NOR: return WANT_OR;
    case NAND: return WANT_AND;
  }
  return 0;
}

// flush the statMap and push everything to the stack
static
void treeCalcPushStats(Want** pwants, int statMap[numLines]) {
  RangeBefore(numLines, i) {
    if (statMap[i] >= 0) {
      *BufAlloc(pwants) = (Want){
        .type = WANT_STAT,
        .lineLo = allLinesLo[i],
        .lineHi = allLinesHi[i],
        .value = statMap[i],
      };
      statMap[i] = -1;
    }
  }
}

static
int treeCalcBranch(TreeData* g, Want** pwants,
    int statMap[numLines], int* values, int node, int* seen)
{
  if (seen[node]) {
    // it's important to not visit the same node twice so we don't get into a loop
    return 0;
  }

  seen[node] = 1;

  Node* n = &g->tree[node];
  NodeData* d = &g->data[n->type][n->data];

  // TODO: flatten this so we don't use recursion
  if (n->type == NSPLIT) {
    if (d->value >= 0) {
      // in the case of a split, we only take the parent so that we don't traverse the other
      // branches. that's the whole point of this node. to allow reusing common nodes
      return treeCalcBranch(g, pwants, statMap, values, d->value, seen);
    }
    return 0;
  }

  int isOperator = 0;
  switch (n->type) {
  case NOR:
  case NAND:
    isOperator = 1;
    break;
  }

  // we implicitly AND all the stats in a branch. usually this is trivial but if you
  // have nested operators, you end up having to actually emit an AND operator since you will
  // end up with multiple values on the stack
  //
  // for example if we have
  // a -- or -- b
  //       |
  //       c
  //       |
  //       or -- z
  // that should translate to [a, b, <or 2>, c, <and 2> z <or 2>], or (((a || b) && c) || z)
  // the and is implicit and we have to figure out when to emit it by keeping track of how many
  // extra elements on the stack we will have from upstream branches

  int numOperands = 0;
  int elementsOnStack = 0;

  for (size_t i = 0; i < BufLen(n->connections); ++i) {
    int branchElementsOnStack =
      treeCalcBranch(g, pwants, statMap, values, n->connections[i], seen);

    elementsOnStack += branchElementsOnStack;

    // operators. for every branch, finish pending stats and push the parameters to the stack
    // we don't want default value if there's no stats in this branch, otherwise we would get
    // a bunch of 21 att's for each branch that doesn't have stats.
    // that's why FinalizeWants only returns 1 if it actually adds anything to the wants buf
    if (isOperator) {
      int extraStackElements = treeCalcFinalizeWants(statMap, values, 0);

      if (branchElementsOnStack > 0) {
        treeCalcPushStats(pwants, statMap);
        *BufAlloc(pwants) = WantOp(AND, branchElementsOnStack + extraStackElements);
        ++numOperands;
        dbg("%d emitting AND\n", node);
      } else {
        numOperands += extraStackElements;
        dbg("%d extraStackElements=%d\n", node, extraStackElements);
      }
    }
  }
  if (isOperator) {
    if (numOperands) {
      treeCalcPushStats(pwants, statMap);
      *BufAlloc(pwants) = (Want){
        .type = WANT_OP,
        .op = treeTypeToCalcOperator(n->type),
        .opCount = numOperands,
      };
    }
    return 1;
  }

  switch (n->type) {
    case NCUBE:
    case NTIER:
    case NCATEGORY:
    case NAMOUNT:
    case NLEVEL:
    case NREGION:
      values[n->type] = d->value;
    case NSTAT: // handled below
    case NRESULT:
      break;
    default:
      dbg("error visiting node %d, unk type %d\n", node, n->type);
  }

  // every time we encounter either stat or amount:
  // - if it's amount, the desired stat pair is complete and we append either the default line
  //   or the upstream line to the stat map
  // - if it's a line, we save it but we don't do anything until either an amount is found
  //   downstream, another line is found or we're done visiting the graph

  if (n->type == NAMOUNT || (n->type == NSTAT && values[NSTAT] != -1)) {
    elementsOnStack += treeCalcAppendWants(statMap, values);
    // add either default line or upstream line
  }

  if (n->type == NSTAT) {
    values[NSTAT] = d->value; // save this line for later
  }

  return elementsOnStack;
}

static
char const* valueName(int type, int value) {
  switch (type) {
    case NCUBE: return cubeNames[value];
    case NTIER: return tierNames[value];
    case NCATEGORY: return categoryNames[value];
    case NSTAT: return allLineNames[value];
    case NREGION: return regionNames[value];
  }
  return 0;
}

#ifdef CUBECALC_DEBUG
void WantPrint(Want const* wantBuf);
#endif

typedef struct _TreeCalcJobData {
  size_t maxCombos;
  char* treeData;
  int resultId;
  intmax_t revision;
} TreeCalcJobData;

void treeCalcJobDataFree(TreeCalcJobData* data) {
  BufFree(&data->treeData);
  free(data);
}

static
int resultById(TreeData* g, int id) {
  BufEach(Node, g->tree, n) {
    if (n->id == id) {
      return n->data;
    }
  }
  return -1;
}

void* treeCalcJob(void* data) {
  TreeCalcJobData* jobData = data;
  TreeData* g = malloc(sizeof(TreeData));
  MemZero(g);

  NodeData* d;
  Node* n;
  Want* wants = 0;

  int values[NLAST];
  int statMap[numLines];
  int* seen = 0;
  int elementsOnStack;

  if (!unpackTree(g, jobData->treeData)) {
    dbg("treeCalcJob: unexpected failure deserializing tree");
    goto cleanup;
  }

  int resultIdx = resultById(g, jobData->resultId);
  if (resultIdx < 0) {
    dbg("treeCalcJob: couldn't find result id %d", jobData->resultId);
    goto cleanup;
  }
  d = &g->data[NRESULT][resultIdx];
  n = &g->tree[d->node];

  dbg("treeCalcJob %s\n", d->name);

  ArrayEach(int, values, x) { *x = -1; }
  ArrayEach(int, statMap, x) { *x = -1; }

  (void)BufReserve(&seen, BufLen(g->tree));
  BufZero(seen);
  BufClear(wants);
  elementsOnStack = treeCalcBranch(g, &wants, statMap, values, d->node, seen);
  BufFree(&seen);

  for (size_t j = NINVALID + 1; j < NLAST; ++j) {
    switch (j) {
      case NSTAT:
      case NAMOUNT:
      case NRESULT:
      case NCOMMENT:
      case NSPLIT:
      case NOR:
      case NAND:
        // these are handled separately, or are not relevant
        continue;
    }
    if (values[j] == -1) {
      values[j] = treeDefaultValue(j, values[NCATEGORY]);
      dbg("(assumed) ");
    }
    char const* svalue = valueName(j, values[j]);
    char* valueName = nodeNames[j - 1];
    if (svalue) {
      dbg("%s = %s\n", valueName, svalue);
    } else {
      dbg("%s = %d\n", valueName, values[j]);
    }
  }

  // complete any pending stats and push all the stats to the stack
  elementsOnStack += treeCalcFinalizeWants(statMap, values, 0);
  treeCalcPushStats(&wants, statMap);

  if (BufLen(wants)) {
    // terminate with an AND since we always want an operator
    *BufAlloc(&wants) = WantOp(AND, elementsOnStack);

#ifdef CUBECALC_DEBUG
    dbg("===========================================\n");
    dbg("# %s\n", g->data[n->type][n->data].name);
    WantPrint(wants);
    dbg("===========================================\n");
#endif

    Category category = categoryValues[values[NCATEGORY]];
    Cube cube = cubeValues[values[NCUBE]];
    Tier tier = tierValues[values[NTIER]];
    Region region = regionValues[values[NREGION]];

    Lines combos = {0};

    float p = CubeCalc(wants, category, cube, tier, values[NLEVEL], region, &combos);
    dbg("p: %f\n", p);
    Result* resd = &g->resultData[n->data];
    treeResultClear(resd);
    if (p > 0) {

#define fmt(x, y) Humanize(resd->x, sizeof(resd->x), y)
#define quant(n, ...) fmt(within##n, ProbToGeoDistrQuantileDingle(p, n))
      fmt(average, ProbToOneIn(p));
      quant(50);
      quant(75);
      quant(95);
      quant(99);

      size_t numCombos = BufLen(combos.onein) / combos.comboSize;
      fmt(numCombosStr, numCombos);

      if (numCombos <= jobData->maxCombos) {
        BufEachi(combos.onein, i) {
          *BufAlloc(&resd->line) = LineToStr(combos.lineHi[i], combos.lineLo[i]);
          BufAllocStrf(&resd->value, "%d", combos.value[i]);
          BufAllocStrf(&resd->prob, "%.02f", 1/combos.onein[i]);
        }

        (void)BufCpy(&resd->prime, combos.prime);
        resd->comboLen = combos.comboSize;
      }
    }
    LinesFree(&combos);
  }

cleanup:
  BufFree(&wants);
  g->revision = jobData->revision;
  treeCalcJobDataFree(jobData);
  return g;
}

static MTJob** jobs = 0;
static int* resultIds = 0;

void treeCalc(TreeData* g, size_t maxCombos) {
  ++g->revision;

  // lazy but safe: just serialize the three and deserialize it to make a copy.
  // this way we don't have to worry about making a proper deep copy of it and potentially
  // duplicate a lot of the serialization logic.
  Arena* arena = ArenaInit();
  Allocator allocatorArena = ArenaAllocator(arena);
  char* out = packTree(&allocatorArena, g);
  if (!out) {
    dbg("treeCalc: unexpected failure serializing tree");
  } else {
    BufEachi(g->resultData, i) {
      Node* n = &g->tree[g->data[NRESULT][i].node];
      TreeCalcJobData* data = malloc(sizeof(TreeCalcJobData));
      MemZero(data);
      data->maxCombos = maxCombos;
      data->treeData = BufDup(out);
      data->resultId = n->id;
      data->revision = g->revision;
      *BufAlloc(&jobs) = MTStart(treeCalcJob, data);
      *BufAlloc(&resultIds) = n->id;
    }
  }

  ArenaFree(arena);
}

int treeCalcMerge(TreeData* g) {
  int res = 0;
  intmax_t* keep = 0;
  BufEachi(jobs, i) {
    MTJob* j = jobs[i];
    if (MTDone(j)) {
      TreeData* merge = MTResult(j);
      dbg("joined %p\n", merge);
      dbg("revision %jd\n", merge->revision);
      // we keep track of whether the tree has changed since the calc was started.
      // this is done by incrementing revision every time treeCalc is called.
      // if it doesn't match with what it was when job was started, then we ignore the job result
      if (merge->revision == g->revision) {
        int rdata = resultById(merge, resultIds[i]);
        if (rdata < 0) {
          dbg("treeCalcMerge: couldn't locate result id %d", resultIds[i]);
        } else {
          Result* r = &merge->resultData[rdata];
          int drdata = resultById(g, resultIds[i]);
          Result* dr = &g->resultData[drdata];
          treeResultClear(dr);
          *dr = *r;
          MemZero(r); // to make sure it doesn't get freed twice
          res = 1;
        }
      } else {
        // I don't think this can happen now that I only allow starting new calcs when there's
        // no background jobs, but might as well check for good measure
        dbg("(discarded, current revision is %jd)\n", g->revision);
      }
      treeClear(merge);
      treeFree(merge);
      free(merge);
      MTFree(j);
      dbg("%zu done\n", i);
    } else {
      *BufAlloc(&keep) = i;
    }
  }

  MTJob** newJobs = 0;
  int* newResultIds = 0;
  BufIndex(jobs, keep, &newJobs);
  BufIndex(resultIds, keep, &newResultIds);
  BufFree(&jobs);
  BufFree(&resultIds);
  jobs = newJobs;
  resultIds = newResultIds;
  BufFree(&keep);

  return res;
}

void treeCalcMTGlobalFree() {
  BufEach(MTJob*, jobs, pj) {
    size_t n = 0;
    while (!MTDone(*pj)) {
      MTYield(&n);
    }
    MTFree(*pj);
  }
  BufFree(&jobs);
  BufFree(&resultIds);
  MTGlobalFree();
}

size_t treeCalcJobs() {
  return BufLen(jobs);
}
#endif
