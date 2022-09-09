#ifndef GRAPHCALC_H
#define GRAPHCALC_H

#include "graph.c"

void treeCalcGlobalInit(int ts);
void treeCalc(TreeData* g, int maxCombos);
void treeCalcDebug(int enable);

#endif

#if defined(GRAPHCALC_IMPLEMENTATION) && !defined(GRAPHCALC_UNIT)
#define GRAPHCALC_UNIT

#include "humanize.c"
#include "generated.c"
#include "utils.c"

#include <string.h>

// TODO: remove this?
#include <inttypes.h>
extern void dbg(char* fmt, ...);

// TODO: figure out a way to embed numpy + python for non-browser version?
// ... or just port the calculator to C

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_ASYNC_JS(void, pyInit, (int ts), {
  py = await loadPyodide();
  await py.loadPackage("numpy");

  // the easiest way to install my cubecalc into pyodide is to zip it and extract it in the
  // virtual file system
  let zipResponse = await fetch("cubecalc.zip?ts=" + ts);
  let zipBinary = await zipResponse.arrayBuffer();
  py.unpackArchive(zipBinary, "zip");

  // install pip packages, init glue code etc
  Module.pyFunc = (x) => py.runPython(`
      import sys; sys.path.append("cubecalc/");
      from glue import ${x}; ${x}
  `);

  Module.numCombos = {};
  Module.comboLen = {};
  Module.matchingLine = {};
  Module.matchingProbability = {};
  Module.matchingValue = {};
  Module.matchingIsPrime = {};
});

EM_JS(void, pyCalcDebug, (int enable), {
  return Module.pyFunc("calc_debug")(enable != 0);
});

EM_JS(void, pyCalcFree, (int calcIdx), {
  return Module.pyFunc("calc_free")(calcIdx);
});

EM_JS(void, pyCalcSet, (int calcIdx, int key, i64 value), {
  return Module.pyFunc("calc_set")(calcIdx, key, value);
});

EM_JS(void, pyCalcWant, (int calcIdx, i64 key, int value), {
  return Module.pyFunc("calc_want")(calcIdx, key, value);
});

EM_JS(void, pyCalcWantOp, (int calcIdx, int op, int n), {
  return Module.pyFunc("calc_want_op")(calcIdx, op, n);
});

EM_JS(int, pyCalcWantPush, (int calcIdx), {
  return Module.pyFunc("calc_want_push")(calcIdx);
});

EM_JS(int, pyCalcWantLen, (int calcIdx), {
  return Module.pyFunc("calc_want_len")(calcIdx);
});

EM_JS(int, pyCalcWantCurrentLen, (int calcIdx), {
  return Module.pyFunc("calc_want_current_len")(calcIdx);
});

EM_JS(float, pyCalc, (int calcIdx), {
  return Module.pyFunc("calc")(calcIdx);
});

EM_JS(int, pyCalcMatchingLen, (int calcIdx), {
  return Module.matchingLine[calcIdx].length;
});

EM_JS(int, pyCalcMatchingComboLen, (int calcIdx), {
  return Module.comboLen[calcIdx];
});

EM_JS(i64, pyCalcMatchingNumCombos, (int calcIdx), {
  return BigInt(Module.numCombos[calcIdx]);
});

EM_JS(void, pyCalcMatchingLoad, (int calcIdx, int maxCombos), {
  Module.numCombos[calcIdx] = Module.pyFunc("calc_matching_len")(calcIdx);
  if (Module.numCombos[calcIdx] > maxCombos) {
    Module.matchingLine[calcIdx] =
    Module.matchingProbability[calcIdx] =
    Module.matchingValue[calcIdx] =
    Module.matchingIsPrime[calcIdx] = [];
    Module.comboLen[calcIdx] = 0;
    return;
  }
  Module.matchingLine[calcIdx] = Module.pyFunc("calc_matching_lines")(calcIdx);
  Module.matchingProbability[calcIdx] = Module.pyFunc("calc_matching_probabilities")(calcIdx);
  Module.matchingValue[calcIdx] = Module.pyFunc("calc_matching_values")(calcIdx);
  Module.matchingIsPrime[calcIdx] = Module.pyFunc("calc_matching_is_primes")(calcIdx);
  Module.comboLen[calcIdx] = Module.matchingLine[calcIdx][0].length;
});

