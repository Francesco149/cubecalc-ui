#include "generated.c"

#include <stdio.h>

/* TODO: figure out a way to embed numpy + python for non-browser version?
 * ... or just port the calculator to C */
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

EM_ASYNC_JS(void, pyInit, (int ts), {
  py = await loadPyodide();
  await py.loadPackage("micropip");

  // the easiest way to install my cubecalc into pyodide is to zip it and extract it in the
  // virtual file system
  let zipResponse = await fetch("cubecalc.zip?ts=" + ts);
  let zipBinary = await zipResponse.arrayBuffer();
  py.unpackArchive(zipBinary, "zip");

  // install pip packages, init glue code etc
  pa = `import sys; sys.path.append("cubecalc/"); `;
  init = py.runPython(`${pa} from init import init; init`);
  await init();

  Module.pyFunc = (x) => py.runPython(`${pa} from glue import ${x}; ${x}`);
});

EM_JS(void, pyCalcFree, (int calcIdx), {
  return Module.pyFunc("calc_free")(calcIdx);
});

EM_JS(void, pyCalcSet, (int calcIdx, int key, int value), {
  return Module.pyFunc("calc_set")(calcIdx, key, value);
});

EM_JS(void, pyCalcWant, (int calcIdx, int wantsIdx, int key, float value), {
  return Module.pyFunc("calc_want")(calcIdx, wantsIdx, key, value);
});

EM_JS(void, pyCalcWantClear, (int calcIdx), {
  return Module.pyFunc("calc_want_clear")(calcIdx);
});

EM_JS(float, pyCalc, (int calcIdx), {
  return Module.pyFunc("calc")(calcIdx);
});
#endif

#define NK_IMPLEMENTATION
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
/*#define NK_INCLUDE_FIXED_TYPES*/
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR

/* required by glfw backend */
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
};

GLFWwindow* win;
struct nk_context* nk;
struct nk_input* in;
int width, height;
int fps;
int flags = SHOW_INFO | SHOW_GRID;

/* macro to generate things based on the list of node types */
#define nodeTypes(f) \
  f(NCUBE) \
  f(NTIER) \
  f(NCATEGORY) \
  f(NSTAT) \
  f(NAMOUNT) \
  f(NAVERAGE) \

#define stringifyComma(x) #x,
char* nodeNames[] = { nodeTypes(stringifyComma) };

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

/*
 * we need the data to be nicely packed in memory so that we don't have to traverse a tree
 * when we draw the nodes since that would be slow.
 * but we also need a tree that represents relationships between nodes which is only traversed
 * when we calculate or when we change a connection
 */

typedef struct _Node {
  int type;
  int data; /* index into the data array of this type. updated on add/remove */
  int* connections; /* index into nodes array, updated on add/remove */
} Node;

typedef struct _NodeData {
  struct nk_rect bounds;
  int value, node;
  char name[16];
  struct nk_panel* panel;
} NodeData;

Node* tree;
NodeData* data[NLAST];
char** errors;

void treeInit() {
  treeInitNodeNames();
}

void treeUpdateConnections() {
  // todo
}

void error(char* s) {
  *BufAlloc(&errors) = s;
  fprintf(stderr, "%s\n", s);
}

void treeAdd(int type, int x, int y) {
  NodeData* d = BufAlloc(&data[type]);
  Node* n;
  int chars;

  d->bounds = nk_rect(x, y, 150, 80);

  switch (type) {
    case NCUBE:
      d->value = RED;
      break;
    case NTIER:
      d->value = LEGENDARY;
      break;
    case NCATEGORY:
      d->value = WEAPON;
      break;
    case NSTAT:
      d->value = ATT;
      break;
    case NAMOUNT:
      d->value = 21;
      break;
    default:
      d->value = 0;
  }

  chars = snprintf(d->name, sizeof(d->name), "%s %zu", nodeNames[type - 1], BufLen(data[type]));
  if (chars >= sizeof(d->name)) {
    error("too many nodes, can't format node name");
    BufDel(data[type], BufLen(data[type]) - 1);
    return;
  }

  n = BufAlloc(&tree);
  n->data = BufLen(data[type]) - 1;
  n->type = type;
  n->connections = 0;
  d->node = BufLen(tree) - 1;
}

