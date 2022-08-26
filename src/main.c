#include "generated.c"

#include <stdio.h>
#include <float.h>

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
});

EM_JS(void, pyCalcFree, (int calcIdx), {
  return Module.pyFunc("calc_free")(calcIdx);
});

EM_JS(void, pyCalcSet, (int calcIdx, int key, int value), {
  return Module.pyFunc("calc_set")(calcIdx, key, value);
});

EM_JS(void, pyCalcWant, (int calcIdx, int key, float value), {
  return Module.pyFunc("calc_want")(calcIdx, key, value);
});

EM_JS(float, pyCalc, (int calcIdx), {
  return Module.pyFunc("calc")(calcIdx);
});
#endif

#define NK_IMPLEMENTATION
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
//#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR

// required by glfw backend
#define NK_KEYSTATE_BASED_INPUT
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_GLFW_ES2_IMPLEMENTATION

#include "thirdparty/nuklear.h"
#include "thirdparty/nuklear_glfw_es2.h"
#include "utils.c"
#include <ctype.h>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

#define MAX_VERTEX_MEMORY 512 * 1024
#define MAX_ELEMENT_MEMORY 128 * 1024

#define CONTEXT_HEIGHT 20

void errorCallback(int e, const char *d) {
  printf("Error %d: %s\n", e, d);
}

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
};

#define MUTEX_FLAGS ( \
    LINKING | \
    UNLINKING | \
    RESIZING | \
    0)

#define otherMutexFlags(x) (MUTEX_FLAGS & ~(x))
#define flagAllowed(x) (!(flags & otherMutexFlags(x)))

const struct nk_vec2 contextualSize = { .x = 300, .y = 220 };

GLFWwindow* win;
struct nk_context* nk;
struct nk_input* in;
int width, height;
int displayWidth, displayHeight;
int fps;
int flags = SHOW_INFO | SHOW_GRID | SHOW_DISCLAIMER | UPDATE_SIZE;
struct nk_vec2 pan;
int linkNode;
int resizeNode;
int selectedNode = -1;
struct nk_vec2 savedMousePos; // in node space, not screen space
int tool;
int disclaimerHeight = 290;

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

#define stringifyComma(x) #x,
char* nodeNames[] = { nodeTypes(stringifyComma) };
char const* toolNames[] = { tools(stringifyComma) };

#define NODEWND  NK_WINDOW_BORDER | NK_WINDOW_TITLE
#define OTHERWND NODEWND | NK_WINDOW_CLOSABLE
#define CALCWND  NODEWND | NK_WINDOW_NO_SCROLLBAR

