#include "graph.c"
#include "graphcalc.c"
#include "serialization.c"
#include "generated.c"
#include "nuklear.c"
#include "utils.c"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <stdlib.h> // qsort
#include <dirent.h> // opendir readdir

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
  return Window.devicePixelRatio || 1;
});

EM_JS(float, pinchDelta, (), {
  return Module.deltaDist;
});

EM_JS(int, isTouch, (), {
  return (('ontouchstart' in window) ||
     (navigator.maxTouchPoints > 0) ||
     (navigator.msMaxTouchPoints > 0)) ? 1 : 0;
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
  SAVE_OVERWRITE_PROMPT = 1<<14,
  DELETE_PROMPT = 1<<15,
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
struct nk_vec2 pan;
int linkNode;
int resizeNode;
int selectedNode = -1;
struct nk_vec2 savedMousePos; // in node space, not screen space
int tool;
int disclaimerHeight = 290;
int maxCombos = 50;
int* removeNodes;

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

char const* toolNames[] = { tools(StringifyComma) };
enum { tools(appendComma) };

#define NODEWND  NK_WINDOW_BORDER | NK_WINDOW_TITLE
#define OTHERWND NODEWND | NK_WINDOW_CLOSABLE
#define CALCWND  NK_WINDOW_NO_SCROLLBAR

#define COMMENT_ROUND 10
#define COMMENT_THICK 1

typedef struct _NodeLink {
  int from, to; // node index
  struct nk_color color;
  float thick;
} NodeLink;

NodeLink* links;
TreeData graph;

void uiTreeClear() {
  treeClear(&graph);
  BufClear(removeNodes);
  flags |= UPDATE_CONNECTIONS;
}

void uiTreeFree() {
  treeClear(&graph);
  treeFree(&graph);
  BufFree(&removeNodes);
  BufFree(&links);
}

int uiTreeAdd(int type, int x, int y) {
  int res = treeAdd(&graph, type, x, y);
  if (type == NRESULT) {
    flags |= DIRTY;
  }
  return res;
}

void uiTreeDel(int nodeIndex) {
  flags |= UPDATE_CONNECTIONS;
  if (BufLen(graph.tree[nodeIndex].connections)) {
    flags |= DIRTY;
  }
  treeDel(&graph, nodeIndex);
}

void uiTreeLink(int from, int to) {
  treeLink(&graph, from, to);
  flags |= UPDATE_CONNECTIONS | DIRTY;
}

void uiTreeUnlink(int from, int to) {
  treeUnlink(&graph, from, to);
  flags |= DIRTY;
}

NodeData* uiTreeDataByNode(int node) {
  return treeDataByNode(&graph, node);
}

Result* uiTreeResultByNode(int node) {
  return treeResultByNode(&graph, node);
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
  Node* n = &graph.tree[node];
  int i = n->data;
  int type = n->type;
  struct nk_rect bounds = graph.data[type][i].bounds;
  struct nk_vec2 center = nk_rect_pos(bounds);
  center.x += bounds.w / 2;
  center.y += bounds.h / 2;
  return center;
}

void uiTreeUpdateConnections() {
  BufClear(links);

  // TODO: could be a bitmask
  int* done = 0;
  BufReserve(&done, BufLen(graph.tree));
  memset(done, 0, BufLen(done) * sizeof(done[0]));

  for (size_t i = 0; i < BufLen(graph.tree); ++i) {
    int* cons = graph.tree[i].connections;
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
    if (graph.tree[i].type == NSPLIT) {
      NodeData* d = &graph.data[NSPLIT][graph.tree[i].data];
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
}

void updateFPS() {
  static int frames;
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
  NodeData* d = &graph.data[type][i];
  struct nk_rect bounds = d->bounds;
  struct nk_panel* parentPanel = nk_window_get_panel(nk);
  int winFlags = NODEWND;

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
    nkAdjustDragBounds(nk, &nodeBounds, &dragBounds);
  }

  if (!nk_input_is_mouse_hovering_rect(in, parentPanel->bounds)) {
    // no dragging if we're clicking on the info window
    return res;
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
  int draggingThisNode = draggingId == graph.tree[d->node].id;
  if (!inCombo && leftMouseDown && leftMouseClickInCursor && !leftMouseClicked &&
      tool == MOVE)
  {
    if (draggingId == -1 || draggingThisNode) {
      draggingId = graph.tree[d->node].id;
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
  NodeData* d = &graph.data[type][i];
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
            graph.data[NSPLIT][graph.tree[linkNode].data].value = d->node;
          } else {
            // finish linking node->node
            uiTreeLink(d->node, linkNode);
          }

          flags |= UPDATE_CONNECTIONS;

          if (!isSplit && !(flags & LINKING_SPLIT)) {
            int otherType = graph.tree[linkNode].type;
            if ((type == NSPLIT && d->value == linkNode) ||
                (otherType == NSPLIT &&
                 graph.data[NSPLIT][graph.tree[linkNode].data].value == d->node)
            ) {
              error("the node is already linked as a split branch");
              uiTreeUnlink(d->node, linkNode);
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
        uiTreeUnlink(linkNode, d->node);

        // unlink split
        int otherType = graph.tree[linkNode].type;
        NodeData* otherData = &graph.data[NSPLIT][graph.tree[linkNode].data];
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

void uiTreeSetValue(NodeData* d, int newValue) {
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
  for (size_t i = 0; i < BufLen(graph.data[type]); ++i) {
    if (uiBeginNode(type, i, 20)) {
      uiEndNode(type, i);
    }
    if (uiContextual(type, i)) {
      nk_contextual_end(nk);
    }
  }
}

char** presetFiles;
char presetFile[FILENAME_MAX + 1];
int presetIndex;

int storageSaveGlobalsSync();
void storageAutoSave();
int presetLoad(char* path);
int presetSave(char* path);
int presetSaveNoCommit(char* path);
int presetExists(char* path);
void presetList();
void presetDelete(char* path);

nk_bool presetFilter(const struct nk_text_edit *box, nk_rune unicode) {
  NK_UNUSED(box);
  if ((unicode >= 'A' && unicode <= 'Z') ||
      (unicode >= 'a' && unicode <= 'z') ||
      (unicode >= '0' && unicode <= '9') ||
      unicode == '_')
    return nk_true;
  return nk_false;
}

void loop() {
#ifdef __EMSCRIPTEN__
  float pd = pinchDelta();
  if (pd != 0) {
    nk_glfw3_set_scale_factor(nk_glfw3_scale_factor() * (1 + pd * 0.002));
    flags |= UPDATE_SIZE;
  }
#endif

  glfwPollEvents();
  nk_glfw3_new_frame();

  if (flags & SHOW_DISCLAIMER) {
    goto dontShowCalc;
  }

  if (nk_begin(nk, CALC_NAME, calcBounds, CALCWND)) {
    struct nk_command_buffer* canvas = nk_window_get_canvas(nk);
    struct nk_rect totalSpace = nk_window_get_content_region(nk);

    nk_layout_space_begin(nk, NK_STATIC, totalSpace.h, BufLen(graph.tree));
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
      Node* n = &graph.tree[resizeNode];
      NodeData* d = &graph.data[n->type][n->data];
      struct nk_vec2 m = nk_layout_space_to_local(nk, in->mouse.pos);
      d->bounds.w = NK_MAX(100, m.x - d->bounds.x + pan.x);
      d->bounds.h = NK_MAX(50, m.y - d->bounds.y + pan.y);
    }

#define comboNode(type, enumName) \
  BufEachi(graph.data[type], i) { \
    if (uiBeginNode(type, i, 25)) { \
      NodeData* d = &graph.data[type][i]; \
      int newValue = nk_combo(nk, (const char**)enumName##Names, NK_LEN(enumName##Names), \
        d->value, 25, nk_vec2(nk_widget_width(nk), 100)); \
      uiTreeSetValue(d, newValue); \
      uiEndNode(type, i); \
    } \
    if (uiContextual(type, i)) { \
      nk_contextual_end(nk); \
    } \
  }

#define propNode(type, valueType) \
  BufEachi(graph.data[type], i) { \
    if (uiBeginNode(type, i, 20)) { \
      NodeData* d = &graph.data[type][i]; \
      int newValue = nk_property##valueType(nk, d->name, 0, d->value, 300, 1, 0.02); \
      uiTreeSetValue(d, newValue); \
      uiEndNode(type, i); \
    } \
    if (uiContextual(type, i)) { \
      nk_contextual_end(nk); \
    } \
  }

    BufEachi(graph.data[NCOMMENT], i) {
      NodeData* d = &graph.data[NCOMMENT][i];
      struct nk_rect bounds = commentBounds(d->bounds);
      const struct nk_color color = nk_rgb(255, 255, 128);
      nk_stroke_rect(canvas, nk_layout_space_rect_to_screen(nk, bounds),
                     COMMENT_ROUND, COMMENT_THICK, color);
      if (uiBeginNode(NCOMMENT, i, COMMENT_H)) {
        bounds.x -= pan.x;
        bounds.y -= pan.y;

        Comment* com = &graph.commentData[i];
        char oldBuf[sizeof(com->buf)];
        int oldLen = com->len;
        memcpy(oldBuf, com->buf, com->len);
        nk_edit_string(nk, NK_EDIT_FIELD, com->buf, &com->len, COMMENT_MAX, 0);
        if (com->len != oldLen || memcmp(oldBuf, com->buf, com->len)) {
          storageAutoSave();
        }

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
    comboNode(NSTAT, allLine);
    comboNode(NREGION, region);
    propNode(NAMOUNT, i);
    propNode(NLEVEL, i);

    BufEachi(graph.data[NRESULT], i) {
      if (uiBeginNode(NRESULT, i, 10)) {
#define l(text, x) \
  nk_label(nk, text, NK_TEXT_RIGHT); \
  nk_label(nk, *graph.resultData[i].x ? graph.resultData[i].x : "impossible", NK_TEXT_LEFT)
#define q(n) l(#n "% within:", within##n)

        nk_layout_row_template_begin(nk, 10);
        nk_layout_row_template_push_static(nk, 90);
        nk_layout_row_template_push_dynamic(nk);
        nk_layout_row_template_end(nk);
        l("average 1 in:", average);
        q(50); q(75); q(95); q(99);
        l("combos:", numCombosStr);

        Result* r = &graph.resultData[i];
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
            static char const* const primestr[2] = { "", "P" };
            nk_label(nk, primestr[ArrayBitVal(r->prime, k)], NK_TEXT_LEFT);
            nk_label(nk, r->value[k], NK_TEXT_RIGHT);
            nk_label(nk, r->line[k], NK_TEXT_LEFT);
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
      Node* sn = &graph.tree[selectedNode];
      NodeData* sd = &graph.data[sn->type][sn->data];
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
      uiTreeDel(*nodeIndex);
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
        for (size_t i = 0; i < ArrayLength(nodeNames); ++i) {
          if (nk_contextual_item_label(nk, nodeNames[i], NK_TEXT_CENTERED)) {
            uiTreeAdd(i + 1, savedMousePos.x, savedMousePos.y);
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
      flag(DEBUG, "Debug in Console", 0);

      if (nk_contextual_item_label(nk, "I'm Lost", NK_TEXT_CENTERED)) {
        if (BufLen(graph.tree)) {
          NodeData* n = &graph.data[graph.tree[0].type][graph.tree[0].data];
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
    treeCalc(&graph, maxCombos);

    // ensure autosaves happens on 1st frame
    static double autosaveTimer30 = -10000;
    static double autosaveTimer5  = -10000;
    static double autosaveTimer1  = -10000;

    double t = glfwGetTime();

    if (t - autosaveTimer5 > 30 * 60) {
      presetSaveNoCommit("autosave_30mins");
      autosaveTimer30 = t;
    }

    if (t - autosaveTimer5 > 5 * 60) {
      presetSaveNoCommit("autosave_5mins");
      autosaveTimer5 = t;
    }

    if (t - autosaveTimer1 > 1 * 60) {
      presetSaveNoCommit("autosave_1min");
      autosaveTimer1 = t;
    }

    storageAutoSave(); // this also commits
    flags &= ~DIRTY;
  }

  if (flags & UPDATE_CONNECTIONS) {
    uiTreeUpdateConnections();
    flags &= ~UPDATE_CONNECTIONS;
  }

  if (flags & SHOW_INFO) {
    if (nk_begin(nk, INFO_NAME, infoBounds, NK_WINDOW_BORDER)) {
      if (flags & PORTRAIT) {
        nk_layout_row_template_begin(nk, 20);
        nk_layout_row_template_push_dynamic(nk);
        nk_layout_row_template_push_static(nk, 90);
        nk_layout_row_template_push_static(nk, 90);
        nk_layout_row_template_end(nk);
      } else {
        nk_layout_row_dynamic(nk, 20, 1);
      }

      // TODO: DRY
      static double overwriteTimer, deleteTimer;

      if (glfwGetTime() - overwriteTimer > 5) {
        flags &= ~SAVE_OVERWRITE_PROMPT;
      }

      if (glfwGetTime() - deleteTimer > 5) {
        flags &= ~DELETE_PROMPT;
      }

      if (BufLen(presetFiles)) {
        presetIndex = NK_MIN(presetIndex, BufLen(presetFiles));
        int newValue = nk_combo(nk, (char const**)presetFiles, BufLen(presetFiles), presetIndex,
          25, nk_vec2(nk_widget_width(nk), 100));
        if (newValue != presetIndex) {
          snprintf(presetFile, FILENAME_MAX, "%s", presetFiles[newValue]);
          presetIndex = newValue;
          flags &= ~SAVE_OVERWRITE_PROMPT;
        }

        if (nk_button_label(nk, "load")) {
          if (!presetLoad(presetFiles[presetIndex])) {
            error("failed to load preset (corrupt?)");
          }
        }

        if (nk_button_label(nk, (flags & DELETE_PROMPT) ? "/!\\ delete? /!\\" : "delete")) {
          if (flags & DELETE_PROMPT) {
            presetDelete(presetFiles[presetIndex]);
          }
          deleteTimer = glfwGetTime();
          flags ^= DELETE_PROMPT;
        }
      }

      nk_edit_string_zero_terminated(nk, NK_EDIT_FIELD, presetFile, FILENAME_MAX, presetFilter);

      if (nk_button_label(nk, (flags & SAVE_OVERWRITE_PROMPT) ? "/!\\ overwrite? /!\\" : "save")) {
        if (presetExists(presetFile)) {
          if (flags & SAVE_OVERWRITE_PROMPT) {
            if (!presetSave(presetFile)) {
              error("failed to overwrite preset (disk full?)");
            }
          }
          overwriteTimer = glfwGetTime();
          flags ^= SAVE_OVERWRITE_PROMPT;
        } else {
          if (!presetSave(presetFile)) {
            error("failed to save preset (disk full?)");
          }
        }
        presetList();
      }

      if (flags & FULL_INFO) {
        if (flags & PORTRAIT) {
          nk_layout_row_static(nk, 20, NK_MAX(180, nk_widget_width(nk) / 2), 2);
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
        nk_glfw3_set_left_button(toolToMouseButton[tool]);
      }

      if (!(flags & PORTRAIT)) {
        nk_spacer(nk);
        nk_layout_row_dynamic(nk, 10, 1);
        nk_spacer(nk);
      }

      int activeFlags = 0;

#define showFlag(x) \
      if (flags & x) { \
        if (flags & FULL_INFO) nk_label(nk, #x "...", NK_TEXT_CENTERED); \
        activeFlags |= x; \
      }

      showFlag(LINKING)
      showFlag(UNLINKING)
      showFlag(RESIZING)

      if ((flags & FULL_INFO) && !activeFlags) {
        nk_value_int(nk, "FPS", fps);
        nk_value_int(nk, "Nodes", BufLen(graph.tree));
        nk_value_int(nk, "Links", BufLen(links));
      }

      if (!(flags & PORTRAIT)) {
        nk_layout_row_dynamic(nk, 20, 1);
      }

      if (activeFlags && (
            glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS ||
            ((flags & FULL_INFO) && nk_button_label(nk, "esc/ctx to cancel"))
          )) {
        flags &= ~activeFlags;
      }

      if (flags & PORTRAIT) {
        int pw = (int)nk_window_get_panel(nk)->bounds.w;
        nk_layout_row_static(nk, 20, NK_MAX(150, pw/5), 5);
      } else {
        nk_layout_row_dynamic(nk, 20, 1);
      }
      float currentSf = nk_glfw3_scale_factor();
      float sf = nk_propertyf(nk, "Scale", 1, currentSf, 2, 0.1, 0.02);
      if (sf != currentSf) {
        nk_glfw3_set_scale_factor(sf);
        flags |= UPDATE_SIZE;
      }
      int newMaxCombos = nk_propertyi(nk, "Max Combos", 0, maxCombos, 500, 100, 0.02);
      if (newMaxCombos != maxCombos) {
        maxCombos = newMaxCombos;
        flags |= DIRTY;
      }
      nk_label(nk, *statusText ? statusText : "Idle", NK_TEXT_LEFT);

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
        storageSaveGlobalsSync();
      }
      nk_layout_row_static(nk, disclaimerHeight, NK_MAX(570, nk_widget_width(nk)), 1);
      static int disclaimerLen = -1;
      if (disclaimerLen < 0) {
        disclaimerLen = strlen(disclaimer);
      }
      // NOTE: since it's not editable it should hopefully never touch that memory
      nk_edit_string(nk, NK_EDIT_BOX | NK_EDIT_READ_ONLY,
                     (char*)disclaimer, &disclaimerLen, disclaimerLen + 1, 0);
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
  int w, h;
  glfwGetWindowSize(win, &w, &h);
  if (w != width || h != height) {
    width = w;
    height = h;
    flags |= UPDATE_SIZE;
  }
#endif

  if (flags & UPDATE_SIZE) {
    calcBounds.x = calcBounds.y = 0;
    float sf = nk_glfw3_scale_factor();
    calcBounds.w = width / sf;
    calcBounds.h = height / sf;
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
      infoBounds.h = (flags & FULL_INFO) ? 210 : 120;
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

int uiTreeAddChk(struct nk_vec2 start, int type, int x, int y, int* succ) {
  int res = uiTreeAdd(type, start.x + x, start.y + y);
  *succ = *succ && res >= 0;
  return res;
}

int uiTreeAddComment(struct nk_vec2 start, int x, int y, int w, int h, char* text, int* succ) {
  int ncomment = uiTreeAddChk(start, NCOMMENT, x, y, succ);
  int i = graph.tree[ncomment].data;
  NodeData* d = &graph.data[NCOMMENT][i];
  Comment* cd = &graph.commentData[i];
  d->bounds.w = w;
  d->bounds.h = h;
  snprintf(cd->buf, COMMENT_MAX - 1, "%s", text);
  cd->len = strlen(cd->buf);
  return ncomment;
}

int examplesCommon(int* succ, int category) {
  struct nk_vec2 s = nk_vec2(20, 20);
  int ncategory = uiTreeAddChk(s, NCATEGORY, 0, 0, succ);
  int ncube = uiTreeAddChk(s, NCUBE, 310, 0, succ);
  int ntier = uiTreeAddChk(s, NTIER, 520, 0, succ);
  int nlevel = uiTreeAddChk(s, NLEVEL, 730, 0, succ);
  int nregion = uiTreeAddChk(s, NREGION, 940, 0, succ);
  int nsplit = uiTreeAddChk(s, NSPLIT, 650, 90, succ);

  if (*succ) {
    uiTreeDataByNode(ncategory)->value = category;
    uiTreeDataByNode(nsplit)->value = ntier;
    uiTreeDataByNode(nlevel)->value = 200;
    uiTreeLink(ncategory, ncube);
    uiTreeLink(ncube, ntier);
    uiTreeLink(nlevel, ntier);
    uiTreeLink(nlevel, nregion);
  }

  return nsplit;
}

void examplesBasicUsage() {
  struct nk_vec2 s = nk_vec2(20, 20);
  int succ = 1;
  int nprevres;
  int nsplit = examplesCommon(&succ, WEAPON_IDX);
  {
    s.y += 150;
    int ncomment = uiTreeAddComment(s, 0, 0, 410, 310, "example: 23+ %att", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(namt)->value = 23;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
    }
  }

  {
    s.x += 450;
    int ncomment = uiTreeAddComment(s, 0, 0, 410, 310, "example: 20+ %att and 30+ %boss", &succ);
    int nstat2 = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt2 = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 210, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 210, 140, &succ);
    int nres = uiTreeAddChk(s, NRESULT, 210, 230, &succ);

    if (succ) {
      uiTreeDataByNode(nstat2)->value = BOSS_IDX;
      uiTreeDataByNode(namt2)->value = treeDefaultValue(NAMOUNT, BOSS_IDX);
      uiTreeDataByNode(namt)->value = 20;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(nstat2, namt2);
      uiTreeLink(namt, nres);
      uiTreeLink(namt2, nres);
    }
  }

  {
    s.x = 230;
    s.y += 350;
    int ncomment = uiTreeAddComment(s, 0, 0, 200, 220, "example: bpot 23+ %att", &succ);
    int nbpot = uiTreeAddChk(s, NCUBE, 0, 50, &succ);
    int nres = uiTreeAddChk(s, NRESULT, 0, 140, &succ);
    int nsplit2 = uiTreeAddChk(s, NSPLIT, -100, -20, &succ);

    if (succ) {
      uiTreeDataByNode(nbpot)->value = BONUS_IDX;
      uiTreeDataByNode(nsplit2)->value = nprevres;
      uiTreeLink(nbpot, nsplit2);
      uiTreeLink(nbpot, nres);
    }
  }

  {
    s.x += 240;
    int ncomment = uiTreeAddComment(s, 0, 0, 410, 310,
        "example: any 3l combo of %att or %boss", &succ);
    int nstat2 = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt2 = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nstat3 = uiTreeAddChk(s, NSTAT, 210, 140, &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 210, 50, &succ);
    int nres = uiTreeAddChk(s, NRESULT, 210, 230, &succ);

    if (succ) {
      uiTreeDataByNode(nstat2)->value = BOSS_ONLY_IDX;
      uiTreeDataByNode(nstat3)->value = LINES_IDX;
      uiTreeDataByNode(namt2)->value = 3;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, nstat3);
      uiTreeLink(nstat, nstat2);
      uiTreeLink(nstat3, namt2);
      uiTreeLink(namt2, nres);
    }
  }
}

void examplesOperators() {
  int succ = 1;
  struct nk_vec2 s = nk_vec2(20, 20);
  int nsplit = examplesCommon(&succ, FACE_EYE_RING_EARRING_PENDANT_IDX);

  {
    s = nk_vec2(20, 130);
    struct nk_vec2 s0 = s;
    int nmeso = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int ndrop = uiTreeAddChk(s, NSTAT, 210, 50, &succ);
    int nor = uiTreeAddChk(s, NOR, 210 + 210 / 2 - 80 / 2, 140, &succ);
    s.y += 140 + 40;
    int n23stat = uiTreeAddChk(s, NSTAT, 0, 0, &succ);
    int n9stat = uiTreeAddChk(s, NSTAT, 210, 0, &succ);
    s.y += 90;
    int n23amt = uiTreeAddChk(s, NAMOUNT, 0, 0, &succ);
    int n9amt = uiTreeAddChk(s, NAMOUNT, 210, 0, &succ);
    s.y += 90;
    int nor2 = uiTreeAddChk(s, NOR, 210 - 80 / 2, 0, &succ);
    s.y += 60;
    int nres = uiTreeAddChk(s, NRESULT, 210/2, 0, &succ);
    s.y += 90;
    int ncomment = uiTreeAddComment(s0, 0, 0, 410, s.y - s0.y,
        "example: ((meso or drop) and 10+ stat) or 23+ stat", &succ);

    if (succ) {
      uiTreeDataByNode(n23stat)->value = uiTreeDataByNode(n9stat)->value = STAT_IDX;
      uiTreeDataByNode(n23amt)->value = 23;
      uiTreeDataByNode(n9amt)->value = 10;
      uiTreeDataByNode(ndrop)->value = DROP_IDX;
      uiTreeDataByNode(nmeso)->value = MESO_IDX;

      uiTreeLink(ndrop, nor);
      uiTreeLink(nmeso, nor);
      uiTreeLink(nor, n9stat);
      uiTreeLink(n9stat, n9amt);
      uiTreeLink(n9amt, nor2);

      uiTreeLink(n23stat, n23amt);
      uiTreeLink(n23amt, nor2);

      uiTreeLink(nor2, nres);

      uiTreeLink(nsplit, nres);
    }
  }
}

void examplesFamiliars() {
  struct nk_vec2 s = nk_vec2(20, 20);
  int succ = 1, nprevres;

  {
    int ncomment = uiTreeAddComment(s, 0, 0, 410, 400, "example: unique fam 30+ boss reveal", &succ);
    int nfamcat = uiTreeAddChk(s, NCATEGORY, 0, 50, &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 140, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 230, &succ);
    int nfamtier = uiTreeAddChk(s, NTIER, 210, 140, &succ);
    int nfamcube = uiTreeAddChk(s, NCUBE, 210, 230, &succ);
    int nres = uiTreeAddChk(s, NRESULT, 0, 320, &succ);
    int nsplit = nprevres = uiTreeAddChk(s, NSPLIT, 330, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nsplit)->value = nfamcat;
      uiTreeDataByNode(namt)->value = 30;
      uiTreeDataByNode(nfamcat)->value = FAMILIAR_STATS_IDX;
      uiTreeDataByNode(nfamcube)->value = FAMILIAR_IDX;
      uiTreeDataByNode(nfamtier)->value = UNIQUE_IDX;
      uiTreeDataByNode(nstat)->value = BOSS_IDX;
      uiTreeLink(nsplit, nfamtier);
      uiTreeLink(nstat, namt);
      uiTreeLink(nfamcube, namt);
      uiTreeLink(nfamtier, nfamcube);
      uiTreeLink(nfamcube, nres);
    }
  }

  {
    s.x += 450;
    int ncomment = uiTreeAddComment(s, 0, 0, 410, 310, "example: red cards 40+ boss", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nfamtier = uiTreeAddChk(s, NTIER, 210, 50, &succ);
    int nfamcube = uiTreeAddChk(s, NCUBE, 210, 140, &succ);
    int nres = uiTreeAddChk(s, NRESULT, 0, 230, &succ);

    if (succ) {
      uiTreeDataByNode(namt)->value = 40;
      uiTreeDataByNode(nfamcube)->value = RED_FAM_CARD_IDX;
      uiTreeDataByNode(nfamtier)->value = LEGENDARY_IDX;
      uiTreeDataByNode(nstat)->value = BOSS_IDX;
      uiTreeLink(nprevres, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(nfamcube, namt);
      uiTreeLink(nfamtier, nfamcube);
      uiTreeLink(nfamcube, nres);
    }
  }
}

void storageAfterCommit() {
  dbg("storage committed\n");
  presetList();
}

void storageCommit() {
#ifdef __EMSCRIPTEN__
  EM_ASM(
    FS.syncfs(function (err) {
      assert(!err);
      ccall('storageAfterCommit', 'v');
    });
  );
#else
  storageAfterCommit();
#endif
}

static int storageWriteSync(char* path, char* buf) {
  int res = 0;

  dbg("saving %s\n", path);

  FILE* f = fopen(path, "wb");
  if (!f) {
    perror("fopen");
  } else {
    if (fwrite(buf, 1, BufLen(buf), f) != BufLen(buf)) {
      perror("fwrite");
    } else {
      res = 1;
    }
    fclose(f);
  }

  return res;
}

static char* storageReadSync(char* path) {
  dbg("reading %s\n", path);

  struct stat st;
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
  char* rawData = 0;
  BufReserve(&rawData, st.st_size);
  if (fread(rawData, 1, st.st_size, f) != st.st_size) {
    perror("fread");
    BufFree(&rawData);
  }

  fclose(f);
  return rawData;
}

#ifdef __EMSCRIPTEN__
#define DATADIR "/data/"
#else
#define DATADIR "./data/"
#endif
#define GLOBALS_FILE DATADIR ".globals.bin"
#define EXTENSION ".maplecalcv2"
#define AUTOSAVE_FILE DATADIR "autosave" EXTENSION

int storageSaveGlobalsSync() {
  char* out = packGlobals(disclaimer);
  int res = storageWriteSync(GLOBALS_FILE, out);
  BufFree(&out);
  return res;
}

char* storageLoadGlobalsSync() {
  char* out = storageReadSync(GLOBALS_FILE);
  char* res = unpackGlobals(out);
  BufFree(&out);
  return res;
}

int storageSaveSync(char* path) {
  Arena* arena = ArenaInit();
  Allocator allocatorArena = ArenaAllocator(arena);
  char* out = packTree(&allocatorArena, &graph);
  int res = storageWriteSync(path, out);
  ArenaFree(arena);
  return res;
}

char* presetPath(char* path) {
  char* s = 0;
  size_t len = snprintf(0, 0, "%s%s%s", DATADIR, path, EXTENSION);
  BufReserve(&s, len + 1);
  snprintf(s, len + 1, "%s%s%s", DATADIR, path, EXTENSION);
  return s;
}

int storageLoadSync(char* path) {
  int res = 0;
  char* rawData = storageReadSync(path);
  if (rawData) {
    res = unpackTree(&graph, rawData);
    flags |= UPDATE_CONNECTIONS | DIRTY;
  } else {
    uiTreeClear();
  }
  BufFree(&rawData);
  return res;
}

void storageDeleteSync(char* path) {
  dbg("deleting %s\n", path);
  if (remove(path)) {
    perror("remove");
  }
  storageCommit();
}

int storageExists(char* path) {
  struct stat st;
  return stat(path, &st) == 0;
}

int presetSaveNoCommit(char* path) {
  char* s = presetPath(path);
  int res = storageSaveSync(s);
  BufFree(&s);
  return res;
}

int presetSave(char* path) {
  int res = presetSaveNoCommit(path);
  storageCommit();
  return res;
}

int presetLoad(char* path) {
  char* s = presetPath(path);
  int res = storageLoadSync(s);
  BufFree(&s);
  return res;
}

void presetDelete(char* path) {
  char* s = presetPath(path);
  storageDeleteSync(s);
  BufFree(&s);
}

int presetExists(char* path) {
  char* s = presetPath(path);
  int res = storageExists(s);
  BufFree(&s);
  return res;
}

static int qsortStrcmp(void const *a, void const *b) {
  char const** aa = (char const**)a;
  char const** bb = (char const**)b;
  return strcmp(*aa, *bb);
}

void presetList() {
  DIR* dir = opendir(DATADIR);
  if (!dir) {
    perror("opendir");
    return;
  }

  BufFreeClear((void**)presetFiles);

  struct dirent* data;
  while ((data = readdir(dir))) {
    if (*data->d_name == '.') {
      continue;
    }

    // remove file extension
    char* nameEnd = data->d_name + strlen(data->d_name);
    char* end = nameEnd - 1;

    for (; end > data->d_name && *end != '.'; --end);

    // no dot found
    if (end == data->d_name) {
      end = nameEnd;
    }

    size_t len = end - data->d_name;
    char* s = *BufAlloc(&presetFiles) = malloc(len + 1);
    memcpy(s, data->d_name, len);
    s[len] = 0;
  }

  closedir(dir);

  qsort(presetFiles, BufLen(presetFiles), sizeof(*presetFiles), qsortStrcmp);
}

#define examplesFile(x) \
  examplesFile_(DATADIR #x EXTENSION, examples##x)

void examplesFile_(char* path, void (* func)()) {
  if (!storageExists(path)) {
    uiTreeClear();
    func();
    storageSaveSync(path);
  }
}

// as far as I know, this is always called from the same thread as main so it should be fine
// to not have any synchronization
void storageAfterInit() {
  char* disc = storageLoadGlobalsSync();
  if (strcmp(disc, disclaimer)) {
    flags |= SHOW_DISCLAIMER;
  } else {
    flags &= ~SHOW_DISCLAIMER;
  }
  BufFree(&disc);

  examplesFile(BasicUsage);
  examplesFile(Operators);
  examplesFile(Familiars);
  examplesFile_(DATADIR "zzz_Preset1" EXTENSION, examplesBasicUsage);
  examplesFile_(DATADIR "zzz_Preset2" EXTENSION, examplesBasicUsage);
  examplesFile_(DATADIR "zzz_Preset3" EXTENSION, examplesBasicUsage);
  examplesFile_(DATADIR "zzz_Preset4" EXTENSION, examplesBasicUsage);
  examplesFile_(DATADIR "zzz_Preset5" EXTENSION, examplesBasicUsage);
  storageCommit();

  if (!storageLoadSync(AUTOSAVE_FILE)) {
    storageLoadSync(DATADIR "BasicUsage" EXTENSION);
  }
  status("");
}

void storageAutoSave() {
  // TODO: make async
  storageSaveSync(AUTOSAVE_FILE);
  storageCommit();
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

int main() {
  int res = 0;

  snprintf(presetFile, FILENAME_MAX, "autosave");
  treeGlobalInit();
  treeCalcGlobalInit();
  storageInit();

  glfwSetErrorCallback(errorCallback);
  if (!glfwInit()) {
    puts("[GFLW] failed to init!");
    res = 1;
    goto cleanup;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);
  win = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "MapleStory Average Cubing Cost", 0, 0);
  if (!win) {
    res = 1;
    goto cleanup;
  }
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
  for (char const* p = disclaimer; *p; ++p) {
    if (*p == '\n') {
      disclaimerHeight += rowHeight;
    }
  }
  disclaimerHeight = NK_MAX(rowHeight, disclaimerHeight);
  disclaimerHeight += nk->style.edit.padding.y * 2 + nk->style.edit.border * 2;

  if (isTouch()) {
    tool = PAN;
    nk_glfw3_set_left_button(toolToMouseButton[tool]);
  }

#ifdef __EMSCRIPTEN__
  resizeCanvas();
  updateWindowSize();

  nk_glfw3_set_scale_factor(deviceScaleFactor());
  emscripten_set_main_loop(loop, 0, 1);
#else
  nk_glfw3_set_scale_factor(1);
  while (!glfwWindowShouldClose(win)) {
    loop();
  }
#endif

cleanup:
  uiTreeFree();
  BufFreeClear((void**)presetFiles);
  BufFree(&presetFiles);
  BufFree(&errors);
  treeCalcGlobalFree();
  treeGlobalFree();
  if (win) nk_glfw3_shutdown();
  return res;
}