EM_JS(i64, pyCalcMatchingLine, (int calcIdx, int i, int j), {
  return BigInt(Module.matchingLine[calcIdx][i][j]);
});

EM_JS(int, pyCalcMatchingValue, (int calcIdx, int i, int j), {
  return Module.matchingValue[calcIdx][i][j];
});

EM_JS(float, pyCalcMatchingProbability, (int calcIdx, int i, int j), {
  return Module.matchingProbability[calcIdx][i][j];
});

EM_JS(float, pyCalcMatchingIsPrime, (int calcIdx, int i, int j), {
  return Module.matchingIsPrime[calcIdx][i][j];
});
#endif

void treeCalcGlobalInit(int ts) {
  pyInit(ts);
}

void treeCalcDebug(int enable) {
  pyCalcDebug(enable);
}

static
void treeCalcAppendWants(int id, int* values) {
  int key, value;
  // TODO: DRY
  if (values[NSTAT] == -1) {
    key = treeDefaultValue(NSTAT, 0);
    dbg("(assumed) ");
  } else {
    key = values[NSTAT];
  }
  dbg("%s = ", lineNames[key]);
  if (values[NAMOUNT] == -1) {
    value = treeDefaultValue(NAMOUNT, key);
    dbg("(assumed) ");
  } else {
    value = values[NAMOUNT];
  }
  dbg("%d\n", value);
  values[NSTAT] = -1;
  values[NAMOUNT] = -1;
  pyCalcWant(id, lineValues[key], value);
}

static
int treeCalcFinalizeWants(int id, int* values, int optionalCondition) {
  // append complete any pending line to wants, or just the default line
  if (optionalCondition || values[NSTAT] != -1 || values[NAMOUNT] != -1) {
    treeCalcAppendWants(id, values);
    return 1;
  }
  return 0;
}

static
int treeTypeToCalcOperator(int type) {
  switch (type) {
    case NOR: return calcoperatorValues[OR];
    case NAND: return calcoperatorValues[AND];
  }
  return 0;
}