// NSOME_NODE_NAME -> Some Node Name
void treeInitNodeNames() {
  for (int i = 0; i < NK_LEN(nodeNames); ++i) {
    char* p = nodeNames[i];
    int len = strlen(p + 1);
    memmove(p, p + 1, len + 1);
    for (int j = 1; j < len; ++j) {
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
  int value, node;
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
  int within50, within75, within95, within99;
} Result;

Node* tree;
NodeData* data[NLAST];
Comment* commentData;
Result* resultData;
char** errors;
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

struct nk_vec2 nodeSpaceToScreen(struct nk_vec2 v) {
  v = nk_layout_space_to_screen(nk, v);
  v.x -= pan.x;
  v.y -= pan.y;
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

  for (int i = 0; i < BufLen(tree); ++i) {
    int* cons = tree[i].connections;
    done[i] = 1;
    for (int j = 0; j < BufLen(cons); ++j) {
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

void error(char* s) {
  *BufAlloc(&errors) = s;
  fprintf(stderr, "%s\n", s);
}

int defaultValue(int type, int stat) {
  switch (type) {
    case NCUBE: return RED;
    case NTIER: return LEGENDARY;
    case NCATEGORY: return WEAPON;
    case NSTAT: return ATT;
    case NAMOUNT: {
      int val = lineValues[stat];
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
  for (int i = 0; i < BufLen(tree); ++i) {
    if (tree[i].id == id) {
      ++id;
      goto nextId;
    }
  }
  return id;
}

int treeAdd(int type, int x, int y) {
  int id = treeNextId(); // important: do this BEFORE allocating the node
  NodeData* d = BufAlloc(&data[type]);
  Node* n;
  int chars;

  switch (type) {
    case NSPLIT:
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
    BufDel(data[type], BufLen(data[type]) - 1);
    return -1;
  }

  n = BufAlloc(&tree);
  n->id = id;
  n->data = BufLen(data[type]) - 1;
  n->type = type;
  n->connections = 0;
  d->node = BufLen(tree) - 1;

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
  for (int i = 0; i < BufLen(tree); ++i) {
    if (i == nodeIndex) continue;
    Node* n = &tree[i];

    // adjust connections indices
    int* newConnections = 0;
    for (int j = 0; j < BufLen(n->connections); ++j) {
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
      BufDel(resultData, index);
      break;
  }

  flags |= UPDATE_CONNECTIONS;
}

void treeLink(int from, int to) {
  // redundant but useful so we can walk up the graph without searching all nodes
  *BufAlloc(&tree[from].connections) = to;
  *BufAlloc(&tree[to].connections) = from;
  flags |= UPDATE_CONNECTIONS;
}

void treeUnlink(int from, int to) {
  BufDelFindInt(tree[from].connections, to);
  BufDelFindInt(tree[to].connections, from);
}

void updateFPS() {
  static double framesTimer;
  static int frames;
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

int uiBeginNode(int type, int i, int h) {
  NodeData* d = &data[type][i];
  struct nk_rect bounds = d->bounds;
  struct nk_rect screenBounds = nk_layout_space_rect_to_screen(nk, d->bounds);
  struct nk_panel* parentPanel = nk_window_get_panel(nk);
  int winFlags = NODEWND;
  struct nk_style* style = &nk->style;
  struct nk_user_font const* font = style->font;
  // TODO: avoid branching, make a separate func for comments
  if (type == NCOMMENT) {
    bounds.h = h + 20;
    winFlags &= ~NK_WINDOW_TITLE;
  }
  nk_layout_space_push(nk,
    nk_rect(bounds.x - pan.x, bounds.y - pan.y, bounds.w, bounds.h)
  );
  int showContextual = 0;
  int res = nk_group_begin(nk, d->name, winFlags);
  if (res) {
    struct nk_panel* panel = nk_window_get_panel(nk);
    struct nk_rect nodeBounds = panel->bounds;

    nk_layout_row_dynamic(nk, h, 1);

    struct nk_rect dragBounds = nodeBounds;

    if (type != NCOMMENT) {
      // make context menu click area more lenient for nodes
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

    // prevent dragging when the window overlaps with the parent window title
    int inParent = nk_input_is_mouse_hovering_rect(in, parentPanel->bounds);

    // lock dragging to the window we started dragging so we don't drag other windows when we
    // hover over them during dragging
    static int draggingId = -1;
    int inCombo = nk->current->popup.win != 0; // HACK: this relies on nk internals
    int draggingThisNode = draggingId == tree[d->node].id;
    if (!inCombo && inParent && leftMouseDown && leftMouseClickInCursor && !leftMouseClicked &&
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

    showContextual = showContextual || nk_contextual_begin(nk, 0, contextualSize, nodeBounds);
  }

  // the contextual menu logic should be outside of the window logic because for things like
  // comments, we could trigger the contextual menu when the node's main window is not visible
  // by clicking on the border for example

  if (type == NCOMMENT) {
    const int loff = COMMENT_ROUND + COMMENT_THICK * 3;
    const int roff = COMMENT_ROUND;
    struct nk_rect left, right, top, bottom;
    struct nk_rect b = screenBounds;
    left = right = top = bottom = commentBounds(b);
    left.x -= roff / 2;
    left.w = loff;
    right.x += right.w - roff / 2;
    right.w = loff;
    top.y -= roff / 2;
    top.h = loff;
    bottom.y += bottom.h - roff / 2;
    bottom.h = loff;
    showContextual = showContextual || nk_contextual_begin(nk, 0, contextualSize, left);
    showContextual = showContextual || nk_contextual_begin(nk, 0, contextualSize, right);
    showContextual = showContextual || nk_contextual_begin(nk, 0, contextualSize, top);
    showContextual = showContextual || nk_contextual_begin(nk, 0, contextualSize, bottom);
  }

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
        if (type == NSPLIT) {
          d->value = -1;
        } else if (otherType == NSPLIT) {
          data[NSPLIT][tree[linkNode].data].value = -1;
        }

        flags |= UPDATE_CONNECTIONS;
      } else {
        linkNode = d->node;
      }
      flags ^= UNLINKING;
    }

contextualEnd:
    nk_contextual_end(nk);
  } else if (selectedNode == d->node) {
    selectedNode = -1;
  }

  return res;
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

#define ASSUMED_KEY 1
#define ASSUMED_VALUE 2
typedef struct _Pair { int assumed, key, value; } Pair;

void treeCalcAppendWants(int* values, Pair** wants) {
  Pair* want = BufAlloc(wants);
  // TODO: DRY
  if (values[NSTAT] == -1) {
    want->key = defaultValue(NSTAT, 0);
    want->assumed |= ASSUMED_KEY;
  } else {
    want->key = values[NSTAT];
  }
  if (values[NAMOUNT] == -1) {
    want->value = defaultValue(NAMOUNT, want->key);
    want->assumed |= ASSUMED_VALUE;
  } else {
    want->value = values[NAMOUNT];
  }
  values[NSTAT] = -1;
  values[NAMOUNT] = -1;
}

void treeCalcBranch(int* values, Pair** wants, int node, int* seen) {
  if (seen[node]) {
    // it's important to not visit the same node twice so we don't get into a loop
    return;
  }

  seen[node] = 1;

  Node* n = &tree[node];
  NodeData* d = &data[n->type][n->data];

  // TODO: flatten this so we don't use recursion
  if (n->type == NSPLIT) {
    if (d->value >= 0) {
      // in the case of a split, we only take the parent so that we don't traverse the other
      // branches. that's the whole point of this node. to allow reusing common nodes
      treeCalcBranch(values, wants, d->value, seen);
    }
  } else {
    for (int i = 0; i < BufLen(n->connections); ++i) {
      treeCalcBranch(values, wants, n->connections[i], seen);
    }
  }

  switch (n->type) {
    case NCUBE:
    case NTIER:
    case NCATEGORY:
    case NAMOUNT:
    case NLEVEL:
    case NREGION:
      values[n->type] = d->value;
    // TODO: more advanced logic (AND, OR, etc)
    case NSPLIT: // handled above
    case NSTAT: // handled below
    case NRESULT:
      break;
    default:
      fprintf(stderr, "error visiting node %d, unknown type %d\n", node, n->type);
  }

  // every time we counter either stat or amount:
  // - if it's amount, the desired stat pair is complete and we append either the default line
  //   or the upstream line to the wants array
  // - if it's a line, we save it but we don't do anything until either an amount is found
  //   downstream, another line is found or we're done visiting the graph

  if (n->type == NAMOUNT || (n->type == NSTAT && values[NSTAT] != -1)) {
    treeCalcAppendWants(values, wants); // add either default line or upstream line
  }

  if (n->type == NSTAT) {
    // save this line for later
    values[NSTAT] = d->value;
  }
}

char const* valueName(int type, int value) {
  switch (type) {
    case NCUBE: return cubeNames[value];
    case NTIER: return tierNames[value];
    case NCATEGORY: return categoryNames[value];
    case NSTAT: return lineNames[value];
  }
  return 0;
}

int valueToCalc(int type, int value) {
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

void treeCalc() {
  for (int i = 0; i < BufLen(tree); ++i) {
    Node* n = &tree[i];
    // TODO: more type of result nodes (median, 75%, 85%, etc)
    if (n->type == NRESULT) {
      NodeData* d = &data[n->type][n->data];
      printf("treeCalc %s\n", d->name);
      pyCalcFree(n->id);

      int values[NLAST];
      for (int j = 0; j < NLAST; ++j) {
        values[j] = -1;
      }

      int* seen = 0;
      Pair* wants = 0;
      BufReserve(&seen, BufLen(tree));
      BufZero(seen);
      treeCalcBranch(values, &wants, i, seen);
      BufFree(&seen);

      for (int j = NINVALID + 1; j < NLAST; ++j) {
        switch (j) {
          case NSTAT:
          case NAMOUNT:
          case NRESULT:
          case NCOMMENT:
          case NSPLIT:
            // these are handled separately, or are not relevant
            continue;
        }
        if (values[j] == -1) {
          values[j] = defaultValue(j, values[CATEGORY]);
          printf("(assumed) ");
        }
        char const* svalue = valueName(j, values[j]);
        char* fmt = svalue ? "%s = %s\n" : "%s = %d\n";
        char* valueName = nodeNames[j - 1];
        if (svalue) {
          printf(fmt, valueName, svalue);
        } else {
          printf(fmt, valueName, values[j]);
        }
        int param = treeTypeToCalcParam(j);
        int value = valueToCalc(j, values[j]);
        if (param) {
          pyCalcSet(n->id, param, value);
        } else {
          fprintf(stderr, "unknown calc param %d = %d\n", j, values[j]);
        }
      }

      // append complete any pending line to wants, or just the default line
      if (!BufLen(wants) || values[NSTAT] != -1 || values[NAMOUNT] != -1) {
        treeCalcAppendWants(values, &wants);
      }

      for (int j = 0; j < BufLen(wants); ++j) {
        if (wants[j].assumed & ASSUMED_KEY) {
          printf("(assumed) ");
        }
        printf("%s = ", lineNames[wants[j].key]);
        if (wants[j].assumed & ASSUMED_VALUE) {
          printf("(assumed) ");
        }
        printf("%d\n", wants[j].value);

        pyCalcWant(n->id, lineValues[wants[j].key], wants[j].value);
      }

      BufFree(&wants);

      float chance = pyCalc(n->id);
      Result* resd = &resultData[n->data];
      if (chance > 0) {
        d->value = ProbToOneIn(chance);
        resd->within50 = ProbToGeoDistrQuantileDingle(chance, 50);
        resd->within75 = ProbToGeoDistrQuantileDingle(chance, 75);
        resd->within95 = ProbToGeoDistrQuantileDingle(chance, 95);
        resd->within99 = ProbToGeoDistrQuantileDingle(chance, 99);
      } else {
        d->value = 0;
        memset(resd, 0, sizeof(*resd));
      }
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

void loop() {
  int i, j;

  glfwPollEvents();
  nk_glfw3_new_frame();

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
    for (i = 0; i < BufLen(links); ++i) {
      NodeLink* l = &links[i];
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
  for (i = 0; i < BufLen(data[type]); ++i) { \
    if (uiBeginNode(type, i, 25)) { \
      NodeData* d = &data[type][i]; \
      int newValue = nk_combo(nk, enumName##Names, NK_LEN(enumName##Names), d->value, \
        25, nk_vec2(nk_widget_width(nk), 100)); \
      treeSetValue(d, newValue); \
      uiEndNode(type, i); \
    } \
  }

#define propNode(type, valueType) \
  for (i = 0; i < BufLen(data[type]); ++i) { \
    if (uiBeginNode(type, i, 20)) { \
      NodeData* d = &data[type][i]; \
      int newValue = nk_property##valueType(nk, d->name, 0, d->value, 300, 1, 0.02); \
      treeSetValue(d, newValue); \
      uiEndNode(type, i); \
    } \
  }

    for (i = 0; i < BufLen(data[NCOMMENT]); ++i) {
      NodeData* d = &data[NCOMMENT][i];
      struct nk_rect bounds = commentBounds(d->bounds);
      const struct nk_color color = nk_rgb(255, 255, 128);
      nk_stroke_rect(canvas, nk_layout_space_rect_to_screen(nk, bounds),
                     COMMENT_ROUND, COMMENT_THICK, color);
      if (uiBeginNode(NCOMMENT, i, 20)) {
        bounds.x -= pan.x;
        bounds.y -= pan.y;

        Comment* com = &commentData[i];
        nk_edit_string(nk, NK_EDIT_FIELD, com->buf, &com->len, COMMENT_MAX, 0);
        uiEndNode(NCOMMENT, i);
      }
    }

    for (i = 0; i < BufLen(data[NSPLIT]); ++i) {
      if (uiBeginNode(NSPLIT, i, 20)) {
        uiEndNode(NSPLIT, i);
      }
    }

    comboNode(NCUBE, cube);
    comboNode(NTIER, tier);
    comboNode(NCATEGORY, category);
    comboNode(NSTAT, line);
    comboNode(NREGION, region);
    propNode(NAMOUNT, i);
    propNode(NLEVEL, i);

    for (i = 0; i < BufLen(data[NRESULT]); ++i) {
      if (uiBeginNode(NRESULT, i, 10)) {
        nk_value_int(nk, "average 1 in", data[NRESULT][i].value);
        nk_value_int(nk, "50% chance within", resultData[i].within50);
        nk_value_int(nk, "75% chance within", resultData[i].within75);
        nk_value_int(nk, "95% chance within", resultData[i].within95);
        nk_value_int(nk, "99% chance within", resultData[i].within99);
        uiEndNode(NRESULT, i);
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
    for (i = 0; i < BufLen(removeNodes); ++i) {
      int nodeIndex = removeNodes[i];
      treeDel(nodeIndex);
      selectedNode = -1;
      for (j = i; j < BufLen(removeNodes); ++j) {
        if (removeNodes[j] > nodeIndex) {
          --removeNodes[j];
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
        for (int i = 0; i < NK_LEN(nodeNames); ++i) {
          if (nk_contextual_item_label(nk, nodeNames[i], NK_TEXT_CENTERED)) {
            treeAdd(i + 1, savedMousePos.x, savedMousePos.y);
          }
        }
        nk_layout_row_dynamic(nk, CONTEXT_HEIGHT, 1);
      }

#define flag(x, text, f) (void)( \
      nk_contextual_item_label(nk, (flags & x) ? "Hide " text : "Show " text, NK_TEXT_CENTERED) &&\
        (flags = (flags ^ x) | f))

      flag(SHOW_INFO, "Info", UPDATE_SIZE);
      flag(SHOW_GRID, "Grid", 0);
      flag(SHOW_DISCLAIMER, "Disclaimer", 0);

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
      for (i = 0; i < BufLen(errors); ++i) {
        nk_label(nk, errors[i], NK_TEXT_LEFT);
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
    calcBounds.x = calcBounds.y = 10;
    calcBounds.w = width / glfw.scale_factor - 20;
    calcBounds.h = height / glfw.scale_factor - 20;
    disclaimerBounds.w = NK_MIN(calcBounds.w, 610);
    disclaimerBounds.h = NK_MIN(calcBounds.h, 370);
    disclaimerBounds.x = calcBounds.w / 2 - disclaimerBounds.w / 2 + 10;
    disclaimerBounds.y = calcBounds.h / 2 - disclaimerBounds.h / 2 + 10;
    errorBounds.x = calcBounds.w / 2 - 200 + 10;
    errorBounds.y = calcBounds.h / 2 - 100 + 10;
    errorBounds.w = 400;
    errorBounds.h = 200;
    if (width > height) {
      if (flags & SHOW_INFO) calcBounds.w -= 210;
      infoBounds.x = calcBounds.w + 20;
      infoBounds.y = 10;
      infoBounds.w = 200;
      infoBounds.h = calcBounds.h;
      flags &= ~PORTRAIT;
    } else {
      if (flags & SHOW_INFO) calcBounds.h -= 210;
      infoBounds.x = 10;
      infoBounds.y = calcBounds.h + 20;
      infoBounds.w = calcBounds.w;
      infoBounds.h = 200;
      flags |= PORTRAIT;
    }
    calcBounds.w = NK_MAX(50, calcBounds.w);
    calcBounds.h = NK_MAX(50, calcBounds.h);
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

int treeAddChk(int type, int x, int y, int* succ) {
  int res = treeAdd(type, x, y);
  *succ = *succ && res >= 0;
  return res;
}

int treeAddComment(int x, int y, int w, int h, char* text, int* succ) {
  int ncomment = treeAddChk(NCOMMENT, x, y, succ);
  int i = tree[ncomment].data;
  NodeData* d = &data[NCOMMENT][i];
  Comment* cd = &commentData[i];
  d->bounds.w = w;
  d->bounds.h = h;
  snprintf(cd->buf, COMMENT_MAX - 1, "%s", text);
  cd->len = strlen(cd->buf);
  return ncomment;
}

int main() {
  pyInit(TS);
  treeInit();

  int succ = 1;

  int ncategory = treeAddChk(NCATEGORY, 20, 20, &succ);
  int ncube = treeAddChk(NCUBE, 340, 20, &succ);
  int ntier = treeAddChk(NTIER, 560, 20, &succ);
  int nlevel = treeAddChk(NLEVEL, 770, 20, &succ);
  int nregion = treeAddChk(NREGION, 770, 110, &succ);
  int nsplit = treeAddChk(NSPLIT, 690, 110, &succ);
  if (succ) {
    data[NSPLIT][tree[nsplit].data].value = ntier;
    data[NLEVEL][tree[nlevel].data].value = 200;
    treeLink(ncategory, ncube);
    treeLink(ncube, ntier);
    treeLink(nlevel, ntier);
    treeLink(nlevel, nregion);
  }

  {
    int ncomment = treeAddComment(20, 130, 200, 310, "example: 23+ %att", &succ);
    int nstat = treeAddChk(NSTAT, 20, 180, &succ);
    int namt = treeAddChk(NAMOUNT, 20, 270, &succ);
    int nres = treeAddChk(NRESULT, 20, 360, &succ);

    if (succ) {
      data[NAMOUNT][tree[namt].data].value = 23;
      treeLink(nsplit, nstat);
      treeLink(nstat, namt);
      treeLink(namt, nres);
    }
  }

  {
    int ncomment = treeAddComment(260, 130, 410, 310, "example: 20+ %att and 30+ %boss", &succ);
    int nstat2 = treeAddChk(NSTAT, 260, 180, &succ);
    int namt2 = treeAddChk(NAMOUNT, 260, 270, &succ);
    int nstat = treeAddChk(NSTAT, 470, 180, &succ);
    int namt = treeAddChk(NAMOUNT, 470, 270, &succ);
    int nres = treeAddChk(NRESULT, 470, 360, &succ);

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

  glfwSetErrorCallback(errorCallback);
  if (!glfwInit()) {
      fprintf(stdout, "[GFLW] failed to init!\n");
      exit(1);
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
