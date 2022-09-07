#define NK_IMPLEMENTATION
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR

// required by glfw backend
#define NK_KEYSTATE_BASED_INPUT
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_GLFW_ES2_IMPLEMENTATION

#include "thirdparty/nuklear.h"
#include "thirdparty/nuklear_glfw_es2.h"
#include <ctype.h>
#include <float.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <errno.h>

#include "utils.c"
#include "generated.c"

#include <sys/stat.h>
#include "thirdparty/protobuf-c/protobuf-c.c"
#include "proto/cubecalc.pb-c.h"
#include "proto/cubecalc.pb-c.c"


// TODO: figure out a way to embed numpy + python for non-browser version?
// ... or just port the calculator to C
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(int, canvas_get_width, (), {
  return Module.canvas.width;
});

EM_JS(int, canvas_get_height, (), {
  return Module.canvas.height;
});

EM_JS(void, resizeCanvas, (), {
  js_resizeCanvas();
});

EM_JS(float, deviceScaleFactor, (), {
  return js_deviceScaleFactor();
});

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

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

#define MAX_VERTEX_MEMORY 512 * 1024
#define MAX_ELEMENT_MEMORY 128 * 1024

#define CONTEXT_HEIGHT 20

enum {
  SHOW_INFO = 1<<0,
  SHOW_GRID = 1<<1,
  LINKING = 1<<2,
  UNLINKING = 1<<3,
  DIRTY = 1<<4,
  RESIZING = 1<<5,
  SHOW_DISCLAIMER = 1<<6,
  UPDATE_CONNECTIONS = 1<<7,
  LINKING_SPLIT = 1<<8,
  SAVED_MOUSE_POS = 1<<9,
  UPDATE_SIZE = 1<<10,
  PORTRAIT = 1<<11,
  FULL_INFO = 1<<12,
  DEBUG = 1<<13,
};

#define MUTEX_FLAGS ( \
    LINKING | \
    UNLINKING | \
    RESIZING | \
    0)

#define otherMutexFlags(x) (MUTEX_FLAGS & ~(x))
#define flagAllowed(x) (!(flags & otherMutexFlags(x)))

const struct nk_vec2 contextualSize = { .x = 300, .y = 240 };

GLFWwindow* win;
struct nk_context* nk;
struct nk_input* in;
char** errors;
char statusText[64];
int width, height;
int displayWidth, displayHeight;
int fps;
int flags = SHOW_INFO | SHOW_GRID | SHOW_DISCLAIMER | UPDATE_SIZE
#ifdef CUBECALC_DEBUG
| DEBUG
#endif
;
struct nk_vec2 pan = {.x = -210, .y = 0};
int linkNode;
int resizeNode;
int selectedNode = -1;
struct nk_vec2 savedMousePos; // in node space, not screen space
int tool;
int disclaimerHeight = 290;
int maxCombos = 50;
_Atomic int storageReady;

void dbg(char* fmt, ...) {
  if (flags & DEBUG) {
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
  }
}

void error(char* s) {
  *BufAlloc(&errors) = s;
  dbg("%s\n", s);
}

void status(char* fmt, ...) {
  va_list va;
  va_start(va, fmt);
  vsnprintf(statusText, sizeof(statusText), fmt, va);
  va_end(va);
}

void errorCallback(int e, const char *d) {
  dbg("Error %d: %s\n", e, d);
}

#define CALC_NAME "MapleStory Cubing Calculator"
#define INFO_NAME "Info"
#define DISCLAIMER_NAME "Disclaimer"
#define ERROR_NAME "Error"

struct nk_rect calcBounds, infoBounds, disclaimerBounds, errorBounds;

#define tools(f) \
  f(MOVE) \
  f(PAN) \
  f(CONTEXTUAL) \

int toolToMouseButton[] = {
  NK_BUTTON_LEFT,
  NK_BUTTON_MIDDLE,
  NK_BUTTON_RIGHT,
};

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

#define stringifyComma(x) #x,
char* nodeNames[] = { nodeTypes(stringifyComma) };
char const* toolNames[] = { tools(stringifyComma) };

#define NODEWND  NK_WINDOW_BORDER | NK_WINDOW_TITLE
#define OTHERWND NODEWND | NK_WINDOW_CLOSABLE
#define CALCWND  NK_WINDOW_NO_SCROLLBAR

