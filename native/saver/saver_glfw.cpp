// Windows .scr (and a Linux test build). Windows launches a screensaver with:
//   /s            run full screen          /p <hwnd>   draw the small preview inside that window
//   /c            settings (none here)     no argument  same as /c
#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "core.h"
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#else
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#endif

static bool gQuit = false;
static double gStartX = -1e9, gStartY = 0, gArmedAt = 0;

static void hints() {
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_DECORATED, GLFW_FALSE); glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE); glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
}

int main(int argc, char** argv) {
  enum { RUN, PREVIEW, CONFIG, WINDOW } mode = CONFIG;
  std::string a = argc > 1 ? argv[1] : "", extra = argc > 2 ? argv[2] : "";
  for (auto& c : a) c = (char)tolower((unsigned char)c);
  if (a.size() >= 2 && (a[0] == '/' || a[0] == '-')) {
    if (a[1] == 's') mode = RUN;
    else if (a[1] == 'p') { mode = PREVIEW; if (a.size() > 3 && a[2] == ':') extra = a.substr(3); }
    else if (a[1] == 'w') mode = WINDOW;      // -w : plain window, for testing
  }
#ifdef _WIN32
  if (mode == CONFIG) {
    MessageBoxW(nullptr, L"This screensaver has no settings.\n\nIt starts after the wait time you set in Windows' Screen Saver Settings, and stops when you move the mouse or press a key.", L"Prismatica screensaver", MB_OK | MB_ICONINFORMATION);
    return 0;
  }
  HWND parent = nullptr;
  if (mode == PREVIEW) { parent = (HWND)(intptr_t)_strtoui64(extra.c_str(), nullptr, 10); if (!parent || !IsWindow(parent)) return 0; }
#else
  if (mode == CONFIG || mode == PREVIEW) mode = WINDOW;
#endif
  if (!glfwInit()) return 1;
  hints();
  GLFWwindow* win = nullptr; std::vector<GLFWwindow*> blanks;
  if (mode == RUN) {
    // the picture goes on the primary display; any other displays are blacked out
    int n = 0; GLFWmonitor** mons = glfwGetMonitors(&n); GLFWmonitor* primary = glfwGetPrimaryMonitor();
    for (int i = 0; i < n; i++) {
      const GLFWvidmode* m = glfwGetVideoMode(mons[i]);
      glfwWindowHint(GLFW_RED_BITS, m->redBits); glfwWindowHint(GLFW_GREEN_BITS, m->greenBits); glfwWindowHint(GLFW_BLUE_BITS, m->blueBits); glfwWindowHint(GLFW_REFRESH_RATE, m->refreshRate);
      GLFWwindow* w = glfwCreateWindow(m->width, m->height, "Prismatica", mons[i], nullptr);
      if (!w) continue;
      if (mons[i] == primary) win = w; else blanks.push_back(w);
    }
  } else {
    int w = 960, h = 540;
#ifdef _WIN32
    if (mode == PREVIEW) { RECT r; GetClientRect(parent, &r); w = std::max<int>(2, r.right); h = std::max<int>(2, r.bottom); glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE); }
#endif
    if (mode == WINDOW) { glfwWindowHint(GLFW_DECORATED, GLFW_TRUE); if (getenv("SAVER_W")) w = atoi(getenv("SAVER_W")); if (getenv("SAVER_H")) h = atoi(getenv("SAVER_H")); }
    win = glfwCreateWindow(w, h, "Prismatica", nullptr, nullptr);
#ifdef _WIN32
    if (win && mode == PREVIEW) {
      HWND hw = glfwGetWin32Window(win);
      SetWindowLongPtrW(hw, GWL_STYLE, WS_CHILD | WS_VISIBLE);
      SetWindowLongPtrW(hw, GWL_EXSTYLE, 0);
      SetParent(hw, parent);
      SetWindowPos(hw, nullptr, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
#endif
  }
  if (!win) {
#ifdef _WIN32
    if (mode == RUN) MessageBoxW(nullptr, L"Couldn't start OpenGL 3.3 on this computer.", L"Prismatica screensaver", MB_OK | MB_ICONERROR);
#endif
    glfwTerminate(); return 1;
  }
  for (GLFWwindow* b : blanks) { glfwMakeContextCurrent(b); glfwSwapInterval(0); glfwSetInputMode(b, GLFW_CURSOR, GLFW_CURSOR_HIDDEN); }
  glfwMakeContextCurrent(win);
  if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) { glfwTerminate(); return 1; }
  glfwSwapInterval(1);

  if (mode == RUN) {
    auto quit = [](GLFWwindow*, int, int, int, int) { gQuit = true; };
    auto click = [](GLFWwindow*, int, int, int) { gQuit = true; };
    auto scroll = [](GLFWwindow*, double, double) { gQuit = true; };
    auto move = [](GLFWwindow* w, double x, double y) {   // ignore the jitter of the window appearing under the cursor
      if (glfwGetTime() < gArmedAt) return;
      int wx, wy; glfwGetWindowPos(w, &wx, &wy); x += wx; y += wy;
      if (gStartX < -1e8) { gStartX = x; gStartY = y; return; }
      if (std::abs(x - gStartX) > 8 || std::abs(y - gStartY) > 8) gQuit = true;
    };
    gArmedAt = glfwGetTime() + .6;
    std::vector<GLFWwindow*> all = blanks; all.push_back(win);
    for (GLFWwindow* w : all) { glfwSetKeyCallback(w, quit); glfwSetMouseButtonCallback(w, click); glfwSetScrollCallback(w, scroll); glfwSetCursorPosCallback(w, move); glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_HIDDEN); }
    glfwFocusWindow(win);
  }

