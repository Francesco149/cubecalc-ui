#ifndef GRAPH_H
#define GRAPH_H

#include "nuklear.h"
#include "utils.c"

// macro to generate things based on the list of node types
#define nodeTypes(f) \
  f(NCOMMENT) \
  f(NCUBE) \
  f(NTIER) \
  f(NCATEGORY) \
  f(NSTAT) \
  f(NAMOUNT) \
  f(NRESULT) \
  f(NSPLIT) \
  f(NLEVEL) \
  f(NREGION) \
  f(NOR) \
  f(NAND) \


#define appendComma(x) x,
enum {
  NINVALID = 0,
  nodeTypes(appendComma)
  NLAST
};

#define countEntries(x) 1+
#define nodeNamesCount (nodeTypes(countEntries)+0)
extern char* nodeNames[nodeNamesCount];

typedef struct _Node {
  int type;
  int id; // unique
  int data; // index into the data array of this type. updated on add/remove

  int* connections; // index into nodes array, updated on add/remove
} Node;

typedef struct _NodeData {
  struct nk_rect bounds;
  int value;
  int node;
  char name[16];
} NodeData;

#define COMMENT_MAX 255

typedef struct _Comment {
  char buf[COMMENT_MAX];
  int len;
} Comment;

typedef struct _Result {
  int page, perPage;

  char average[8];
  char within50[8];
  char within75[8];
  char within95[8];
  char within99[8];
  int comboLen;
  char** line;
  char** value;
  char** prob;
  intmax_t* prime;
  char numCombosStr[8];
} Result;

typedef struct _TreeData {
  Node* tree;
  NodeData* data[NLAST];
  Comment* commentData;
  Result* resultData;
} TreeData;

void treeGlobalInit();
void treeClear(TreeData* g);
int treeDefaultValue(int type, int stat);
int treeAdd(TreeData* g, int type, int x, int y);
void treeDel(TreeData* g, int nodeIndex);
void treeLink(TreeData* g, int from, int to);
void treeUnlink(TreeData* g, int from, int to);
NodeData* treeDataByNode(TreeData* g, int node);
Result* treeResultByNode(TreeData* g, int node);
void treeResultClear(Result* r);

#endif
#if defined(GRAPH_IMPLEMENTATION) && !defined(GRAPH_UNIT)
#define GRAPH_UNIT

#include "generated.c"

#include <string.h>
#include <ctype.h> // tolower
#include <stdio.h> // snprintf

// we need the data to be nicely packed in memory so that we don't have to traverse a tree
// when we draw the nodes since that would be slow.
// but we also need a tree that represents relationships between nodes which is only traversed
// when we calculate or when we change a connection

char* nodeNames[nodeNamesCount] = { nodeTypes(StringifyComma) };

// TODO: avoid the extra pointers, this is not good for the cpu cache
// ideally cache 1 page worth of lines into the struct for performance at draw time
void treeResultClear(Result* r) {
  BufFree(&r->line);
  BufFreeClear((void**)r->value);
  BufFree(&r->value);
  BufFreeClear((void**)r->prob);
  BufFree(&r->prob);
  BufFree(&r->prime);
  memset(r, 0, sizeof(*r));
}

// NSOME_NODE_NAME -> Some Node Name
static void treeInitNodeNames() {
  for (size_t i = 0; i < ArrayLength(nodeNames); ++i) {
    char* p = nodeNames[i] = strdup(nodeNames[i]);
    size_t len = strlen(p + 1);
    memmove(p, p + 1, len + 1);
    for (size_t j = 1; j < len; ++j) {
      if (p[j] == '_') {
        p[j++] = ' ';
      } else {
        p[j] = tolower(p[j]);
      }
    }
  }
}

void treeGlobalInit() {
  treeInitNodeNames();
}

void treeClear(TreeData* g) {
  BufEach(Node, g->tree, n) {
    BufFree(&n->connections);
  }
  BufClear(g->tree);
  for (size_t i = 0; i < NLAST; ++i) {
    BufClear(g->data[i]);
  }
  BufClear(g->commentData);
  BufEach(Result, g->resultData, r) {
    treeResultClear(r);
  }
  BufClear(g->resultData);
}

int treeDefaultValue(int type, int statIndex) {
  switch (type) {
    case NCUBE: return RED_IDX;
    case NTIER: return LEGENDARY_IDX;
    case NCATEGORY: return WEAPON_IDX;
    case NSTAT: return ATT_IDX;
    case NAMOUNT: {
      switch (statIndex) {
        case COOLDOWN_IDX:
        case INVIN_IDX:
          return 2;
        case DECENTS_IDX:
          return 1;
        case CRITDMG_IDX:
          return 8;
        case MESO_IDX:
        case DROP_IDX:
          return 20;
        case BOSS_ONLY_IDX:
        case BOSS_IDX:
        case IED_IDX:
          return 30;
        case FLAT_ATT_IDX:
          return 10;
      }
      return 21;
    }
    case NSPLIT: return -1;
    case NLEVEL: return 150;
    case NREGION: return GMS_IDX;
  }
  return 0;
}

