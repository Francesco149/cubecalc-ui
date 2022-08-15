#include "generated.c"

#include <stdio.h>

int width, height;

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

EM_JS(void, pyCalcFree, (int i), {
  return Module.pyFunc("calc_free")();
});

EM_JS(void, pyCalcSet, (int i, int k, int v), {
  return Module.pyFunc("calc_set")(i, k, v);
});

EM_JS(void, pyCalcWant, (int i, int k, float v), {
  return Module.pyFunc("calc_want")(i, k, v);
});

EM_JS(void, pyCalcWantClear, (int i), {
  return Module.pyFunc("calc_want_clear")(i);
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

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

typedef NK_UINT32 U32;
typedef NK_SIZE_TYPE Size;

static void errorCallback(int e, const char *d) {
  printf("Error %d: %s\n", e, d);
}

GLFWwindow* win;
struct nk_context* nk;
int frames, fps;
double framesTimer;
int cubeIndex = RED;
int categoryIndex = WEAPON;
float stat = 21;

void updateFPS() {
  double t = glfwGetTime();
  ++frames;
  if (t - framesTimer >= 1) {
    framesTimer = t;
    fps = frames;
    frames = 0;
  }
}

void loop() {
  glfwPollEvents();
  nk_glfw3_new_frame();

  if (nk_begin(nk, "Average Cubing Cost", nk_rect(50, 50, 230, 250),
      NK_WINDOW_BORDER|NK_WINDOW_MOVABLE|NK_WINDOW_SCALABLE|
      NK_WINDOW_MINIMIZABLE|NK_WINDOW_TITLE))
  {
    nk_layout_row_dynamic(nk, 25, 1);

    nk_property_float(nk, "stat:", 0, &stat, 36, 3, 1);
    nk_label(nk, "cube:", NK_TEXT_LEFT);
#define combo(x) x##Index = nk_combo(nk, x##Names, NK_LEN(x##Names), x##Index, \
    25, nk_vec2(nk_widget_width(nk), 100))

    combo(cube);
    combo(category);

    if (nk_button_label(nk, "calculate")) {
      /* do shit */
    }
  }
  nk_end(nk);

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

  /*
  pyCalcWantClear(0);
  pyCalcWant(0, MAINSTAT | ALLSTAT, 21);*/

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