void treeDel(int type, int index) {
  NodeData* d = &data[type][index];

  // since we are deleting an element in the packed arrays we have to adjust all indices pointing
  // after it. this means we have slow add/del but fast iteration which is what we want
  for (int i = 0; i < BufLen(tree); ++i) {
    if (i == d->node) continue;
    Node* n = &tree[i];

    // adjust connections indices
    int* newConnections = 0;
    for (int j = 0; j < BufLen(n->connections); ++j) {
      if (n->connections[j] > d->node) {
        *BufAlloc(&newConnections) = n->connections[j] - 1;
      } else if (n->connections[j] < d->node) {
        *BufAlloc(&newConnections) = n->connections[j];
      }
      // if the node is referring to the node we're removing, just remove the connection.
      // TODO: try to connect to removed node's connections?
    }
    BufFree(&n->connections);
    n->connections = newConnections;

    // adjust data indices
    if (n->data > index) {
      --n->data;
    }
  }

  // adjust node indices
  for (int i = 0; i < BufLen(data[type]); ++i) {
    if (i == index) continue;
    if (data[type][i].node > d->node) {
      --data[type][i].node;
    }
  }

  // actually delete elements and shift everything after them left by 1
  BufDel(tree, d->node);
  BufDel(data[type], index);

  treeUpdateConnections();
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

#define NODE_WINDOW_FLAGS \
  NK_WINDOW_BORDER | \
  NK_WINDOW_MOVABLE | \
  NK_WINDOW_SCALABLE | \
  NK_WINDOW_TITLE | \
  0

int uiBeginNode(int type, int i, int h) {
  NodeData* d = &data[type][i];
  nk_layout_space_push(nk, d->bounds);
  int res = nk_group_begin(nk, d->name, NODE_WINDOW_FLAGS);
  if (res) {
    d->panel = nk_window_get_panel(nk);
    nk_layout_row_dynamic(nk, h, 1);
    // TODO: this doesn't work if the parent window is moved. it gets offset by parent pos
    if (nk_contextual_begin(nk, 0, nk_vec2(100, 220), d->panel->bounds)) {
      nk_layout_row_dynamic(nk, CONTEXT_HEIGHT, 1);
      if (nk_contextual_item_label(nk, "Remove", NK_TEXT_CENTERED)) {
        treeDel(type, i);
      }
      nk_contextual_end(nk);
    }
  }
  return res;
}

void uiEndNode(int type, int i) {
  nk_group_end(nk);

  // get node position after dragging it (if dragged) and update the draw position for next frame
  // TODO: handle panning around
  NodeData* d = &data[type][i];
  d->bounds = nk_layout_space_rect_to_local(nk, d->panel->bounds);
}

void loop() {
  int i, j;

  glfwPollEvents();
  nk_glfw3_new_frame();

  if (nk_begin(nk, "MapleStory Cubing Calculator", nk_rect(0, 0, 640, 480),
      NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE |
      NK_WINDOW_SCALABLE))
  {
    struct nk_command_buffer* canvas = nk_window_get_canvas(nk);
    struct nk_rect totalSpace = nk_window_get_content_region(nk);

    nk_layout_space_begin(nk, NK_STATIC, totalSpace.h, BufLen(tree));

    if (flags & SHOW_GRID) {
      struct nk_rect bnds = nk_layout_space_bounds(nk);
      float x, y;
      const float gridSize = 32.0f;
      const struct nk_color gridColor = nk_rgb(60, 60, 60);
      for (x = (float)fmod(bnds.x, gridSize); x < bnds.w; x += gridSize) {
        nk_stroke_line(canvas, x + bnds.x, bnds.y, x + bnds.x, bnds.y + bnds.h, 1.0f, gridColor);
      }
      for (y = (float)fmod(bnds.y, gridSize); y < bnds.h; y += gridSize) {
        nk_stroke_line(canvas, bnds.x, y + bnds.y, bnds.x + bnds.w, y + bnds.y, 1.0f, gridColor);
      }
    }

#define comboNode(type, enumName) \
  for (i = 0; i < BufLen(data[type]); ++i) { \
    if (uiBeginNode(type, i, 25)) { \
      NodeData* d = &data[type][i]; \
      d->value = nk_combo(nk, enumName##Names, NK_LEN(enumName##Names), d->value, \
        25, nk_vec2(nk_widget_width(nk), 100)); \
      uiEndNode(type, i); \
    } \
  }

#define valueNode(type, valueType, text) \
  for (i = 0; i < BufLen(data[type]); ++i) { \
    if (uiBeginNode(type, i, 10)) { \
      nk_value_##valueType(nk, text, data[type][i].value); \
      uiEndNode(type, i); \
    } \
  }

#define propNode(type, valueType, text) \
  for (i = 0; i < BufLen(data[type]); ++i) { \
    if (uiBeginNode(type, i, 20)) { \
      data[type][i].value = \
        nk_property##valueType(nk, text, 0, data[type][i].value, 120, 1, 0.02); \
      uiEndNode(type, i); \
    } \
  }

    comboNode(NCUBE, cube);
    comboNode(NTIER, tier);
    comboNode(NCATEGORY, category);
    comboNode(NSTAT, line);
    propNode(NAMOUNT, i, "amount");
    valueNode(NAVERAGE, float, "average 1 in");

    if (nk_contextual_begin(nk, 0, nk_vec2(100, 220), nk_window_get_bounds(nk))) {
      nk_layout_row_dynamic(nk, CONTEXT_HEIGHT, 1);

      for (int i = 0; i < NK_LEN(nodeNames); ++i) {
        if (nk_contextual_item_label(nk, nodeNames[i], NK_TEXT_CENTERED)) {
          struct nk_vec2 pos = in->mouse.pos;
          treeAdd(i + 1, pos.x, pos.y);
        }
      }

#define flag(x, text) (void)( \
      nk_contextual_item_label(nk, (flags & x) ? "Hide " text : "Show " text, NK_TEXT_CENTERED) &&\
        (flags ^= x))

      flag(SHOW_INFO, "Info");
      flag(SHOW_GRID, "Grid");

      nk_contextual_end(nk);
    }

    nk_layout_space_end(nk);
  }
  nk_end(nk);

  if (flags & SHOW_INFO) {
    if (nk_begin(nk, "Info", nk_rect(700, 50, 150, 100), NODE_WINDOW_FLAGS | NK_WINDOW_CLOSABLE)) {
      nk_layout_row_dynamic(nk, 10, 1);
      nk_value_int(nk, "FPS", fps);
      nk_value_int(nk, "Nodes", BufLen(tree));
    } else {
      flags &= ~SHOW_INFO;
    }
    nk_end(nk);
  }

  if (BufLen(errors)) {
    if (nk_begin(nk, "Error", nk_rect(width / 2 - 200, height / 2 - 100, 400, 200),
                 NODE_WINDOW_FLAGS)) {
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
  {
    int w, h;
    w = canvas_get_width();
    h = canvas_get_height();
    if (w != width || h != height) {
      width = w;
      height = h;
      glfwSetWindowSize(win, width, height);
    }
  }
#else
  glfwGetWindowSize(win, &width, &height);
#endif

  glViewport(0, 0, width, height);
  glClear(GL_COLOR_BUFFER_BIT);
  nk_glfw3_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY);
  glfwSwapBuffers(win);

  updateFPS();
}

int main() {
  pyInit(TS);
  treeInit();

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

  /* this is required even if empty */
  {
    struct nk_font_atlas *atlas;
    nk_glfw3_font_stash_begin(&atlas);
    nk_glfw3_font_stash_end();
  }

#ifdef __EMSCRIPTEN__
  resizeCanvas();
  emscripten_set_main_loop(loop, 0, 1);
#else
  while (!glfwWindowShouldClose(win)) {
    loop();
  }
#endif

  return 0;
}
