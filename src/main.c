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
#define NK_GLFW_GL2_IMPLEMENTATION

#include "thirdparty/nuklear.h"
#include "thirdparty/nuklear_glfw_gl2.h"
#include "utils.c"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

void errorCallback(int e, const char *d) {
  printf("Error %d: %s\n", e, d);
}

GLFWwindow* win;
struct nk_context* nk;
int width, height;

int* cubeIdx;
int* categoryIdx;
int* tierIdx;

/*
 * [
 *   calculator 0 = or([
 *     and([
 *      { key: value },
 *      { key: value },
 *      ...
 *     ])
 *   ]),
 *   calculator 1 = ...,
 *   ...
 * ]
 */
typedef struct _Pair {
  int key, value;
} Pair;

typedef Pair** Wants;

Wants* wants;

int* onein;
int* xy;

Wants defaultWants() {
  Wants wantsOr = 0;
  Pair* wantsAnd = 0;
  Pair* wants;
  wants = BufAlloc(&wantsAnd);
  wants->key = ATT;
  wants->value = 21;
  *BufAlloc(&wantsOr) = wantsAnd;
  return wantsOr;
}

void add(int x, int y) {
  *BufAlloc(&cubeIdx) = RED;
  *BufAlloc(&categoryIdx) = WEAPON;
  *BufAlloc(&tierIdx) = LEGENDARY;
  *BufAlloc(&wants) = defaultWants();
  *BufAlloc(&onein) = 0;
  *BufAlloc(&xy) = Pack16to32(x, y);
}

int fps;

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

void calc(int calcIdx) {
  float res;
  int andIdx, orIdx;

  pyCalcWantClear(calcIdx);

  for (andIdx = 0; andIdx < BufLen(wants[calcIdx]); ++andIdx) {
    for (orIdx = 0; orIdx < BufLen(wants[calcIdx][andIdx]); ++orIdx) {
      Pair* pair = &wants[calcIdx][andIdx][orIdx];
      pyCalcWant(calcIdx, andIdx, lineValues[pair->key], pair->value);
    }
  }

  pyCalcSet(calcIdx, calcparamValues[CATEGORY], categoryValues[categoryIdx[calcIdx]]);
  pyCalcSet(calcIdx, calcparamValues[CUBE], cubeValues[cubeIdx[calcIdx]]);
  pyCalcSet(calcIdx, calcparamValues[TIER], tierValues[tierIdx[calcIdx]]);
  res = pyCalc(calcIdx);

  onein[calcIdx] = res ? (int)(1/res + 0.5) : 0;
  printf("[%d] one in %d (%f%%)\n", calcIdx, onein[calcIdx], res * 100);
}

void loop() {
  int i, j, k;

  glfwPollEvents();
  nk_glfw3_new_frame();

  for (i = 0; i < BufLen(cubeIdx); ++i) {
    if (nk_begin(nk, "Average Cubing Cost", nk_rect(HiWord(xy[i]), LoWord(xy[i]), 230, 250),
        NK_WINDOW_BORDER|NK_WINDOW_MOVABLE|NK_WINDOW_SCALABLE|
        NK_WINDOW_MINIMIZABLE|NK_WINDOW_TITLE))
    {
      nk_layout_row_dynamic(nk, 25, 1);

      for (j = 0; j < BufLen(wants[i]); ++j) {
        nk_label(nk, j == 0 ? "OR at least" : "at least:", NK_TEXT_LEFT);
        for (k = 0; k < BufLen(wants[i][j]); ++k) {
          Pair* pair = &wants[i][j][k];
          /* TODO: appropriate input for different types of stats */
          nk_property_int(nk, lineNames[pair->key], 0, &pair->value, 100, 3, 1);
        }
      }

#define combo(x) x##Idx[i] = nk_combo(nk, x##Names, NK_LEN(x##Names), x##Idx[i], \
      25, nk_vec2(nk_widget_width(nk), 100))

      combo(cube);
      combo(category);

      if (nk_button_label(nk, "calculate")) {
        calc(i);
      }

      nk_value_int(nk, "one in ", onein[i]);
    }
    nk_end(nk);
  }

  if (nk_begin(nk, "Stats", nk_rect(350, 50, 150, 100),
               NK_WINDOW_BORDER|NK_WINDOW_MOVABLE|NK_WINDOW_SCALABLE|
               NK_WINDOW_MINIMIZABLE|NK_WINDOW_TITLE))
  {
    nk_layout_row_dynamic(nk, 10, 1);
    nk_value_int(nk, "FPS:", fps);
  }
  nk_end(nk);

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
  nk_glfw3_render(NK_ANTI_ALIASING_ON);
  glfwSwapBuffers(win);

  updateFPS();
}

int main() {
  pyInit(TS);
  add(50, 50);

  glfwSetErrorCallback(errorCallback);
  if (!glfwInit()) {
      fprintf(stdout, "[GFLW] failed to init!\n");
      exit(1);
  }
  win = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "MapleStory Average Cubing Cost", 0, 0);
  glfwMakeContextCurrent(win);
  glfwGetWindowSize(win, &width, &height);

  nk = nk_glfw3_init(win, NK_GLFW3_INSTALL_CALLBACKS);

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