// NSOME_NODE_NAME -> Some Node Name
void treeInitNodeNames() {
  for (size_t i = 0; i < NK_LEN(nodeNames); ++i) {
    char* p = nodeNames[i];
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

#define appendComma(x) x,
enum {
  NINVALID = 0,
  nodeTypes(appendComma)
  NLAST
};

enum { tools(appendComma) };

// we need the data to be nicely packed in memory so that we don't have to traverse a tree
// when we draw the nodes since that would be slow.
// but we also need a tree that represents relationships between nodes which is only traversed
// when we calculate or when we change a connection

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
#define COMMENT_ROUND 10
#define COMMENT_THICK 1
typedef struct _Comment {
  char buf[COMMENT_MAX];
  int len;
} Comment;

typedef struct _Result {
  char average[22];
  char within50[22];
  char within75[22];
  char within95[22];
  char within99[22];
  int page, perPage;
  int comboLen;
  i64* line;
  char** value;
  char** prob;
  int* prime;
  i64 numCombos;
  char numCombosStr[22];
} Result;

// TODO: avoid the extra pointers, this is not good for the cpu cache
// ideally cache 1 page worth of lines into the struct for performance at draw time
void ResultFree(Result* r) {
  BufFree(&r->line);
  BufFree(&r->value);
  BufFree(&r->prob);
  BufFree(&r->prime);
}

Node* tree;
NodeData* data[NLAST];
Comment* commentData;
Result* resultData;
int* removeNodes;

void treeInit() {
  treeInitNodeNames();
}

typedef struct _NodeLink {
  int from, to; // node index
  struct nk_color color;
  float thick;
} NodeLink;

NodeLink* links;

void treeClear() {
  for (size_t i = 0; i < NLAST; ++i) {
    BufClear(data[i]);
  }
  BufClear(commentData);
  BufClear(resultData);
  BufClear(removeNodes);
  flags |= UPDATE_CONNECTIONS;
}

struct nk_vec2 nodeSpaceToScreen(struct nk_vec2 v) {
  v = nk_layout_space_to_screen(nk, v);
  v.x -= pan.x;
  v.y -= pan.y;
  return v;
}

struct nk_rect nodeSpaceToScreenRect(struct nk_rect v) {
  struct nk_vec2 pos = nodeSpaceToScreen(nk_rect_pos(v));
  v.x = pos.x;
  v.y = pos.y;
  return v;
}

struct nk_vec2 nodeCenter(int node) {
  Node* n = &tree[node];
  int i = n->data;
  int type = n->type;
  struct nk_rect bounds = data[type][i].bounds;
  struct nk_vec2 center = nk_rect_pos(bounds);
  center.x += bounds.w / 2;
  center.y += bounds.h / 2;
  return center;
}

void treeUpdateConnections() {
  BufClear(links);

  // TODO: could be a bitmask
  int* done = 0;
  BufReserve(&done, BufLen(tree));
  memset(done, 0, BufLen(done) * sizeof(done[0]));

  for (size_t i = 0; i < BufLen(tree); ++i) {
    int* cons = tree[i].connections;
    done[i] = 1;
    for (size_t j = 0; j < BufLen(cons); ++j) {
      int other = cons[j];
      if (done[other]) continue;
      // TODO: could cache pre computed positions and update them when nodes are moved
      *BufAlloc(&links) = (NodeLink){
        .from = i,
        .to = other,
        .color = nk_rgb(100, 100, 100),
        .thick = 2,
      };
    }
    if (tree[i].type == NSPLIT) {
      NodeData* d = &data[NSPLIT][tree[i].data];
      if (d->value >= 0) {
        *BufAlloc(&links) = (NodeLink){
          .from = i,
          .to = d->value,
          .color = nk_rgb(200, 100, 200),
          .thick = 8,
        };
      }
    }
  }

  BufFree(&done);
  flags |= DIRTY;
}

int defaultValue(int type, i64 stat) {
  switch (type) {
    case NCUBE: return RED;
    case NTIER: return LEGENDARY;
    case NCATEGORY: return WEAPON;
    case NSTAT: return ATT;
    case NAMOUNT: {
      i64 val = lineValues[stat];
      if ((val & lineValues[COOLDOWN]) || val == lineValues[INVIN]) {
        return 2;
      }
      if (val & lineValues[DECENTS]) {
        return 1;
      }
      switch (stat) {
        case CRITDMG: return 8;
        case MESO:
        case DROP: return 20;
        case BOSS:
        case IED: return 30;
        case FLAT_ATT: return 10;
      }
      return 21;
    }
    case NSPLIT: return -1;
    case NLEVEL: return 150;
    case NREGION: return GMS;
  }
  return 0;
}

// could use a unique number like time(0) but whatever
int treeNextId() {
  int id = 0;
nextId:
  for (size_t i = 0; i < BufLen(tree); ++i) {
    if (tree[i].id == id) {
      ++id;
      goto nextId;
    }
  }
  return id;
}

int treeAdd(int type, i64 x, i64 y) {
  int id = treeNextId(); // important: do this BEFORE allocating the node
  NodeData* d = BufAlloc(&data[type]);
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
  d->value = defaultValue(type, ATT);
  chars = snprintf(d->name, sizeof(d->name), "%s %d", nodeNames[type - 1], id);
  if (chars >= sizeof(d->name)) {
    error("too many nodes, can't format node name");
    BufDel(data[type], -1);
    return -1;
  }

  n = BufAllocZero(&tree);
  n->id = id;
  n->data = BufI(data[type], -1);
  n->type = type;
  d->node = BufI(tree, -1);

  switch (type) {
    case NRESULT:
      flags |= DIRTY;
      BufAllocZero(&resultData);
      break;
    case NCOMMENT:
      BufAllocZero(&commentData);
      break;
  }

  return d->node;
}

void treeDel(int nodeIndex) {
  int type = tree[nodeIndex].type;
  int index = tree[nodeIndex].data;
  NodeData* d = &data[type][index];

  BufFree(&tree[nodeIndex].connections);

  // since we are deleting an element in the packed arrays we have to adjust all indices pointing
  // after it. this means we have slow add/del but fast iteration which is what we want
  for (size_t i = 0; i < BufLen(tree); ++i) {
    if (i == nodeIndex) continue;
    Node* n = &tree[i];

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
      if (data[n->type][n->data].value > nodeIndex) {
        --data[n->type][n->data].value;
      } else if (data[n->type][n->data].value == nodeIndex) {
        data[n->type][n->data].value = -1;
      }
    }

    // NOTE: we are USING n->data so it needs to be modified after we use it
    // dangerous code

    // adjust node indices
    if (i > nodeIndex) {
      --data[n->type][n->data].node;
    }

    // adjust data indices
    if (n->type == type && n->data > index) {
      --n->data;
    }
  }

  // actually delete elements and shift everything after them left by 1
  BufDel(tree, nodeIndex);
  BufDel(data[type], index);

  switch (type) {
    case NCOMMENT:
      BufDel(commentData, index);
      break;
    case NRESULT:
      ResultFree(&resultData[index]);
      BufDel(resultData, index);
      break;
  }

  flags |= UPDATE_CONNECTIONS;
}

void treeLink(int from, int to) {
  // redundant but useful so we can walk up the graph without searching all nodes
  if (BufFindInt(tree[from].connections, to) >= 0) {
    // could sanity check the other connections here?
    return;
  }
  *BufAlloc(&tree[from].connections) = to;
  *BufAlloc(&tree[to].connections) = from;
  flags |= UPDATE_CONNECTIONS;
}

void treeUnlink(int from, int to) {
  BufDelFindInt(tree[from].connections, to);
  BufDelFindInt(tree[to].connections, from);
}

static int frames;
void updateFPS() {
  static double framesTimer;
  double t = glfwGetTime();
  ++frames;
  if (t - framesTimer >= 1) {
    framesTimer = t;
    fps = frames;
    frames = 0;
  }
}

struct nk_rect commentBounds(struct nk_rect bounds) {
  bounds.x -= COMMENT_ROUND;
  bounds.y -= COMMENT_ROUND;
  bounds.w += COMMENT_ROUND * 2;
  bounds.h += COMMENT_ROUND * 2;
  bounds.x -= pan.x;
  bounds.y -= pan.y;
  return bounds;
}

#define COMMENT_H 30
#define COMMENT_BOUNDS_H (COMMENT_H + 10)

int adjustCommentBounds(int type, struct nk_rect* b) {
  if (type == NCOMMENT) {
    b->h = COMMENT_BOUNDS_H;
    return 1;
  }
  return 0;
}

int uiBeginNode(int type, int i, int h) {
  NodeData* d = &data[type][i];
  struct nk_rect bounds = d->bounds;
  struct nk_panel* parentPanel = nk_window_get_panel(nk);
  int winFlags = NODEWND;
  struct nk_style* style = &nk->style;
  struct nk_user_font const* font = style->font;

  // TODO: avoid branching, make a separate func for comments
  if (adjustCommentBounds(type, &bounds)) {
    winFlags &= ~NK_WINDOW_TITLE;
    winFlags |= NK_WINDOW_NO_SCROLLBAR;
  }

  nk_layout_space_push(nk, nk_rect(bounds.x - pan.x, bounds.y - pan.y, bounds.w, bounds.h));

  int res;
  if (!(res = nk_group_begin(nk, d->name, winFlags))) {
    return res;
  }

  struct nk_panel* panel = nk_window_get_panel(nk);
  struct nk_rect nodeBounds = panel->bounds;

  if (type != NRESULT) nk_layout_row_dynamic(nk, h, 1);

  struct nk_rect dragBounds = nodeBounds;

  if (type != NCOMMENT) {
    // calculate dragging click bounds (window title)
    // HACK: there doesn't seem any api to get a panel's full bounds including title etc
    // NOTE: this assumes the nodes always have a title bar
    // TODO: find a way to get rid of all this jank
    struct nk_vec2 panelPadding = nk_panel_get_padding(style, panel->type);
    int headerHeight = font->height +
      2 * style->window.header.padding.y +
      2 * style->window.header.label_padding.y;
    struct nk_vec2 scrollbarSize = style->window.scrollbar_size;
    nodeBounds.y -= headerHeight;
    nodeBounds.x -= panelPadding.x;
    nodeBounds.h += headerHeight + panelPadding.y;
    nodeBounds.w += scrollbarSize.x + panelPadding.x * 2 + style->window.header.padding.x;

    dragBounds = nodeBounds;
    dragBounds.h = headerHeight + panelPadding.y;
  }

  // HACK: nuklear's built in drag movement is janky when multiple windows overlap because of
  // the stateless nature of it so I make my own slight adjustments
  int leftMouseDown = in->mouse.buttons[NK_BUTTON_LEFT].down;
  int leftMouseClicked = in->mouse.buttons[NK_BUTTON_LEFT].clicked;
  int leftMouseClickInCursor = nk_input_has_mouse_click_down_in_rect(in,
      NK_BUTTON_LEFT, dragBounds, nk_true);

  // lock dragging to the window we started dragging so we don't drag other windows when we
  // hover over them during dragging
  static int draggingId = -1;
  int inCombo = nk->current->popup.win != 0; // HACK: this relies on nk internals
  int draggingThisNode = draggingId == tree[d->node].id;
  if (!inCombo && leftMouseDown && leftMouseClickInCursor && !leftMouseClicked &&
      tool == MOVE)
  {
    if (draggingId == -1 || draggingThisNode) {
      draggingId = tree[d->node].id;
      d->bounds.x += in->mouse.delta.x;
      d->bounds.y += in->mouse.delta.y;
      in->mouse.buttons[NK_BUTTON_LEFT].clicked_pos.x += in->mouse.delta.x;
      in->mouse.buttons[NK_BUTTON_LEFT].clicked_pos.y += in->mouse.delta.y;
      nk->style.cursor_active = nk->style.cursors[NK_CURSOR_MOVE];
    }
  } else if (draggingThisNode) {
    draggingId = -1;
  }

  return res;
}

int uiContextual(int type, int i) {
  NodeData* d = &data[type][i];
  struct nk_rect nodeBounds = nodeSpaceToScreenRect(d->bounds);

  adjustCommentBounds(type, &nodeBounds);

  // the contextual menu logic should be outside of the window logic because for things like
  // comments, we could trigger the contextual menu when the node's main window is not visible
  // by clicking on the border for example

  if (type == NCOMMENT) {
    const int loff = COMMENT_ROUND + COMMENT_THICK * 3;
    const int roff = COMMENT_ROUND;
    struct nk_rect left, right, top, bottom;
    left = right = top = bottom = commentBounds(d->bounds);
    left.x -= roff / 2;
    left.w = loff;
    right.x += right.w - roff / 2;
    right.w = loff;
    top.y -= roff / 2;
    top.h = loff;
    bottom.y += bottom.h - roff / 2;
    bottom.h = loff;

    // IMPORTANT:
    // the contextual menu system relies on having a consistent amount of calls to
    // contextual_begin (call count is the index of the context menu)
    // this is kind of a hack, but we have to check if we're in the context menu rect before
    // we call nk_contextual_begin so that there's only one call per unique contextual menu

#define checkRect(x) if (nk_input_mouse_clicked(in, NK_BUTTON_RIGHT, x)) nodeBounds = x
    checkRect(left);
    checkRect(right);
    checkRect(top);
    checkRect(bottom);
  }

  int showContextual = nk_contextual_begin(nk, 0, contextualSize, nodeBounds);
  if (showContextual) {
    selectedNode = d->node;
    nk_layout_row_dynamic(nk, CONTEXT_HEIGHT, 2);

    if (flagAllowed(RESIZING)) {
      char const* resizingText = (flags & RESIZING) ? "Stop Resizing" : "Resize";
      if (nk_contextual_item_label(nk, resizingText, NK_TEXT_CENTERED)) {
        flags ^= RESIZING;
        resizeNode = d->node;
      }
    }

    if (flags & RESIZING) {
      goto contextualEnd;
    }

    if (nk_contextual_item_label(nk, "Remove", NK_TEXT_CENTERED)) {
      *BufAlloc(&removeNodes) = d->node;
    }

    if (type == NCOMMENT) {
      // not all nodes make sense to link
      goto contextualEnd;
    }

    // not unlinking and not linking to same node
    if (flagAllowed(LINKING) && !((flags & LINKING) && linkNode == d->node)) {
      int isSplit =
        type == NSPLIT &&
        nk_contextual_item_label(nk, "Link Split", NK_TEXT_CENTERED);

      int isNode = nk_contextual_item_label(nk, "Link", NK_TEXT_CENTERED);

      if (isSplit || isNode) {
        if (flags & LINKING) {
          if (isSplit) {
            // finish linking split->split or node->split
            d->value = linkNode;
          } else if (flags & LINKING_SPLIT) {
            // finish linking split->node
            data[NSPLIT][tree[linkNode].data].value = d->node;
          } else {
            // finish linking node->node
            treeLink(d->node, linkNode);
          }

          flags |= UPDATE_CONNECTIONS;

          if (!isSplit && !(flags & LINKING_SPLIT)) {
            int otherType = tree[linkNode].type;
            if ((type == NSPLIT && d->value == linkNode) ||
                (otherType == NSPLIT && data[NSPLIT][tree[linkNode].data].value == d->node)
            ) {
              error("the node is already linked as a split branch");
              treeUnlink(d->node, linkNode);
            }
          }
        } else {
          linkNode = d->node;
          if (isSplit) {
            // start linking split
            flags |= LINKING_SPLIT;
          } else {
            // start linking node
            flags &= ~LINKING_SPLIT;
          }
        }
        flags ^= LINKING;
      }
    }

    if (flagAllowed(UNLINKING) && nk_contextual_item_label(nk, "Un-Link", NK_TEXT_CENTERED)) {
      if (flags & UNLINKING) {
        treeUnlink(linkNode, d->node);

        // unlink split
        int otherType = tree[linkNode].type;
        NodeData* otherData = &data[NSPLIT][tree[linkNode].data];
        if (type == NSPLIT && d->value == linkNode) {
          d->value = -1;
        } else if (otherType == NSPLIT && otherData->value == d->node) {
          otherData->value = -1;
        }

        flags |= UPDATE_CONNECTIONS;
      } else {
        linkNode = d->node;
      }
      flags ^= UNLINKING;
    }
  } else if (selectedNode == d->node) {
    selectedNode = -1;
  }

contextualEnd:
  return showContextual;
}

void uiEndNode(int type, int i) {
  nk_group_end(nk);
}

void drawLink(struct nk_command_buffer* canvas,
              struct nk_vec2 from, struct nk_vec2 to, struct nk_color color, float thick) {
  if (from.x > to.x) {
    struct nk_vec2 tmp = from;
    from = to;
    to = tmp;
  }
  int offs = NK_MIN(200, to.x - from.x);
  nk_stroke_curve(canvas,
    from.x, from.y,
    from.x + offs, from.y,
    to.x - offs, to.y,
    to.x, to.y,
    thick, color
  );
}

void treeSetValue(NodeData* d, int newValue) {
  if (newValue != d->value) {
    // TODO: we could flag individual nodes as dirty instead of recalculating everything
    // for every value change even on unlinked nodes. however that would be a bit slower
    // since we would have to look up the node for every single value change.
    // also, it would create the complexity of having to propagate the dirty flag to all its
    // dependent nodes
    flags |= DIRTY;
    d->value = newValue;
  }
}

void treeCalcAppendWants(int id, int* values) {
  int key, value;
  // TODO: DRY
  if (values[NSTAT] == -1) {
    key = defaultValue(NSTAT, 0);
    dbg("(assumed) ");
  } else {
    key = values[NSTAT];
  }
  dbg("%s = ", lineNames[key]);
  if (values[NAMOUNT] == -1) {
    value = defaultValue(NAMOUNT, key);
    dbg("(assumed) ");
  } else {
    value = values[NAMOUNT];
  }
  dbg("%d\n", value);
  values[NSTAT] = -1;
  values[NAMOUNT] = -1;
  pyCalcWant(id, lineValues[key], value);
}

int treeCalcFinalizeWants(int id, int* values, int optionalCondition) {
  // append complete any pending line to wants, or just the default line
  if (optionalCondition || values[NSTAT] != -1 || values[NAMOUNT] != -1) {
    treeCalcAppendWants(id, values);
    return 1;
  }
  return 0;
}

int treeTypeToCalcOperator(int type) {
  switch (type) {
    case NOR: return calcoperatorValues[OR];
    case NAND: return calcoperatorValues[AND];
  }
  return 0;
}

int treeCalcBranch(int id, int* values, int node, int* seen) {
  if (seen[node]) {
    // it's important to not visit the same node twice so we don't get into a loop
    return 0;
  }

  seen[node] = 1;

  Node* n = &tree[node];
  NodeData* d = &data[n->type][n->data];

  // TODO: flatten this so we don't use recursion
  if (n->type == NSPLIT) {
    if (d->value >= 0) {
      // in the case of a split, we only take the parent so that we don't traverse the other
      // branches. that's the whole point of this node. to allow reusing common nodes
      return treeCalcBranch(id, values, d->value, seen);
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
    int branchElementsOnStack = treeCalcBranch(id, values, n->connections[i], seen);
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

int humanizeSnprintf(i64 x, char* buf, size_t sz, char const* suff) {
  return snprintf(buf, sz, "%" PRId64 "%s ", x, suff);
}

int humanizeSnprintfWithDot(double x, char* buf, size_t sz, char const* suff) {
  i64 mod = (i64)(x * 10) % 10;
  if (mod) {
    return snprintf(buf, sz, "%" PRId64 ".%" PRId64 "%s ", (i64)x, mod, suff);
  }
  return humanizeSnprintf((i64)x, buf, sz, suff);
}

int humanizeStepWithDot(i64 mag, char const* suff, char* buf, size_t sz, i64 value) {
  if (value >= mag) {
    double x = value / (double)mag;
    int n = humanizeSnprintfWithDot(x, 0, 0, suff);
    if (n >= sz) {
      snprintf(buf, sz, "...");
    } else {
      humanizeSnprintfWithDot(x, buf, sz, suff);
    }
    return n;
  }
  return 0;
}

void humanize(char* buf, size_t sz, i64 value) {
  const i64 k = 1000;
  const i64 m = k * k;
  const i64 b = k * m;
  const i64 t = k * b;
  const i64 q = k * t;

  if (value < k) {
    snprintf(buf, sz, "%" PRId64, value);
    return;
  }

  if (value >= q * k) {
    if (snprintf(buf, sz, "%.2e", (double)value) >= sz) {
      snprintf(buf, sz, "(too big)");
    }
    return;
  }

  humanizeStepWithDot(q, "q", buf, sz, value) ||
  humanizeStepWithDot(t, "t", buf, sz, value) ||
  humanizeStepWithDot(b, "b", buf, sz, value) ||
  humanizeStepWithDot(m, "m", buf, sz, value) ||
  humanizeStepWithDot(k, "k", buf, sz, value);
}

void treeCalc() {
  for (size_t i = 0; i < BufLen(tree); ++i) {
    Node* n = &tree[i];
    // TODO: more type of result nodes (median, 75%, 85%, etc)
    if (n->type == NRESULT) {
      NodeData* d = &data[n->type][n->data];
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
      BufReserve(&seen, BufLen(tree));
      BufZero(seen);
      int elementsOnStack = treeCalcBranch(n->id, values, i, seen);
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
          values[j] = defaultValue(j, values[CATEGORY]);
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
      Result* resd = &resultData[n->data];
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

#ifdef __EMSCRIPTEN__
void updateWindowSize() {
  int w, h;
  w = canvas_get_width();
  h = canvas_get_height();
  if (w != width || h != height) {
    width = w;
    height = h;
    glfwSetWindowSize(win, width, height);
    flags |= UPDATE_SIZE;
  }
  glfwGetFramebufferSize(win, &w, &h);
  if (w != displayWidth || h != displayHeight) {
    flags |= UPDATE_SIZE;
    displayWidth = w;
    displayHeight = h;
  }
}
#endif

void uiEmptyNode(int type) {
  for (size_t i = 0; i < BufLen(data[type]); ++i) {
    if (uiBeginNode(type, i, 20)) {
      uiEndNode(type, i);
    }
    if (uiContextual(type, i)) {
      nk_contextual_end(nk);
    }
  }
}

void loop() {
  glfwPollEvents();
  nk_glfw3_new_frame();

  // TODO: remove this
  if (!atomic_load(&storageReady)) {
    goto dontShowCalc;
  }

  if (flags & SHOW_DISCLAIMER) {
    goto dontShowCalc;
  }

  if (nk_begin(nk, CALC_NAME, calcBounds, CALCWND)) {
    struct nk_command_buffer* canvas = nk_window_get_canvas(nk);
    struct nk_rect totalSpace = nk_window_get_content_region(nk);

    nk_layout_space_begin(nk, NK_STATIC, totalSpace.h, BufLen(tree));
    nk_fill_rect(canvas, totalSpace, 0, nk_rgb(10, 10, 10));

    if (flags & SHOW_GRID) {
      struct nk_rect bnds = nk_layout_space_bounds(nk);
      float x, y;
      const float gridSize = 32.0f;
      const struct nk_color gridColor = nk_rgb(30, 30, 30);
      for (x = (float)fmod(-pan.x, gridSize); x < bnds.w; x += gridSize) {
        nk_stroke_line(canvas, x + bnds.x, bnds.y, x + bnds.x, bnds.y + bnds.h, 1.0f, gridColor);
      }
      for (y = (float)fmod(-pan.y, gridSize); y < bnds.h; y += gridSize) {
        nk_stroke_line(canvas, bnds.x, y + bnds.y, bnds.x + bnds.w, y + bnds.y, 1.0f, gridColor);
      }
    }

    // draw links BEFORE the nodes so they appear below them
    BufEach(NodeLink, links, l) {
      struct nk_vec2 from = nodeSpaceToScreen(nodeCenter(l->from));
      struct nk_vec2 to = nodeSpaceToScreen(nodeCenter(l->to));
      drawLink(canvas, from, to, l->color, l->thick);
    }

    if (flags & (LINKING | UNLINKING)) {
      struct nk_vec2 from = nodeSpaceToScreen(nodeCenter(linkNode));
      struct nk_vec2 to = in->mouse.pos;
      drawLink(canvas, from, to,
               (flags & LINKING) ? nk_rgb(200, 200, 255) : nk_rgb(255, 200, 200), 2);
    }

    if (flags & RESIZING) {
      flags |= RESIZING;
      Node* n = &tree[resizeNode];
      NodeData* d = &data[n->type][n->data];
      struct nk_vec2 m = nk_layout_space_to_local(nk, in->mouse.pos);
      d->bounds.w = NK_MAX(100, m.x - d->bounds.x + pan.x);
      d->bounds.h = NK_MAX(50, m.y - d->bounds.y + pan.y);
    }

#define comboNode(type, enumName) \
  BufEachi(data[type], i) { \
    if (uiBeginNode(type, i, 25)) { \
      NodeData* d = &data[type][i]; \
      int newValue = nk_combo(nk, enumName##Names, NK_LEN(enumName##Names), d->value, \
        25, nk_vec2(nk_widget_width(nk), 100)); \
      treeSetValue(d, newValue); \
      uiEndNode(type, i); \
    } \
    if (uiContextual(type, i)) { \
      nk_contextual_end(nk); \
    } \
  }

#define propNode(type, valueType) \
  BufEachi(data[type], i) { \
    if (uiBeginNode(type, i, 20)) { \
      NodeData* d = &data[type][i]; \
      int newValue = nk_property##valueType(nk, d->name, 0, d->value, 300, 1, 0.02); \
      treeSetValue(d, newValue); \
      uiEndNode(type, i); \
    } \
    if (uiContextual(type, i)) { \
      nk_contextual_end(nk); \
    } \
  }

    BufEachi(data[NCOMMENT], i) {
      NodeData* d = &data[NCOMMENT][i];
      struct nk_rect bounds = commentBounds(d->bounds);
      const struct nk_color color = nk_rgb(255, 255, 128);
      nk_stroke_rect(canvas, nk_layout_space_rect_to_screen(nk, bounds),
                     COMMENT_ROUND, COMMENT_THICK, color);
      if (uiBeginNode(NCOMMENT, i, COMMENT_H)) {
        bounds.x -= pan.x;
        bounds.y -= pan.y;

        Comment* com = &commentData[i];
        nk_edit_string(nk, NK_EDIT_FIELD, com->buf, &com->len, COMMENT_MAX, 0);
        uiEndNode(NCOMMENT, i);
      }
      if (uiContextual(NCOMMENT, i)) {
        nk_contextual_end(nk);
      }
    }

    uiEmptyNode(NSPLIT);
    uiEmptyNode(NOR);
    uiEmptyNode(NAND);

    comboNode(NCUBE, cube);
    comboNode(NTIER, tier);
    comboNode(NCATEGORY, category);
    comboNode(NSTAT, line);
    comboNode(NREGION, region);
    propNode(NAMOUNT, i);
    propNode(NLEVEL, i);

    BufEachi(data[NRESULT], i) {
      if (uiBeginNode(NRESULT, i, 10)) {
#define l(text, x) \
  nk_label(nk, text, NK_TEXT_RIGHT); \
  nk_label(nk, *resultData[i].x ? resultData[i].x : "impossible", NK_TEXT_LEFT)
#define q(n) l(#n "% within:", within##n)

        nk_layout_row_template_begin(nk, 10);
        nk_layout_row_template_push_static(nk, 90);
        nk_layout_row_template_push_dynamic(nk);
        nk_layout_row_template_end(nk);
        l("average 1 in:", average);
        q(50); q(75); q(95); q(99);
        l("combos:", numCombosStr);

        Result* r = &resultData[i];
        if (!r->comboLen) goto terminateNode;

        nk_layout_row_dynamic(nk, 10, 1);
        nk_spacer(nk);
        nk_layout_row_dynamic(nk, 20, 1);

        int newPerPage = nk_propertyi(nk, "combos per page", 0, r->perPage, 10000, 1, 0.02);
        if (newPerPage != r->perPage) {
          if (newPerPage && !r->perPage) {
            flags |= DIRTY;
          }
          r->perPage = newPerPage;
          r->perPage = NK_MAX(0, NK_MIN(r->perPage, 10000));
        }

        if (!r->perPage || !r->comboLen) goto terminateNode;

        int totalCombos = BufLen(r->line) / r->comboLen;
        int totalPages = (totalCombos + r->perPage - 1) / r->perPage;
        const int cs = 7;

        nk_layout_row_template_begin(nk, 20);
        nk_layout_row_template_push_dynamic(nk);
        nk_layout_row_template_push_static(nk, cs * 4);
        nk_layout_row_template_end(nk);

        r->page = nk_propertyi(nk, "combos page", 1, r->page, totalPages, 1, 0.02);
        r->page = NK_MAX(1, NK_MIN(r->page, totalPages));
        nk_labelf(nk, NK_TEXT_LEFT, "/%d", totalPages);
        nk_spacer(nk);
        nk_spacer(nk);

        nk_layout_row_template_begin(nk, 10);
        nk_layout_row_template_push_static(nk, cs * 2);
        nk_layout_row_template_push_static(nk, cs * 3);
        //nk_layout_row_template_push_static(nk, MAX_LINENAME * cs);
        nk_layout_row_template_push_dynamic(nk);
        nk_layout_row_template_push_static(nk, cs * 7);
        nk_layout_row_template_end(nk);

        nk_spacer(nk);
        nk_spacer(nk);
        nk_label(nk, "line", NK_TEXT_LEFT);
        nk_label(nk, "1 in", NK_TEXT_RIGHT);

        int pagei = (r->page - 1) * r->perPage;
        for (int pi = pagei; pi < NK_MIN(pagei + r->perPage, totalCombos); ++pi) {
          for (int j = 0; j < r->comboLen; ++j) {
            int k = pi * r->comboLen + j;
            static char const* primestr[2] = { "", "P" };
            nk_label(nk, primestr[r->prime[k]], NK_TEXT_LEFT);
            nk_label(nk, r->value[k], NK_TEXT_RIGHT);
            nk_label(nk, allLineNames[Log2i64(r->line[k])], NK_TEXT_LEFT);
            nk_label(nk, r->prob[k], NK_TEXT_RIGHT);
          }

          nk_spacer(nk);
          nk_spacer(nk);
          nk_spacer(nk);
          nk_spacer(nk);
        }

#undef l
#undef q

terminateNode:
        uiEndNode(NRESULT, i);
      }

      if (uiContextual(NRESULT, i)) {
        nk_contextual_end(nk);
      }
    }

    // draw rounded border around selected node
    if (selectedNode >= 0) {
      const struct nk_color selColor = nk_rgb(128, 255, 128);
      Node* sn = &tree[selectedNode];
      NodeData* sd = &data[sn->type][sn->data];
      struct nk_rect sbounds = commentBounds(sd->bounds);
      nk_stroke_rect(canvas, nk_layout_space_rect_to_screen(nk, sbounds),
                       COMMENT_ROUND, COMMENT_THICK, selColor);
    }

    // we can't do this from the contextual menu code because it will be in a different local
    // space and it won't convert properly
    struct nk_vec2 mouse = nk_layout_space_to_local(nk, in->mouse.pos);

    nk_layout_space_end(nk);

    // it really isn't necessary to handle multiple deletions right now, but why not.
    // maybe eventually I will have a select function and will want to do this
    BufEach(int, removeNodes, nodeIndex) {
      treeDel(*nodeIndex);
      if (selectedNode == *nodeIndex) {
        selectedNode = -1;
      }
      BufEach(int, removeNodes, otherIndex) {
        if (*otherIndex > *nodeIndex) {
          --*otherIndex;
        }
      }
    }
    BufClear(removeNodes);

    if (nk_contextual_begin(nk, 0, contextualSize, totalSpace)) {
      if (!(flags & SAVED_MOUSE_POS)) {
        flags |= SAVED_MOUSE_POS;
        savedMousePos = mouse;
        savedMousePos.x += pan.x;
        savedMousePos.y += pan.y;
      }

      int activeFlags = 0;

#define flagDisable(x) \
      if ((flags & x)) { \
        nk_layout_row_dynamic(nk, CONTEXT_HEIGHT, 1); \
        activeFlags |= x; \
        if (nk_contextual_item_label(nk, "Stop " #x, NK_TEXT_CENTERED)) { \
          flags &= ~x; \
        } \
      } \

      flagDisable(RESIZING)
      flagDisable(LINKING)
      flagDisable(UNLINKING)

      if (!activeFlags) {
        nk_layout_row_dynamic(nk, CONTEXT_HEIGHT, 2);
        for (size_t i = 0; i < NK_LEN(nodeNames); ++i) {
          if (nk_contextual_item_label(nk, nodeNames[i], NK_TEXT_CENTERED)) {
            treeAdd(i + 1, savedMousePos.x, savedMousePos.y);
          }
        }
        nk_layout_row_dynamic(nk, CONTEXT_HEIGHT, 1);
      }

#define flag(x, text, f) ( \
      nk_contextual_item_label(nk, (flags & x) ? "Hide " text : "Show " text, NK_TEXT_CENTERED) &&\
        (flags = (flags ^ x) | f))

      flag(SHOW_INFO, "Info", UPDATE_SIZE);
      flag(SHOW_GRID, "Grid", 0);
      flag(SHOW_DISCLAIMER, "Disclaimer", 0);

      if (flag(DEBUG, "Debug in Console", 0)) {
        pyCalcDebug(flags & DEBUG);
      }

      if (nk_contextual_item_label(nk, "I'm Lost", NK_TEXT_CENTERED)) {
        if (BufLen(tree)) {
          NodeData* n = &data[tree[0].type][tree[0].data];
          pan = nk_rect_pos(n->bounds);
          pan.x -= totalSpace.w / 2 - n->bounds.w / 2;
          pan.y -= totalSpace.h / 2 - n->bounds.h / 2;
        }
      }

      nk_contextual_end(nk);
    } else {
      flags &= ~SAVED_MOUSE_POS;
    }

    nk_layout_space_end(nk);

    if (nk_input_is_mouse_hovering_rect(in, nk_window_get_bounds(nk)) &&
        nk_input_is_mouse_down(in, NK_BUTTON_MIDDLE)) {
      pan.x -= in->mouse.delta.x;
      pan.y -= in->mouse.delta.y;
    }
  }
  nk_end(nk);

  if (flags & DIRTY) {
    treeCalc();
    flags &= ~DIRTY;
  }

  if (flags & UPDATE_CONNECTIONS) {
    treeUpdateConnections();
    flags &= ~UPDATE_CONNECTIONS;
  }

  if (flags & SHOW_INFO) {
    if (nk_begin(nk, INFO_NAME, infoBounds, OTHERWND)) {
      if (flags & PORTRAIT) {
        nk_layout_row_static(nk, 20, NK_MAX(360, nk_widget_width(nk)), 5);
      } else {
        nk_layout_row_dynamic(nk, 20, 1);
      }
      float sf = nk_propertyf(nk, "Scale", 1, glfw.scale_factor, 2, 0.1, 0.02);
      if (sf != glfw.scale_factor) {
        glfw.scale_factor = sf;
        flags |= UPDATE_SIZE;
      }
      int newMaxCombos = nk_propertyi(nk, "Max Combos", 0, maxCombos, 500, 100, 0.02);
      if (newMaxCombos != maxCombos) {
        maxCombos = newMaxCombos;
        flags |= DIRTY;
      }
      nk_label(nk, *statusText ? statusText : "Idle", NK_TEXT_LEFT);
      if (flags & FULL_INFO) {
        if (flags & PORTRAIT) {
          nk_layout_row_static(nk, 20, NK_MAX(180, nk_widget_width(nk) / 2), 2);
        } else {
          nk_spacer(nk);
        }
        nk_label(nk, "pan: middle drag", NK_TEXT_LEFT);
        nk_label(nk, "move nodes: left drag", NK_TEXT_LEFT);
        nk_label(nk, "node actions: rclick", NK_TEXT_LEFT);
        nk_label(nk, "add nodes: rclick space", NK_TEXT_LEFT);

#define TOOL1 "tool "
#define TOOL2 "(override lclick for devs "
#define TOOL3 "w/ limited mouse support)"

        if (flags & PORTRAIT) {
          nk_layout_row_dynamic(nk, 20, 1);
          nk_label(nk, TOOL1 TOOL2 TOOL3, NK_TEXT_LEFT);
        } else {
          nk_spacer(nk);
          nk_label(nk, TOOL1, NK_TEXT_LEFT);
          nk_label(nk, TOOL2, NK_TEXT_LEFT);
          nk_label(nk, TOOL3, NK_TEXT_LEFT);
        }
      }

      nk_layout_row_dynamic(nk, 20, (flags & PORTRAIT) ? 3 : 1);

      int newTool = tool;

      for (int curTool = 0; curTool < NK_LEN(toolNames); ++curTool) {
        if (tool == curTool) {
          nk_label(nk, toolNames[curTool], NK_TEXT_CENTERED);
        } else {
          if (nk_button_label(nk, toolNames[curTool])) {
            newTool = curTool;
          }
        }
      }

      if (newTool != tool) {
        tool = newTool;
        glfw.left_button = toolToMouseButton[tool];
      }

      if (!(flags & PORTRAIT)) {
        nk_spacer(nk);
        nk_layout_row_dynamic(nk, 10, 1);
        nk_spacer(nk);
      }

      int activeFlags = 0;

#define showFlag(x) \
      if (flags & x) { \
        nk_label(nk, #x "...", NK_TEXT_CENTERED); \
        activeFlags |= x; \
      }

      showFlag(LINKING)
      showFlag(UNLINKING)
      showFlag(RESIZING)

      if (!activeFlags) {
        nk_value_int(nk, "FPS", fps);
        nk_value_int(nk, "Nodes", BufLen(tree));
        nk_value_int(nk, "Links", BufLen(links));
      }

      if (!(flags & PORTRAIT)) {
        nk_layout_row_dynamic(nk, 20, 1);
      }

      if (activeFlags && (
            glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS ||
            nk_button_label(nk, "esc/ctx to cancel")
          )) {
        flags &= ~activeFlags;
      }
    } else {
      flags &= ~SHOW_INFO;
      flags |= UPDATE_SIZE;
    }
    nk_end(nk);
  }

dontShowCalc:
  if (flags & SHOW_DISCLAIMER) {
    if (nk_begin(nk, DISCLAIMER_NAME, disclaimerBounds, OTHERWND)) {
      nk_layout_row_dynamic(nk, 20, 1);
      if (nk_button_label(nk, "I understand")) {
        flags &= ~SHOW_DISCLAIMER;
      }
      nk_layout_row_static(nk, disclaimerHeight, NK_MAX(570, nk_widget_width(nk)), 1);
      static int disclaimerLen = -1;
      if (disclaimerLen < 0) {
        disclaimerLen = strlen(disclaimer);
      }
      nk_edit_string(nk, NK_EDIT_BOX | NK_EDIT_READ_ONLY,
                     disclaimer, &disclaimerLen, disclaimerLen + 1, 0);
    } else {
      flags &= ~SHOW_DISCLAIMER;
    }
    nk_end(nk);
  }

  if (BufLen(errors)) {
    if (nk_begin(nk, ERROR_NAME, errorBounds, OTHERWND)){
      nk_layout_row_dynamic(nk, 10, 1);
      BufEach(char*, errors, perr) {
        nk_label(nk, *perr, NK_TEXT_LEFT);
      }
    } else {
      BufClear(errors);
    }
    nk_end(nk);
  }

#ifdef __EMSCRIPTEN__
  updateWindowSize();
#else
  glfwGetWindowSize(win, &width, &height);
#endif

  if (flags & UPDATE_SIZE) {
    calcBounds.x = calcBounds.y = 0;
    calcBounds.w = width / glfw.scale_factor;
    calcBounds.h = height / glfw.scale_factor;
    disclaimerBounds.w = NK_MIN(calcBounds.w, 610);
    disclaimerBounds.h = NK_MIN(calcBounds.h, 370);
    disclaimerBounds.x = calcBounds.w / 2 - disclaimerBounds.w / 2 + 10;
    disclaimerBounds.y = calcBounds.h / 2 - disclaimerBounds.h / 2 + 10;
    errorBounds.x = calcBounds.w / 2 - 200 + 10;
    errorBounds.y = calcBounds.h / 2 - 100 + 10;
    errorBounds.w = 400;
    errorBounds.h = 200;
    calcBounds.w = NK_MAX(50, calcBounds.w);
    calcBounds.h = NK_MAX(50, calcBounds.h);
    if (calcBounds.h >= 500) {
      flags |= FULL_INFO;
    } else {
      flags &= ~FULL_INFO;
    }
    if (calcBounds.w - 210 > calcBounds.h) {
      if (flags & SHOW_INFO) calcBounds.w -= 210;
      infoBounds.x = calcBounds.w;
      infoBounds.y = 0;
      infoBounds.w = 200;
      infoBounds.h = calcBounds.h;
      flags &= ~PORTRAIT;
    } else {
      if (flags & SHOW_INFO) calcBounds.h -= (flags & FULL_INFO) ? 200 : 120;
      infoBounds.x = 0;
      infoBounds.y = calcBounds.h;
      infoBounds.w = calcBounds.w;
      infoBounds.h = (flags & FULL_INFO) ? 200 : 120;
      flags |= PORTRAIT;
    }
    nk_window_set_bounds(nk, CALC_NAME, calcBounds);
    nk_window_set_bounds(nk, DISCLAIMER_NAME, disclaimerBounds);
    nk_window_set_bounds(nk, ERROR_NAME, errorBounds);
    nk_window_set_bounds(nk, INFO_NAME, infoBounds);
    nk_window_set_focus(nk, DISCLAIMER_NAME);
    flags &= ~UPDATE_SIZE;
  }

  glViewport(0, 0, width, height);
  glClear(GL_COLOR_BUFFER_BIT);
  nk_glfw3_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY);
  glfwSwapBuffers(win);

  updateFPS();
}

int treeAddChk(struct nk_vec2 start, int type, int x, int y, int* succ) {
  int res = treeAdd(type, start.x + x, start.y + y);
  *succ = *succ && res >= 0;
  return res;
}

int treeAddComment(struct nk_vec2 start, int x, int y, int w, int h, char* text, int* succ) {
  int ncomment = treeAddChk(start, NCOMMENT, x, y, succ);
  int i = tree[ncomment].data;
  NodeData* d = &data[NCOMMENT][i];
  Comment* cd = &commentData[i];
  d->bounds.w = w;
  d->bounds.h = h;
  snprintf(cd->buf, COMMENT_MAX - 1, "%s", text);
  cd->len = strlen(cd->buf);
  return ncomment;
}

void examplesInit() {
  int succ = 1;

  struct nk_vec2 s = nk_vec2(20, 20);
  int ncategory = treeAddChk(s, NCATEGORY, 0, 0, &succ);
  int ncube = treeAddChk(s, NCUBE, 320, 0, &succ);
  int ntier = treeAddChk(s, NTIER, 540, 0, &succ);
  int nlevel = treeAddChk(s, NLEVEL, 750, 0, &succ);
  int nregion = treeAddChk(s, NREGION, 750, 90, &succ);
  int nsplit = treeAddChk(s, NSPLIT, 670, 90, &succ);

  if (succ) {
    data[NSPLIT][tree[nsplit].data].value = ntier;
    data[NLEVEL][tree[nlevel].data].value = 200;
    treeLink(ncategory, ncube);
    treeLink(ncube, ntier);
    treeLink(nlevel, ntier);
    treeLink(nlevel, nregion);
  }

  int nprevres;
  {
    s = nk_vec2(20 - 210, 130);
    int ncomment = treeAddComment(s, 0, 0, 410, 310, "example: 23+ %att", &succ);
    int nstat = treeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = treeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nres = nprevres = treeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      data[NAMOUNT][tree[namt].data].value = 23;
      data[NRESULT][tree[nres].data].bounds.h = 260;
      resultData[tree[nres].data].perPage = 100;
      treeLink(nsplit, nstat);
      treeLink(nstat, namt);
      treeLink(namt, nres);
    }
  }

  {
    s = nk_vec2(20, 480);
    int ncomment = treeAddComment(s, 0, 0, 200, 220, "example: bpot 23+ %att", &succ);
    int nbpot = treeAddChk(s, NCUBE, 0, 50, &succ);
    int nres = treeAddChk(s, NRESULT, 0, 140, &succ);
    int nsplit2 = treeAddChk(s, NSPLIT, -100, -20, &succ);

    if (succ) {
      data[NCUBE][tree[nbpot].data].value = BONUS;
      data[NSPLIT][tree[nsplit2].data].value = nprevres;
      treeLink(nbpot, nsplit2);
      treeLink(nbpot, nres);
    }
  }

  {
    s = nk_vec2(260, 130);
    int ncomment = treeAddComment(s, 0, 0, 410, 310, "example: 20+ %att and 30+ %boss", &succ);
    int nstat2 = treeAddChk(s, NSTAT, 0, 50, &succ);
    int namt2 = treeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nstat = treeAddChk(s, NSTAT, 210, 50, &succ);
    int namt = treeAddChk(s, NAMOUNT, 210, 140, &succ);
    int nres = treeAddChk(s, NRESULT, 210, 230, &succ);

    if (succ) {
      data[NSTAT][tree[nstat2].data].value = BOSS;
      data[NAMOUNT][tree[namt2].data].value = defaultValue(NAMOUNT, BOSS);
      data[NAMOUNT][tree[namt].data].value = 20;
      treeLink(nsplit, nstat);
      treeLink(nstat, namt);
      treeLink(nstat2, namt2);
      treeLink(namt, nres);
      treeLink(namt2, nres);
    }
  }

  {
    s = nk_vec2(260, 480);
    int ncomment = treeAddComment(s, 0, 0, 410, 310,
        "example: any 3l combo of %att or %boss", &succ);
    int nstat2 = treeAddChk(s, NSTAT, 0, 50, &succ);
    int namt2 = treeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nstat3 = treeAddChk(s, NSTAT, 210, 140, &succ);
    int nstat = treeAddChk(s, NSTAT, 210, 50, &succ);
    int nres = treeAddChk(s, NRESULT, 210, 230, &succ);

    if (succ) {
      data[NSTAT][tree[nstat2].data].value = BOSS_ONLY;
      data[NSTAT][tree[nstat3].data].value = LINES;
      data[NAMOUNT][tree[namt2].data].value = 3;
      treeLink(nsplit, nstat);
      treeLink(nstat, nstat3);
      treeLink(nstat, nstat2);
      treeLink(nstat3, namt2);
      treeLink(namt2, nres);
    }
  }

  {
    s = nk_vec2(260, 830);
    struct nk_vec2 s0 = s;
    int nmeso = treeAddChk(s, NSTAT, 0, 50, &succ);
    int ndrop = treeAddChk(s, NSTAT, 210, 50, &succ);
    int nor = treeAddChk(s, NOR, 210 + 210 / 2 - 80 / 2, 140, &succ);
    s.y += 140 + 40;
    int n23stat = treeAddChk(s, NSTAT, 0, 0, &succ);
    int n9stat = treeAddChk(s, NSTAT, 210, 0, &succ);
    s.y += 90;
    int n23amt = treeAddChk(s, NAMOUNT, 0, 0, &succ);
    int n9amt = treeAddChk(s, NAMOUNT, 210, 0, &succ);
    s.y += 90;
    int nor2 = treeAddChk(s, NOR, 210 - 80 / 2, 0, &succ);
    s.y += 60;
    int nres = treeAddChk(s, NRESULT, 210/2, 0, &succ);
    int ncat = treeAddChk(s, NCATEGORY, -340, 0, &succ);
    s.y += 90;
    int ncomment = treeAddComment(s0, 0, 0, 410, s.y - s0.y,
        "example: ((meso or drop) and 10+ stat) or 23+ stat", &succ);

    if (succ) {
      data[NSTAT][tree[n23stat].data].value = data[NSTAT][tree[n9stat].data].value = STAT;
      data[NAMOUNT][tree[n23amt].data].value = 23;
      data[NAMOUNT][tree[n9amt].data].value = 10;
      data[NSTAT][tree[ndrop].data].value = DROP;
      data[NSTAT][tree[nmeso].data].value = MESO;
      data[NCATEGORY][tree[ncat].data].value = FACE_EYE_RING_EARRING_PENDANT;

      treeLink(ndrop, nor);
      treeLink(nmeso, nor);
      treeLink(nor, n9stat);
      treeLink(n9stat, n9amt);
      treeLink(n9amt, nor2);

      treeLink(n23stat, n23amt);
      treeLink(n23amt, nor2);

      treeLink(nor2, nres);

      treeLink(nsplit, ncat);
      treeLink(ncat, nres);
    }
  }

  {
    s = nk_vec2(710, 220);
    int ncomment = treeAddComment(s, 0, 0, 410, 400, "example: unique fam 30+ boss reveal", &succ);
    int nfamcat = treeAddChk(s, NCATEGORY, 0, 50, &succ);
    int nstat = treeAddChk(s, NSTAT, 0, 140, &succ);
    int namt = treeAddChk(s, NAMOUNT, 0, 230, &succ);
    int nfamtier = treeAddChk(s, NTIER, 210, 140, &succ);
    int nfamcube = treeAddChk(s, NCUBE, 210, 230, &succ);
    int nres = nprevres = treeAddChk(s, NRESULT, 0, 320, &succ);

    if (succ) {
      data[NAMOUNT][tree[namt].data].value = 30;
      data[NCATEGORY][tree[nfamcat].data].value = FAMILIAR_STATS;
      data[NCUBE][tree[nfamcube].data].value = FAMILIAR;
      data[NTIER][tree[nfamtier].data].value = UNIQUE;
      data[NSTAT][tree[nstat].data].value = BOSS;
      treeLink(nfamcat, nstat);
      treeLink(nstat, namt);
      treeLink(nfamcube, namt);
      treeLink(nfamtier, nfamcube);
      treeLink(nfamcube, nres);
    }
  }

  {
    s = nk_vec2(710, 660);
    int ncomment = treeAddComment(s, 0, 0, 410, 310, "example: red cards 40+ boss", &succ);
    int nstat = treeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = treeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nfamtier = treeAddChk(s, NTIER, 210, 50, &succ);
    int nfamcube = treeAddChk(s, NCUBE, 210, 140, &succ);
    int nres = treeAddChk(s, NRESULT, 0, 230, &succ);
    int nsplit = treeAddChk(s, NSPLIT, 290, -100, &succ);

    if (succ) {
      data[NAMOUNT][tree[namt].data].value = 40;
      data[NCUBE][tree[nfamcube].data].value = RED_FAM_CARD;
      data[NTIER][tree[nfamtier].data].value = LEGENDARY;
      data[NSTAT][tree[nstat].data].value = BOSS;
      data[NSPLIT][tree[nsplit].data].value = nprevres;
      treeLink(nsplit, nstat);
      treeLink(nstat, namt);
      treeLink(nfamcube, namt);
      treeLink(nfamtier, nfamcube);
      treeLink(nfamcube, nres);
    }
  }
}

int SavedConnectionEqual(SavedConnection* a, SavedConnection* b) {
  return (
    (a->fromid == b->fromid && a->toid == b->  toid) ||
    (a->fromid == b->  toid && a->toid == b->fromid)
  );
}

// convert a Buf of structs to a Buf of pointers to the structs (no copying)
// this is useless normally, it's mainly for protobuf

#define BufToProto(protoStruc, member, b) \
  (protoStruc)->member = BufToProto_(b, &(protoStruc)->n_##member, &allocatorDefault)

void* BufToProto_(void* b, size_t *pn, Allocator* allocator) {
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

int storageSaveSync(char* path) {
  dbg("saving %s\n", path);

  Arena arena = ArenaInitializer;
  Allocator allocatorArena = ArenaAllocator(&arena);

// override default allocator to use this memory arena so I don't have to manually free
// all the arrays

#undef allocatorDefault
#define allocatorDefault allocatorArena

  SavedNode* savedTree = 0;
  SavedNodeData* savedData = 0;
  SavedRect* savedRects = 0;

  size_t treeLen = BufLen(tree);
  BufReserve(&savedTree, treeLen);
  BufReserve(&savedData, treeLen);
  BufReserve(&savedRects, treeLen);

  SavedComment* savedCommentData = 0;
  BufReserve(&savedCommentData, BufLen(commentData));

  SavedResult* savedResultData = 0;
  BufReserve(&savedResultData, BufLen(resultData));

  size_t numConnections = 0;

  // first, convert everything into serialized structs, with no care for buckets
  BufEachi(tree, i) {
    Node* n = &tree[i];
    NodeData* d = &data[n->type][n->data];
    numConnections += BufLen(n->connections);

    SavedRect* sr = &savedRects[i];
    saved_rect__init(sr);
    sr->x = d->bounds.x;
    sr->y = d->bounds.y;
    sr->w = d->bounds.w;
    sr->h = d->bounds.h;

    SavedNodeData* sd = &savedData[i];
    saved_node_data__init(sd);
    sd->bounds = sr;

    if (n->type == NSPLIT) {
      sd->value = tree[d->value].id;
    } else {
      sd->value = d->value;
    }

    SavedNode* sn = &savedTree[i];
    saved_node__init(sn);
    sn->id = n->id;
    sn->data = sd;

    switch (n->type) {
      case NCOMMENT: {
        Comment* c = &commentData[n->data];
        SavedComment* sc = &savedCommentData[n->data];
        saved_comment__init(sc);
        sc->text = BufStrDupn(c->buf, c->len);

        sn->commentdata = sc;
        break;
      }
      case NRESULT: {
        Result* r = &resultData[n->data];
        SavedResult* sr = &savedResultData[n->data];
        saved_result__init(sr);
        sr->page = r->page;
        sr->perpage = r->perPage;
        sn->resultdata = sr;
        break;
      }
    }
  }

  // now we figure out buckets and connections and copy to bucket arrays
  SavedBucket* buckets = 0;
  BufReserve(&buckets, NLAST); // make sure we don't trigger realloc
  BufClear(buckets);

  SavedConnection* connections = 0;
  BufReserve(&connections, numConnections);
  BufClear(connections);

  for (size_t type = 0; type < NLAST; ++type) {
    size_t bucketLen = BufLen(data[type]);
    if (!bucketLen) continue;

    SavedNode* savedNodes = 0;
    BufReserve(&savedNodes, bucketLen);

    BufEachi(data[type], i) {
      NodeData* d = &data[type][i];
      Node* n = &tree[d->node];
      savedNodes[i] = savedTree[d->node];

      BufEach(int, n->connections, conn) {
        Node* to = &tree[*conn];
        SavedConnection* sc = BufAlloc(&connections);
        saved_connection__init(sc);
        sc->fromid = n->id;
        sc->toid = to->id;
      }
    }

    SavedBucket* buck = BufAlloc(&buckets);
    saved_bucket__init(buck);
    #define nodeTypeToSavedEntry(x) [x] = SAVED_NODE_TYPE__##x,
    int const nodeTypeToSaved[] = { nodeTypes(nodeTypeToSavedEntry) };
    buck->type = nodeTypeToSaved[type];
    BufToProto(buck, nodes, savedNodes);
  }

  // prune redundant connections
  SavedConnection* uniqueConnections = 0;
  BufReserve(&uniqueConnections, numConnections);
  BufClear(uniqueConnections);

  BufEach(SavedConnection, connections, conn) {
    BufEach(SavedConnection, uniqueConnections, uniq) {
      if (SavedConnectionEqual(conn, uniq)) {
        goto nextConn;
      }
    }

    *BufAlloc(&uniqueConnections) = *conn;
nextConn:;
  }

  SavedPreset preset;
  saved_preset__init(&preset);
  BufToProto(&preset, buckets, buckets);
  BufToProto(&preset, connections, uniqueConnections);

  u8* out = 0;
  BufReserve(&out, saved_preset__get_packed_size(&preset));
  saved_preset__pack(&preset, out);

  FILE* f = fopen(path, "wb");
  if (!f) {
    perror("fopen");
  } else {
    if (fwrite(out, 1, BufLen(out), f) != BufLen(out)) {
      perror("fwrite");
    }
    fclose(f);
  }

  ArenaFree(&arena);

// restore default allocator
#undef allocatorDefault
#define allocatorDefault allocatorDefault_

  return 1;
}

int storageLoadSync(char* path) {
  struct stat st;

  dbg("loading %s\n", path);

  if (stat(path, &st)) {
    perror("stat");
    return 0;
  }

  FILE* f = fopen(path, "rb");
  if (!f) {
    perror("fopen");
    return 0;
  }

  int res = 0;
  SavedPreset* preset = 0;
  int* treeById = 0;

  u8* rawData = 0;
  BufReserve(&rawData, st.st_size);
  if (fread(rawData, 1, st.st_size, f) != st.st_size) {
    perror("fread");
    goto cleanupData;
  }
  treeClear();

  preset = saved_preset__unpack(0, st.st_size, rawData);

cleanupData:
  fclose(f);
  BufFree(&rawData);

  if (!preset) {
    fprintf(stderr, "missing preset\n");
    goto cleanup;
  }

  for (size_t i = 0; i < preset->n_buckets; ++i) {
    SavedBucket* buck = preset->buckets[i];

#define savedToNodeTypeEntry(x) [SAVED_NODE_TYPE__##x] = x,
    int const savedToNodeType[] = { nodeTypes(savedToNodeTypeEntry) };
    int type = savedToNodeType[buck->type];

    BufReserveZero(&data[type], buck->n_nodes);

    switch (type) {
      case NCOMMENT: BufReserveZero(&commentData, buck->n_nodes); break;
      case NRESULT: BufReserveZero(&resultData, buck->n_nodes); break;
    }

    for (size_t j = 0; j < buck->n_nodes; ++j) {
      SavedNode* sn = buck->nodes[j];

      // map tree id to tree index to convert the connections later
      if (sn->id + 1 > BufLen(treeById)) {
        BufReserve(&treeById, sn->id + 1 - BufLen(treeById));
      }
      treeById[sn->id] = BufLen(tree);

      int in = treeAdd(type, 0, 0);
      if (in < 0) {
        goto cleanup;
      }

      Node* n = &tree[in];

      SavedNodeData* sd = sn->data;
      if (!sd) {
        fprintf(stderr, "node id %d missing data\n", n->id);
        goto cleanup;
      }

      SavedRect* r = sd->bounds;
      if (!r) {
        fprintf(stderr, "node id %d missing bounds\n", n->id);
        goto cleanup;
      }

      NodeData* d = &data[type][n->data];
      d->bounds = nk_rect(r->x, r->y, r->w, r->h);
      d->value = sd->value;

      switch (type) {
        case NCOMMENT: {
          SavedComment* sc = sn->commentdata;
          if (!sc) {
            fprintf(stderr, "node id %d missing commentData\n", n->id);
            goto cleanup;
          }
          Comment* c = &commentData[n->data];
          snprintf(c->buf, sizeof(c->buf), "%s", sc->text);
          c->len = strlen(sc->text);
          break;
        }
        case NRESULT: {
          SavedResult* sr = sn->resultdata;
          if (!sr) {
            fprintf(stderr, "node id %d missing resultData\n", n->id);
            goto cleanup;
          }
          Result* r = &resultData[n->data];
          r->page = sr->page;
          r->perPage = sr->perpage;
          break;
        }
      }

    }
  }

  // remember, these are also references to nodes which need to be converted from id to idx
  BufEach(NodeData, data[NSPLIT], d) {
    d->value = treeById[d->value];
  }

  for (size_t i = 0; i < preset->n_connections; ++i) {
    SavedConnection* con = preset->connections[i];
    int from = treeById[con->fromid];
    int   to = treeById[con->  toid];
    treeLink(from, to);
  }

  res = 1;

cleanup:
  saved_preset__free_unpacked(preset, 0);
  BufFree(&treeById);

  if (!res) {
    treeClear();
    error("failed to load preset, possibly corrupt file?");
  }

  return res;
}

void storageAfterInit() {
  if (!storageLoadSync("/autosave.cubecalc")) {
    examplesInit();
    storageSaveSync("/autosave.cubecalc");
    storageLoadSync("/autosave.cubecalc");
  }
  status("");
  atomic_store(&storageReady, 1);
}

void storageInit() {
  status("loading from storage");
#ifdef __EMSCRIPTEN__
  EM_ASM(
    FS.mkdir('/data');
    FS.mount(IDBFS, {}, '/data');
    FS.syncfs(true, function (err) {
      assert(!err);
      ccall('storageAfterInit', 'v');
    });
  );
#else
  storageAfterInit();
#endif
}

void storageSave() {
#ifdef __EMSCRIPTEN__
  EM_ASM(
    FS.syncfs(function (err) {
      assert(!err);
    });
  );
#endif
}

int main() {
  atomic_init(&storageReady, 0);
  pyInit(TS);
  treeInit();
  storageInit();

  glfwSetErrorCallback(errorCallback);
  if (!glfwInit()) {
    puts("[GFLW] failed to init!");
    return 1;
  }
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  win = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "MapleStory Average Cubing Cost", 0, 0);
  glfwMakeContextCurrent(win);
  glfwGetWindowSize(win, &width, &height);

  nk = nk_glfw3_init(win, NK_GLFW3_INSTALL_CALLBACKS);
  in = &nk->input;

  // this is required even if empty
  {
    struct nk_font_atlas *atlas;
    nk_glfw3_font_stash_begin(&atlas);
    nk_glfw3_font_stash_end();
  }

  disclaimerHeight = 0;
  int rowHeight = nk->style.font->height + nk->style.edit.row_padding;
  for (char* p = disclaimer; *p; ++p) {
    if (*p == '\n') {
      disclaimerHeight += rowHeight;
    }
  }
  disclaimerHeight = NK_MAX(rowHeight, disclaimerHeight);
  disclaimerHeight += nk->style.edit.padding.y * 2 + nk->style.edit.border * 2;

#ifdef __EMSCRIPTEN__
  resizeCanvas();
  updateWindowSize();
  glfw.scale_factor = deviceScaleFactor();
  emscripten_set_main_loop(loop, 0, 1);
#else
  glfw.scale_factor = 1;
  while (!glfwWindowShouldClose(win)) {
    loop();
  }
#endif

  return 0;
}