#ifndef _WIN32
  if (getenv("SAVER_VIDEO")) {                       // testing: raw RGB frames to stdout (pipe into ffmpeg)
    float fps = getenv("SAVER_FPS") ? (float)atof(getenv("SAVER_FPS")) : 30, secs = (float)atof(getenv("SAVER_VIDEO"));
    float warm = getenv("SAVER_WARM") ? (float)atof(getenv("SAVER_WARM")) : 0;
    int fw, fh; glfwGetFramebufferSize(win, &fw, &fh);
    std::vector<unsigned char> px(fw * 3 * fh), row(fw * 3);
    for (int i = 0; i < (int)(warm * 60); i++) saver::render(1 / 60.f, fw, fh);
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadBuffer(GL_BACK);
    for (int i = 0; i < (int)(secs * fps); i++) {
      saver::render(1 / fps, fw, fh);
      glReadPixels(0, 0, fw, fh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
      for (int y = fh - 1; y >= 0; y--) fwrite(&px[y * fw * 3], 1, fw * 3, stdout);
    }
    fflush(stdout); glfwTerminate(); return 0;
  }
  if (const char* shot = getenv("SAVER_SHOT")) {      // testing: simulate N seconds at 60 fps, save a PNG, exit
    float secs = getenv("SAVER_SECS") ? (float)atof(getenv("SAVER_SECS")) : 20;
    int fw, fh; glfwGetFramebufferSize(win, &fw, &fh);
    for (int i = 0; i < (int)(secs * 60); i++) saver::render(1 / 60.f, fw, fh);
    std::vector<unsigned char> px(fw * fh * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadBuffer(GL_BACK); glReadPixels(0, 0, fw, fh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    stbi_flip_vertically_on_write(1); stbi_write_png(shot, fw, fh, 3, px.data(), fw * 3);
    glfwTerminate(); return 0;
  }
#endif

  double last = glfwGetTime(), blankAt = 0;
  while (!gQuit && !glfwWindowShouldClose(win)) {
    glfwPollEvents();
#ifdef _WIN32
    if (mode == PREVIEW && !IsWindow(parent)) break;
#endif
    double now = glfwGetTime(); float dt = (float)(now - last); last = now;
    if (!blanks.empty() && now >= blankAt) {
      for (GLFWwindow* b : blanks) { glfwMakeContextCurrent(b); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); glfwSwapBuffers(b); }
      glfwMakeContextCurrent(win); blankAt = now + 2;
    }
    int fw, fh; glfwGetFramebufferSize(win, &fw, &fh);
    saver::render(dt, fw, fh);
    glfwSwapBuffers(win);
  }
  glfwTerminate();
  return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return main(__argc, __argv); }
#endif