static
int treeCalcBranch(TreeData* g, int id, int* values, int node, int* seen) {
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
      return treeCalcBranch(g, id, values, d->value, seen);
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

  // we implicitly AND all the stats in a branch. usually this is trivial since we just keep
  // adding them to a dict which gets pushed on the stack when we hit an operator. but if you
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
    int branchElementsOnStack = treeCalcBranch(g, id, values, n->connections[i], seen);
    elementsOnStack += branchElementsOnStack;

    // operators. for every branch, finish pending stats and push the parameters to the stack
    // we don't want default value if there's no stats in this branch, otherwise we would get
    // a bunch of 21 att's for each branch that doesn't have stats.
    // that's why WantPush only pushes and returns 1 if the current stats dict is not empty.
    if (isOperator) {
      treeCalcFinalizeWants(id, values, 0);

      int pushed = pyCalcWantPush(id);

      if (branchElementsOnStack > 0) {
        pyCalcWantOp(id, treeTypeToCalcOperator(NAND), branchElementsOnStack + 1);
        pyCalcWantPush(id);
        ++numOperands;
        dbg("%d emitting AND\n", node);
      } else {
        numOperands += pushed;
        dbg("%d pushed: %d\n", node, pushed);
      }
    }
  }
  if (isOperator) {
    if (numOperands) {
      pyCalcWantOp(id, treeTypeToCalcOperator(n->type), numOperands);
      pyCalcWantPush(id);
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

  // every time we counter either stat or amount:
  // - if it's amount, the desired stat pair is complete and we append either the default line
  //   or the upstream line to the wants array
  // - if it's a line, we save it but we don't do anything until either an amount is found
  //   downstream, another line is found or we're done visiting the graph

  if (n->type == NAMOUNT || (n->type == NSTAT && values[NSTAT] != -1)) {
    treeCalcAppendWants(id, values); // add either default line or upstream line
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
    case NSTAT: return lineNames[value];
    case NREGION: return regionNames[value];
  }
  return 0;
}

static
i64 valueToCalc(int type, int value) {
  switch (type) {
    case NCUBE: return cubeValues[value];
    case NTIER: return tierValues[value];
    case NCATEGORY: return categoryValues[value];
    case NSTAT: return lineValues[value];
    case NREGION: return regionValues[value];
  }
  return value;
}

static
int treeTypeToCalcParam(int type) {
  switch (type) {
    case NCUBE: return calcparamValues[CUBE];
    case NTIER: return calcparamValues[TIER];
    case NCATEGORY: return calcparamValues[CATEGORY];
    case NLEVEL: return calcparamValues[LEVEL];
    case NREGION: return calcparamValues[REGION];
  }
  return 0;
}

void treeCalc(TreeData* g, int maxCombos) {
  for (size_t i = 0; i < BufLen(g->tree); ++i) {
    Node* n = &g->tree[i];
    // TODO: more type of result nodes (median, 75%, 85%, etc)
    if (n->type == NRESULT) {
      NodeData* d = &g->data[n->type][n->data];
      dbg("treeCalc %s\n", d->name);
      pyCalcFree(n->id);

      // initialize wants array so that it doesn't happen when traversing a tree
      // (we don't want it to count as an operand or stuff like that)
      pyCalcWantPush(n->id);

      int values[NLAST];
      for (size_t j = 0; j < NLAST; ++j) {
        values[j] = -1;
      }

      int* seen = 0;
      BufReserve(&seen, BufLen(g->tree));
      BufZero(seen);
      int elementsOnStack = treeCalcBranch(g, n->id, values, i, seen);
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
          values[j] = treeDefaultValue(j, values[CATEGORY]);
          dbg("(assumed) ");
        }
        char const* svalue = valueName(j, values[j]);
        char* valueName = nodeNames[j - 1];
        if (svalue) {
          dbg("%s = %s\n", valueName, svalue);
        } else {
          dbg("%s = %d\n", valueName, values[j]);
        }
        int param = treeTypeToCalcParam(j);
        i64 value = valueToCalc(j, values[j]);
        if (param) {
          pyCalcSet(n->id, param, value);
        } else {
          dbg("unknown calc param %zu = %d = %" PRId64 "\n", j, values[j], value);
        }
      }

      // if the wants stack is completely empty, append the default value.
      // also complete any pending stats
      treeCalcFinalizeWants(n->id, values, !pyCalcWantLen(n->id));

      // terminate with an AND in case we have multiple sets of stats
      elementsOnStack += pyCalcWantPush(n->id);

      if (elementsOnStack > 1) {
        pyCalcWantOp(n->id, treeTypeToCalcOperator(NAND), -1);
      }

      float chance = pyCalc(n->id);
      Result* resd = &g->resultData[n->data];
      if (chance > 0) {
#define fmt(x, y) humanize(resd->x, sizeof(resd->x), y)
#define quant(n, ...) fmt(within##n, ProbToGeoDistrQuantileDingle(chance, n))
        fmt(average, ProbToOneIn(chance));
        quant(50);
        quant(75);
        quant(95);
        quant(99);

        BufClear(resd->line);
        BufFreeClear((void**)resd->value);
        BufFreeClear((void**)resd->prob);
        BufClear(resd->prime);

        pyCalcMatchingLoad(n->id, maxCombos);
        resd->numCombos = pyCalcMatchingNumCombos(n->id);
        fmt(numCombosStr, resd->numCombos);

#undef fmt
#undef quant

        int comboLen = pyCalcMatchingComboLen(n->id);
        resd->comboLen = comboLen;
        for (int i = 0; i < pyCalcMatchingLen(n->id); ++i) {
          float comboProbability = 0;
          for (int j = 0; j < comboLen; ++j) {
            *BufAlloc(&resd->line) = pyCalcMatchingLine(n->id, i, j);
            BufAllocStrf(&resd->value, "%d", pyCalcMatchingValue(n->id, i, j));
            float prob = pyCalcMatchingProbability(n->id, i, j);
            BufAllocStrf(&resd->prob, "%.02f", prob);
            *BufAlloc(&resd->prime) = pyCalcMatchingIsPrime(n->id, i, j);
            comboProbability += prob;
          }
        }
      } else {
        resd->average[0] = resd->within50[0] = resd->within75[0] = resd->within95[0] =
          resd->within99[0] = 0;
        resd->page = 0;
        resd->comboLen = 0;
      }

      pyCalcFree(n->id);
    }
  }
}
#endif