// could use a unique number like time(0) but whatever
static int treeNextId(TreeData* g) {
  int id = 0;
nextId:
  for (size_t i = 0; i < BufLen(g->tree); ++i) {
    if (g->tree[i].id == id) {
      ++id;
      goto nextId;
    }
  }
  return id;
}

int treeAdd(TreeData* g, int type, int x, int y) {
  int id = treeNextId(g); // important: do this BEFORE allocating the node
  NodeData* d = BufAllocZero(&g->data[type]);
  Node* n;
  int chars;

  switch (type) {
    case NSPLIT:
    case NOR:
    case NAND:
      d->bounds = nk_rect(x, y, 70, 30);
      break;
    case NCATEGORY:
      d->bounds = nk_rect(x, y, 300, 80);
      break;
    default:
      d->bounds = nk_rect(x, y, 200, 80);
  }
  d->value = treeDefaultValue(type, ATT_IDX);
  chars = snprintf(d->name, sizeof(d->name), "%s %d", nodeNames[type - 1], id);
  if (chars >= sizeof(d->name)) {
    BufDel(g->data[type], -1);
    return -1;
  }

  n = BufAllocZero(&g->tree);
  n->id = id;
  n->data = BufI(g->data[type], -1);
  n->type = type;
  d->node = BufI(g->tree, -1);

  switch (type) {
    case NRESULT:
      BufAllocZero(&g->resultData);
      break;
    case NCOMMENT:
      BufAllocZero(&g->commentData);
      break;
  }

  return d->node;
}

void treeDel(TreeData* g, int nodeIndex) {
  int type = g->tree[nodeIndex].type;
  int index = g->tree[nodeIndex].data;
  NodeData* d = &g->data[type][index];

  BufFree(&g->tree[nodeIndex].connections);

  // since we are deleting an element in the packed arrays we have to adjust all indices pointing
  // after it. this means we have slow add/del but fast iteration which is what we want
  for (size_t i = 0; i < BufLen(g->tree); ++i) {
    if (i == nodeIndex) continue;
    Node* n = &g->tree[i];

    // adjust connections indices
    int* newConnections = 0;
    for (size_t j = 0; j < BufLen(n->connections); ++j) {
      if (n->connections[j] > nodeIndex) {
        *BufAlloc(&newConnections) = n->connections[j] - 1;
      } else if (n->connections[j] < nodeIndex) {
        *BufAlloc(&newConnections) = n->connections[j];
      }
      // if the node is referring to the node we're removing, just remove the connection.
      // TODO: try to connect to removed node's connections?
    }
    BufFree(&n->connections);
    n->connections = newConnections;

    if (n->type == NSPLIT) {
      if (g->data[n->type][n->data].value > nodeIndex) {
        --g->data[n->type][n->data].value;
      } else if (g->data[n->type][n->data].value == nodeIndex) {
        g->data[n->type][n->data].value = -1;
      }
    }

    // NOTE: we are USING n->data so it needs to be modified after we use it
    // dangerous code

    // adjust node indices
    if (i > nodeIndex) {
      --g->data[n->type][n->data].node;
    }

    // adjust data indices
    if (n->type == type && n->data > index) {
      --n->data;
    }
  }

  // actually delete elements and shift everything after them left by 1
  BufDel(g->tree, nodeIndex);
  BufDel(g->data[type], index);

  switch (type) {
    case NCOMMENT:
      BufDel(g->commentData, index);
      break;
    case NRESULT:
      treeResultClear(&g->resultData[index]);
      BufDel(g->resultData, index);
      break;
  }
}

void treeLink(TreeData* g, int from, int to) {
  // redundant but useful so we can walk up the graph without searching all nodes
  if (BufFindInt(g->tree[from].connections, to) >= 0) {
    // could sanity check the other connections here?
    return;
  }
  *BufAlloc(&g->tree[from].connections) = to;
  *BufAlloc(&g->tree[to].connections) = from;
}

void treeUnlink(TreeData* g, int from, int to) {
  BufDelFindInt(g->tree[from].connections, to);
  BufDelFindInt(g->tree[to].connections, from);
}

NodeData* treeDataByNode(TreeData* g, int node) {
  Node* n = &g->tree[node];
  return &g->data[n->type][n->data];
}

Result* treeResultByNode(TreeData* g, int node) {
  Node* n = &g->tree[node];
  return &g->resultData[n->data];
}

#endif
