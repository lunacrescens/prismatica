// Prismatica - psychedelic music visualizer. Native C++ / OpenGL 3.3 core (4.1 core on macOS).
#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "portable-file-dialogs.h"
#include "audio.h"
#include "scenes.h"
#include "shaders.h"
#include "font_roboto.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ============================================================ GL helpers
static GLuint compile(GLenum type, const char* src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof log, nullptr, log); fprintf(stderr, "shader error:\n%s\n", log); }
  return s;
}
struct Program {
  GLuint id = 0;
  GLint params[PARAM_COUNT];
  GLint uRes, uTime, uAT, uBass, uMid, uTreb, uBeat, uBeatAge, uDt60, palShift, uAudio, uPrev, uTex, uN;
  void build(const char* vs, const char* fsrc) {
    id = glCreateProgram();
    GLuint a = compile(GL_VERTEX_SHADER, vs), b = compile(GL_FRAGMENT_SHADER, fsrc);
    glAttachShader(id, a); glAttachShader(id, b); glLinkProgram(id);
    GLint ok = 0; glGetProgramiv(id, GL_LINK_STATUS, &ok);
    if (!ok) { char log[4096]; glGetProgramInfoLog(id, sizeof log, nullptr, log); fprintf(stderr, "link error:\n%s\n", log); }
    glDeleteShader(a); glDeleteShader(b);
    for (int i = 0; i < PARAM_COUNT; i++) params[i] = glGetUniformLocation(id, PARAM_NAMES[i]);
    auto u = [&](const char* n) { return glGetUniformLocation(id, n); };
    uRes = u("uRes"); uTime = u("uTime"); uAT = u("uAT"); uBass = u("uBass"); uMid = u("uMid"); uTreb = u("uTreb");
    uBeat = u("uBeat"); uBeatAge = u("uBeatAge"); uDt60 = u("uDt60"); palShift = u("palShift");
    uAudio = u("uAudio"); uPrev = u("uPrev"); uTex = u("uTex"); uN = u("uN");
  }
};
struct Target { GLuint tex = 0, fbo = 0; };

// ============================================================ state
static GLFWwindow* gWin;
static Program gF, gP, gC;
static GLuint gVao, gAudioTex;
static Target gT[2];
static int gSimW = 0, gSimH = 0, gFlip = 0;
static int gQuality = 1;                   // 0: 720p, 1: 1080p, 2: native
static const char* QUALITY_NAMES[] = {"720p", "1080p", "Native"};
static bool gHalfFloat = true;

static SceneParams gCur = SCENES[0].p, gFrom = gCur;
static int gScene = 0;
static float gFade = 1, gSceneAge = 0, gTime = 0;
static bool gAuto = true;

static std::vector<std::string> gPlaylist;
static int gTrack = -1;
static std::string gTitle = "No audio";
static float gVolume = .8f;
static bool gWelcome = true, gShowKeys = false, gHideUI = false;
static std::string gToast; static float gToastT = -10;
static double gLastActivity = 0;
static bool gFullscreen = false; static int gWinX, gWinY, gWinW = 1280, gWinH = 720;
static std::vector<std::string> gDropped;
static float gUiScale = 1;
static ImFont* gBigFont = nullptr;

static void toast(const std::string& s) { gToast = s; gToastT = (float)glfwGetTime(); }

// ============================================================ render targets
static Target makeTarget(int w, int h) {
  Target t;
  glGenTextures(1, &t.tex); glBindTexture(GL_TEXTURE_2D, t.tex);
  if (gHalfFloat) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
  else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
  glGenFramebuffers(1, &t.fbo); glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE && gHalfFloat) {
    glDeleteFramebuffers(1, &t.fbo); glDeleteTextures(1, &t.tex); gHalfFloat = false; return makeTarget(w, h);
  }
  glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
  glGenerateMipmap(GL_TEXTURE_2D);
  return t;
}
static void ensureTargets(int fbW, int fbH) {
  int cap = gQuality == 0 ? 720 : gQuality == 1 ? 1080 : 100000;
  int w = fbW, h = fbH;
  if (h > cap) { w = (int)std::lround(w * (double)cap / h); h = cap; }
  w = std::max(2, w); h = std::max(2, h);
  if (w == gSimW && h == gSimH) return;
  for (auto& t : gT) if (t.fbo) { glDeleteFramebuffers(1, &t.fbo); glDeleteTextures(1, &t.tex); }
  gSimW = w; gSimH = h;
  gT[0] = makeTarget(w, h); gT[1] = makeTarget(w, h);
}

// ============================================================ scenes
static void setScene(int i, bool instant = false) {
  gScene = ((i % SCENE_COUNT) + SCENE_COUNT) % SCENE_COUNT;
  gFrom = gCur; gFade = instant ? 1 : 0; gSceneAge = 0;
  if (instant) gCur = SCENES[gScene].p;
  toast(SCENES[gScene].name);
}
static void stepScene(float dt, const Levels& L) {
  gSceneAge += dt;
  if (gAuto && gSceneAge > 24 && (L.beat > .9f || gSceneAge > 30)) setScene(gScene + 1);
  if (gFade < 1) {
    gFade = std::min(1.f, gFade + dt / 3.5f);
    float k = gFade * gFade * (3 - 2 * gFade);
    const SceneParams& to = SCENES[gScene].p;
    for (int i = 0; i < PARAM_COUNT; i++) *paramPtr(gCur, i) = *paramPtr(gFrom, i) + (*paramPtr((SceneParams&)to, i) - *paramPtr(gFrom, i)) * k;
    gCur.kalN = (to.kal > 0 ? to : gFrom).kalN;
    gCur.flowerK = (k > .5f ? to : gFrom).flowerK;
  }
}

// ============================================================ frame
static void setCommon(const Program& p, const Levels& L, float dt, float w, float h) {
  glUniform2f(p.uRes, w, h); glUniform1f(p.uTime, gTime); glUniform1f(p.uAT, L.at);
  glUniform1f(p.uBass, L.bass); glUniform1f(p.uMid, L.mid); glUniform1f(p.uTreb, L.treb);
  glUniform1f(p.uBeat, L.beat); glUniform1f(p.uBeatAge, L.beatAge); glUniform1f(p.uDt60, std::min(3.f, dt * 60));
  for (int i = 0; i < PARAM_COUNT; i++) if (p.params[i] >= 0) glUniform1f(p.params[i], *paramPtr(gCur, i));
  if (p.palShift >= 0) glUniform1f(p.palShift, gCur.palShift + gTime * .011f);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gAudioTex);
  if (p.uAudio >= 0) glUniform1i(p.uAudio, 1);
}

static void renderFrame(float dt, int fbW, int fbH, const Levels& L) {
  ensureTargets(fbW, fbH);
  gTime += dt;
  stepScene(dt, L);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gAudioTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, audio::TEXW, 2, GL_RED, GL_UNSIGNED_BYTE, audio::texture());
  Target& src = gT[gFlip]; Target& dst = gT[1 - gFlip]; gFlip = 1 - gFlip;
  glBindVertexArray(gVao);

  glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo); glViewport(0, 0, gSimW, gSimH); glDisable(GL_BLEND);
  glUseProgram(gF.id); setCommon(gF, L, dt, (float)gSimW, (float)gSimH);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, src.tex); glUniform1i(gF.uPrev, 0);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  if (gCur.pStar + gCur.pPhyl > .01f) {
    const int N = 7000;
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR); glEnable(GL_PROGRAM_POINT_SIZE);
    glUseProgram(gP.id); setCommon(gP, L, dt, (float)gSimW, (float)gSimH); glUniform1f(gP.uN, (float)N);
    glDrawArrays(GL_POINTS, 0, N); glDisable(GL_BLEND);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, fbW, fbH);
  glUseProgram(gC.id); setCommon(gC, L, dt, (float)fbW, (float)fbH);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, dst.tex); glGenerateMipmap(GL_TEXTURE_2D);
  glUniform1i(gC.uTex, 0);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

// ============================================================ playback helpers
static bool isAudioFile(const fs::path& p) {
  std::string e = p.extension().string();
  std::transform(e.begin(), e.end(), e.begin(), ::tolower);
  static const char* exts[] = {".mp3", ".flac", ".wav", ".ogg", ".oga", ".m4a", ".aac", ".aif", ".aiff", ".caf", ".wma", ".alac", ".mp4"};
  for (auto x : exts) if (e == x) return true;
  return false;
}
static std::string baseName(const std::string& path) {
  fs::path p = fs::u8path(path);
  return p.stem().u8string();
}
static void playTrack(int i) {
  if (gPlaylist.empty()) return;
  int n = (int)gPlaylist.size();
  for (int tries = 0; tries < n; tries++) {
    gTrack = (((i + tries) % n) + n) % n;
    std::string err;
    if (audio::playFile(gPlaylist[gTrack], &err)) { gTitle = baseName(gPlaylist[gTrack]); gWelcome = false; return; }
    toast("Can't play " + baseName(gPlaylist[gTrack]) + ": " + err);
  }
}
static void addFiles(const std::vector<std::string>& paths) {
  std::vector<std::string> list;
  for (auto& s : paths) {
    std::error_code ec; fs::path p = fs::u8path(s);
    if (fs::is_directory(p, ec)) {
      for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
        if (!ec && it->is_regular_file(ec) && isAudioFile(it->path())) list.push_back(it->path().u8string());
    } else if (isAudioFile(p)) list.push_back(s);
  }
  if (list.empty()) { toast("No playable audio files"); return; }
  std::sort(list.begin(), list.end());
  gPlaylist = list;
  playTrack(0);
}
static void openFilesDialog() {
  auto sel = pfd::open_file("Open music", "", {"Audio files", "*.mp3 *.flac *.wav *.ogg *.oga *.m4a *.aac *.aif *.aiff *.caf *.wma *.mp4", "All files", "*"}, pfd::opt::multiselect).result();
  if (!sel.empty()) addFiles(sel);
}
static void startDemo() { audio::playDemo(); gTitle = "Demo track"; gWelcome = false; }
static void startSystem() {
  std::string err;
  if (audio::startSystem(&err)) { gTitle = "System audio"; gWelcome = false; }
  else toast(err);
}
static void startInput(int idx, const std::string& name) {
  std::string err;
  if (audio::startInput(idx, &err)) { gTitle = name; gWelcome = false; }
  else toast(err);
}
static void playPause() {
  Source s = audio::source();
  if (s == Source::Files || s == Source::Demo) audio::togglePause();
  else if (s == Source::None) startDemo();
}
static void setAuto(bool v) { gAuto = v; toast(v ? "Auto-cycle on" : "Scene locked"); }

static void toggleFullscreen() {
  if (!gFullscreen) {
    glfwGetWindowPos(gWin, &gWinX, &gWinY); glfwGetWindowSize(gWin, &gWinW, &gWinH);
    int cx = gWinX + gWinW / 2, cy = gWinY + gWinH / 2, count = 0;
    GLFWmonitor** mons = glfwGetMonitors(&count); GLFWmonitor* best = glfwGetPrimaryMonitor();
    for (int i = 0; i < count; i++) { int mx, my; glfwGetMonitorPos(mons[i], &mx, &my); const GLFWvidmode* m = glfwGetVideoMode(mons[i]); if (cx >= mx && cx < mx + m->width && cy >= my && cy < my + m->height) best = mons[i]; }
    const GLFWvidmode* mode = glfwGetVideoMode(best);
    glfwSetWindowMonitor(gWin, best, 0, 0, mode->width, mode->height, mode->refreshRate);
  } else {
    glfwSetWindowMonitor(gWin, nullptr, gWinX, gWinY, gWinW, gWinH, 0);
  }
  gFullscreen = !gFullscreen;
  glfwSwapInterval(1);
}

// ============================================================ UI
enum Icon { IC_PLAY, IC_PAUSE, IC_PREV, IC_NEXT, IC_LEFT, IC_RIGHT, IC_FULL };
static bool iconButton(const char* id, Icon ic, bool active = false) {
  float s = ImGui::GetFrameHeight();
  if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, .9f));
  bool r = ImGui::Button(id, ImVec2(s * 1.15f, s));
  if (active) ImGui::PopStyleColor();
  ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(), c((a.x + b.x) * .5f, (a.y + b.y) * .5f);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImU32 col = active ? IM_COL32(0, 0, 0, 255) : IM_COL32(235, 235, 235, 255);
  float h = s * .22f;
  auto tri = [&](float x, float dir) { dl->AddTriangleFilled(ImVec2(x - h * .8f * dir, c.y - h), ImVec2(x - h * .8f * dir, c.y + h), ImVec2(x + h * .8f * dir, c.y), col); };
  switch (ic) {
    case IC_PLAY: tri(c.x + h * .1f, 1); break;
    case IC_PAUSE: dl->AddRectFilled(ImVec2(c.x - h * .8f, c.y - h), ImVec2(c.x - h * .25f, c.y + h), col); dl->AddRectFilled(ImVec2(c.x + h * .25f, c.y - h), ImVec2(c.x + h * .8f, c.y + h), col); break;
    case IC_PREV: tri(c.x + h * .2f, -1); dl->AddRectFilled(ImVec2(c.x - h * 1.1f, c.y - h), ImVec2(c.x - h * .75f, c.y + h), col); break;
    case IC_NEXT: tri(c.x - h * .2f, 1); dl->AddRectFilled(ImVec2(c.x + h * .75f, c.y - h), ImVec2(c.x + h * 1.1f, c.y + h), col); break;
    case IC_LEFT: tri(c.x, -1); break;
    case IC_RIGHT: tri(c.x, 1); break;
    case IC_FULL: {
      float k = h * 1.05f, l = h * .5f;
      for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) {
        ImVec2 p(c.x + sx * k, c.y + sy * k);
        dl->AddLine(p, ImVec2(p.x - sx * l, p.y), col, 1.6f * gUiScale); dl->AddLine(p, ImVec2(p.x, p.y - sy * l), col, 1.6f * gUiScale);
      }
    } break;
  }
  return r;
}
static bool sourceButton(const char* label, bool on) {
  if (on) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, .92f)); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1)); }
  bool r = ImGui::Button(label);
  if (on) ImGui::PopStyleColor(2);
  return r;
}
static std::string fmtTime(double s) {
  if (!(s >= 0) || !std::isfinite(s)) s = 0;
  char b[32]; snprintf(b, sizeof b, "%d:%02d", (int)s / 60, (int)s % 60); return b;
}

static void inputPopupItems() {
  auto devs = audio::inputDevices();
  if (ImGui::Selectable("Default microphone")) startInput(-1, "Microphone");
  ImGui::Separator();
  for (int i = 0; i < (int)devs.size(); i++) {
    std::string label = devs[i].name + (devs[i].isDefault ? "  (default)" : "") + "##dev" + std::to_string(i);
    if (ImGui::Selectable(label.c_str())) startInput(i, devs[i].name);
  }
  if (devs.empty()) ImGui::TextDisabled("No input devices found");
#ifdef __APPLE__
  ImGui::Separator();
  ImGui::TextDisabled("Tip: a loopback device such as BlackHole\nworks as system audio on any macOS.");
#endif
}

static void drawUI(float uiAlpha) {
  ImGuiIO& io = ImGui::GetIO();
  ImVec2 ds = io.DisplaySize;
  float pad = 14 * gUiScale;
  Source src = audio::source();

  // ---- toast
  float tAge = (float)glfwGetTime() - gToastT;
  if (tAge < 3.2f && !gToast.empty()) {
    float a = std::clamp(std::min(tAge / .3f, (3.2f - tAge) / .6f), 0.f, 1.f) * .92f;
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    std::string up = gToast; for (auto& ch : up) ch = (char)toupper((unsigned char)ch);
    ImVec2 p(22 * gUiScale, 18 * gUiScale);
    for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) fg->AddText(ImVec2(p.x + dx * 1.5f, p.y + dy * 1.5f + 1), IM_COL32(0, 0, 0, (int)(110 * a)), up.c_str());
    fg->AddText(p, IM_COL32(255, 255, 255, (int)(255 * a)), up.c_str());
  }

  // ---- welcome
  if (gWelcome) {
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 110));
    ImGui::SetNextWindowPos(ImVec2(ds.x * .5f, ds.y * .46f), ImGuiCond_Always, ImVec2(.5f, .5f));
    ImGui::SetNextWindowBgAlpha(0);
    ImGui::Begin("##welcome", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    float bw = 250 * gUiScale, bh = 78 * gUiScale, gap = 12 * gUiScale;
    float totalW = bw * 2 + gap;
    // title, letter-spaced
    ImGui::PushFont(gBigFont);
    const char* title = "PRISMATICA";
    float spacing = gBigFont->FontSize * .45f, tw = 0;
    for (const char* c = title; *c; c++) tw += ImGui::CalcTextSize(c, c + 1).x + (c[1] ? spacing : 0);
    ImVec2 cur = ImGui::GetCursorScreenPos();
    float x = cur.x + (totalW - tw) * .5f;
    for (const char* c = title; *c; c++) { ImGui::GetWindowDrawList()->AddText(ImVec2(x, cur.y), IM_COL32(255, 255, 255, 240), c, c + 1); x += ImGui::CalcTextSize(c, c + 1).x + spacing; }
    ImGui::Dummy(ImVec2(totalW, gBigFont->FontSize));
    ImGui::PopFont();
    const char* sub = "Pick something to listen to";
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (totalW - ImGui::CalcTextSize(sub).x) * .5f);
    ImGui::TextColored(ImVec4(1, 1, 1, .7f), "%s", sub);
    ImGui::Dummy(ImVec2(0, 14 * gUiScale));
    struct Choice { const char* head; const char* body; int id; };
    Choice ch[4] = {{"Open music", "MP3, FLAC, WAV, OGG, M4A - or drop files / folders on the window", 0},
                    {"System audio", "Visualize whatever your computer is playing", 1},
                    {"Microphone / input", "React to the room, a line-in, or a loopback device", 2},
                    {"Demo track", "Built-in generated loop", 3}};
    for (int i = 0; i < 4; i++) {
      if (i % 2) ImGui::SameLine(0, gap);
      ImGui::PushID(i);
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, .55f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10 * gUiScale);
      bool clicked = ImGui::Button("##choice", ImVec2(bw, bh));
      ImGui::PopStyleVar(); ImGui::PopStyleColor();
      ImVec2 a = ImGui::GetItemRectMin();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImFont* f = ImGui::GetFont();
      dl->AddText(f, f->FontSize * 1.12f, ImVec2(a.x + 16 * gUiScale, a.y + 13 * gUiScale), IM_COL32(255, 255, 255, 255), ch[i].head);
      dl->AddText(f, f->FontSize * .86f, ImVec2(a.x + 16 * gUiScale, a.y + 38 * gUiScale), IM_COL32(255, 255, 255, 165), ch[i].body, nullptr, bw - 30 * gUiScale);
      if (clicked) {
        if (ch[i].id == 0) openFilesDialog();
        else if (ch[i].id == 1) startSystem();
        else if (ch[i].id == 2) ImGui::OpenPopup("##welcomeinput");
        else startDemo();
      }
      if (ImGui::BeginPopup("##welcomeinput")) { inputPopupItems(); ImGui::EndPopup(); }
      ImGui::PopID();
    }
    ImGui::End();
  }

  // ---- bottom bar
  if (uiAlpha > .01f) {
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, uiAlpha);
    float barH = ImGui::GetFrameHeight() + pad * 2.2f;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    float gh = barH + 30 * gUiScale;
    bg->AddRectFilledMultiColor(ImVec2(0, ds.y - gh), ds, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, (int)(200 * uiAlpha)), IM_COL32(0, 0, 0, (int)(200 * uiAlpha)));
    ImGui::SetNextWindowPos(ImVec2(0, ds.y - barH)); ImGui::SetNextWindowSize(ImVec2(ds.x, barH)); ImGui::SetNextWindowBgAlpha(0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad * 1.1f));
    ImGui::Begin("##bar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    float sp = 6 * gUiScale, gp = 18 * gUiScale;
    if (sourceButton("Open", src == Source::Files)) openFilesDialog();
    ImGui::SameLine(0, sp); if (sourceButton("System", src == Source::System)) startSystem();
    ImGui::SameLine(0, sp); if (sourceButton("Input", src == Source::Input)) ImGui::OpenPopup("##inputs");
    if (ImGui::BeginPopup("##inputs")) { inputPopupItems(); ImGui::EndPopup(); }
    ImGui::SameLine(0, sp); if (sourceButton("Demo", src == Source::Demo)) startDemo();

    ImGui::SameLine(0, gp); if (iconButton("##prevT", IC_PREV)) playTrack(gTrack - 1);
    bool playing = (src == Source::Files || src == Source::Demo) && !audio::paused();
    ImGui::SameLine(0, sp); if (iconButton("##play", playing ? IC_PAUSE : IC_PLAY)) playPause();
    ImGui::SameLine(0, sp); if (iconButton("##nextT", IC_NEXT)) playTrack(gTrack + 1);
    // middle area: title | seek | time | volume, squeezed to fit between the left and right groups
    float fh = ImGui::GetFrameHeight();
    float lockW = ImGui::CalcTextSize("Locked").x + ImGui::GetStyle().FramePadding.x * 2;
    float sceneW = 130 * gUiScale, rightW = fh * 1.15f * 4 + lockW + sceneW + sp * 5;
    ImGui::SameLine(0, sp * 2);
    float x0 = ImGui::GetCursorPosX();
    float mid = (ds.x - pad - rightW - gp) - x0;
    if (mid < 200 * gUiScale) { rightW -= sceneW; mid += sceneW; sceneW = 0; }   // compact: drop the scene name
    float timeW = ImGui::CalcTextSize("00:00").x, volW = 80 * gUiScale;
    float titleW = std::min(ImGui::CalcTextSize(gTitle.c_str()).x, std::max(50 * gUiScale, mid * .3f));
    float seekW = mid - titleW - timeW - volW - sp * 6;
    if (seekW < 60 * gUiScale) { volW = 0; seekW = mid - titleW - timeW - sp * 4; }
    if (seekW < 40 * gUiScale) { titleW = std::max(0.f, titleW - (40 * gUiScale - seekW)); seekW = 40 * gUiScale; }
    if (seekW + titleW + timeW + sp * 4 > mid) { titleW = 0; seekW = std::max(20.f, mid - timeW - sp * 3); }
    ImVec2 tp = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(titleW, fh));
    ImGui::GetWindowDrawList()->PushClipRect(tp, ImVec2(tp.x + titleW, tp.y + fh), true);
    ImGui::GetWindowDrawList()->AddText(ImVec2(tp.x, tp.y + (fh - ImGui::GetFontSize()) * .5f), IM_COL32(240, 240, 240, 230), gTitle.c_str());
    ImGui::GetWindowDrawList()->PopClipRect();
    ImGui::SameLine(0, sp * 2);
    double dur = audio::duration(), pos = audio::position();
    bool seekable = src == Source::Files && dur > 0;
    ImGui::SetNextItemWidth(seekW);
    float frac = seekable ? (float)(pos / dur) : src == Source::Demo && dur > 0 ? (float)(std::fmod(pos, dur) / dur) : 0.f;
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 10 * gUiScale);
    if (ImGui::SliderFloat("##seek", &frac, 0, 1, "", ImGuiSliderFlags_NoInput) && (seekable || src == Source::Demo)) audio::seek(frac * dur);
    ImGui::SameLine(0, sp); ImGui::TextColored(ImVec4(1, 1, 1, .7f), "%s", fmtTime(src == Source::Demo ? std::fmod(pos, std::max(1.0, dur)) : pos).c_str());
    if (volW > 0) { ImGui::SameLine(0, sp); ImGui::SetNextItemWidth(volW); if (ImGui::SliderFloat("##vol", &gVolume, 0, 1, "")) audio::setVolume(gVolume); }
    ImGui::PopStyleVar();

    ImGui::SameLine(ds.x - pad - rightW);
    if (iconButton("##prevS", IC_LEFT)) setScene(gScene - 1);
    ImGui::SameLine(0, sp);
    ImVec2 sp0 = ImGui::GetCursorScreenPos(); if (sceneW > 0) ImGui::Dummy(ImVec2(sceneW, fh));
    const char* sn = SCENES[gScene].name; float snw = ImGui::CalcTextSize(sn).x;
    if (sceneW > 0) { ImGui::GetWindowDrawList()->AddText(ImVec2(sp0.x + (sceneW - snw) * .5f, sp0.y + (fh - ImGui::GetFontSize()) * .5f), IM_COL32(240, 240, 240, 230), sn); ImGui::SameLine(0, sp); } if (iconButton("##nextS", IC_RIGHT)) setScene(gScene + 1);
    ImGui::SameLine(0, sp); if (sourceButton(gAuto ? "Auto##lock" : "Locked##lock", !gAuto)) setAuto(!gAuto);
    ImGui::SameLine(0, sp); if (iconButton("##full", IC_FULL)) toggleFullscreen();
    ImGui::SameLine(0, sp); if (ImGui::Button("?", ImVec2(fh * 1.15f, fh))) gShowKeys = !gShowKeys;
    ImGui::End();
    ImGui::PopStyleVar();
  }

  // ---- keys help
  if (gShowKeys) {
    ImGui::SetNextWindowPos(ImVec2(ds.x - pad, ds.y - 74 * gUiScale), ImGuiCond_Always, ImVec2(1, 1));
    ImGui::SetNextWindowBgAlpha(.85f);
    ImGui::Begin("Keys", &gShowKeys, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
    const char* rows[][2] = {{"Space", "Play / pause"}, {"Left / Right", "Previous / next scene"}, {"1 - 0", "Jump to scene"}, {"R", "Random scene"},
                             {"L", "Lock scene / auto-cycle"}, {"N / P", "Next / previous track"}, {"O", "Open music files"}, {"F / double-click", "Fullscreen"},
                             {"Q", "Render quality"}, {"H", "Hide / show controls"}, {"Esc", "Leave fullscreen"}};
    if (ImGui::BeginTable("k", 2)) {
      for (auto& r : rows) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(1, 1, 1, .6f), "%s", r[0]); ImGui::TableNextColumn(); ImGui::TextUnformatted(r[1]); }
      ImGui::EndTable();
    }
    ImGui::TextColored(ImVec4(1, 1, 1, .45f), "Quality: %s", QUALITY_NAMES[gQuality]);
    ImGui::End();
  }
}

static void handleKeys() {
  ImGuiIO& io = ImGui::GetIO();
  if (io.WantTextInput) return;
  auto P = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
  if (P(ImGuiKey_Space)) playPause();
  if (P(ImGuiKey_RightArrow)) setScene(gScene + 1);
  if (P(ImGuiKey_LeftArrow)) setScene(gScene - 1);
  for (int i = 0; i < 10; i++) if (P((ImGuiKey)(ImGuiKey_0 + i))) setScene(i == 0 ? 9 : i - 1);
  if (P(ImGuiKey_R)) { static std::mt19937 rng{std::random_device{}()}; setScene(gScene + 1 + (int)(rng() % (SCENE_COUNT - 1))); }
  if (P(ImGuiKey_L)) setAuto(!gAuto);
  if (P(ImGuiKey_N)) playTrack(gTrack + 1);
  if (P(ImGuiKey_P)) playTrack(gTrack - 1);
  if (P(ImGuiKey_O)) openFilesDialog();
  if (P(ImGuiKey_F) || P(ImGuiKey_F11)) toggleFullscreen();
  if (P(ImGuiKey_Escape)) { if (gFullscreen) toggleFullscreen(); gShowKeys = false; }
  if (P(ImGuiKey_Q)) { gQuality = (gQuality + 1) % 3; toast(std::string("Quality: ") + QUALITY_NAMES[gQuality]); }
  if (P(ImGuiKey_H)) gHideUI = !gHideUI;
  if (P(ImGuiKey_Slash)) gShowKeys = !gShowKeys;
}

static void setupStyle() {
  ImGuiStyle& s = ImGui::GetStyle();
  ImGui::StyleColorsDark(&s);
  s.WindowRounding = 10; s.FrameRounding = 6; s.PopupRounding = 8; s.GrabRounding = 6; s.ChildRounding = 8;
  s.FramePadding = ImVec2(10, 6); s.ItemSpacing = ImVec2(8, 6); s.WindowBorderSize = 0; s.PopupBorderSize = 1; s.FrameBorderSize = 1;
  ImVec4* c = s.Colors;
  c[ImGuiCol_Text] = ImVec4(.93f, .93f, .93f, 1);
  c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, .82f);
  c[ImGuiCol_PopupBg] = ImVec4(.05f, .05f, .07f, .95f);
  c[ImGuiCol_Border] = ImVec4(1, 1, 1, .14f);
  c[ImGuiCol_Button] = ImVec4(1, 1, 1, .08f);
  c[ImGuiCol_ButtonHovered] = ImVec4(1, 1, 1, .2f);
  c[ImGuiCol_ButtonActive] = ImVec4(1, 1, 1, .3f);
  c[ImGuiCol_FrameBg] = ImVec4(1, 1, 1, .12f);
  c[ImGuiCol_FrameBgHovered] = ImVec4(1, 1, 1, .18f);
  c[ImGuiCol_FrameBgActive] = ImVec4(1, 1, 1, .24f);
  c[ImGuiCol_SliderGrab] = ImVec4(1, 1, 1, .95f);
  c[ImGuiCol_SliderGrabActive] = ImVec4(1, 1, 1, 1);
  c[ImGuiCol_Header] = ImVec4(1, 1, 1, .1f);
  c[ImGuiCol_HeaderHovered] = ImVec4(1, 1, 1, .2f);
  c[ImGuiCol_HeaderActive] = ImVec4(1, 1, 1, .3f);
  c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = ImVec4(0, 0, 0, .9f);
  c[ImGuiCol_Separator] = ImVec4(1, 1, 1, .14f);
  s.ScaleAllSizes(gUiScale);
}

// ============================================================ screenshots (internal)
static void writeShot(const std::string& file, int w, int h) {
  std::vector<unsigned char> px(w * h * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
  stbi_flip_vertically_on_write(1);
  stbi_write_png(file.c_str(), w, h, 3, px.data(), w * 3);
  printf("wrote %s\n", file.c_str()); fflush(stdout);
}

// ============================================================ main
int main(int, char**) {
  const char* shotDir = getenv("PRISMATICA_SHOTS");
  if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#ifdef __APPLE__
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
  glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
#endif
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
  int ww = 1280, wh = 720;
  if (shotDir) { if (getenv("PRISMATICA_W")) ww = atoi(getenv("PRISMATICA_W")); if (getenv("PRISMATICA_H")) wh = atoi(getenv("PRISMATICA_H")); glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE); }
  gWin = glfwCreateWindow(ww, wh, "Prismatica", nullptr, nullptr);
  if (!gWin) {
    const char* msg = nullptr; glfwGetError(&msg);
    fprintf(stderr, "Could not create an OpenGL 3.3 window: %s\n", msg ? msg : "?");
    pfd::message("Prismatica", "This computer's graphics driver doesn't support OpenGL 3.3, which Prismatica needs.", pfd::choice::ok, pfd::icon::error);
    return 1;
  }
  glfwSetWindowSizeLimits(gWin, 760, 428, GLFW_DONT_CARE, GLFW_DONT_CARE);
  glfwMakeContextCurrent(gWin);
  glfwSwapInterval(shotDir ? 0 : 1);
  if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) { fprintf(stderr, "gladLoadGL failed\n"); return 1; }
  glfwSetDropCallback(gWin, [](GLFWwindow*, int n, const char** paths) { for (int i = 0; i < n; i++) gDropped.push_back(paths[i]); });

  // UI scale: Windows/Linux report DPI as content scale; macOS uses points + retina framebuffer instead.
  float csx = 1, csy = 1; glfwGetWindowContentScale(gWin, &csx, &csy);
  int fbw, fbh, winw, winh; glfwGetFramebufferSize(gWin, &fbw, &fbh); glfwGetWindowSize(gWin, &winw, &winh);
  float fbScale = winw > 0 ? (float)fbw / winw : 1;
  gUiScale = fbScale > 1.01f ? 1.f : std::max(1.f, csx);
  if (shotDir) gUiScale = 1;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  ImFontConfig fc; fc.OversampleH = 2; fc.FontDataOwnedByAtlas = true;
  static const ImWchar ranges[] = {0x0020, 0x024F, 0x2010, 0x2027, 0};
  float raster = std::max(1.f, fbScale);
  io.Fonts->AddFontFromMemoryCompressedBase85TTF(RobotoMedium_compressed_data_base85, 15.f * gUiScale * raster, &fc, ranges);
  gBigFont = io.Fonts->AddFontFromMemoryCompressedBase85TTF(RobotoMedium_compressed_data_base85, 40.f * gUiScale * raster, &fc, ranges);
  io.FontGlobalScale = 1.f / raster;
  setupStyle();
  ImGui_ImplGlfw_InitForOpenGL(gWin, true);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  gF.build(VS_FULLSCREEN, FS_FEEDBACK);
  gP.build(VS_PARTICLES, FS_PARTICLES);
  gC.build(VS_FULLSCREEN, FS_COMPOSITE);
  glGenVertexArrays(1, &gVao);
  glGenTextures(1, &gAudioTex); glBindTexture(GL_TEXTURE_2D, gAudioTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, audio::TEXW, 2, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  std::string aerr;
  if (!shotDir) { audio::init(&aerr); audio::setVolume(gVolume); if (!aerr.empty()) toast(aerr); }
  setScene(0, true);
  if (shotDir) gToastT = -100;

  // ---------------- screenshot mode: PRISMATICA_PLAN="scene,seconds,ui;..." (ui: 0 none, 1 controls, 2 welcome)
  if (shotDir) {
    fs::create_directories(shotDir);
    const char* plan = getenv("PRISMATICA_PLAN"); std::string ps = plan ? plan : "0,4,0";
    double simT = 0;
    size_t start = 0;
    while (start < ps.size()) {
      size_t end = ps.find(';', start); if (end == std::string::npos) end = ps.size();
      int scene = 0, ui = 0; float secs = 3;
      sscanf(ps.substr(start, end - start).c_str(), "%d,%f,%d", &scene, &secs, &ui);
      start = end + 1;
      setScene(scene, true); gToastT = -100;
      gWelcome = ui == 2;
      gTitle = ui == 1 ? "Demo track" : "No audio";
      int fw, fh; glfwGetFramebufferSize(gWin, &fw, &fh);
      int frames = std::max(1, (int)(secs * 60));
      for (int i = 0; i < frames; i++) {
        simT += 1 / 60.0; gSceneAge = 0;
        const Levels& L = audio::updateOffline(1 / 60.f, gTime, simT);
        renderFrame(1 / 60.f, fw, fh, L);
        if (i >= frames - 3) {   // a few UI frames so auto-sized windows settle
          ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
          if (ui) drawUI(1.f);
          ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        if (i == frames - 1) {
          char name[256]; std::string sn = SCENES[scene].name;
          for (auto& ch : sn) ch = ch == ' ' ? '-' : (char)tolower((unsigned char)ch);
          snprintf(name, sizeof name, "%s/%02d-%s%s.png", shotDir, scene + 1, sn.c_str(), ui == 1 ? "-controls" : ui == 2 ? "-welcome" : "");
          writeShot(name, fw, fh);
        }
        glfwSwapBuffers(gWin); glfwPollEvents();
      }
    }
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
    glfwTerminate();
    return 0;
  }

  // ---------------- main loop
  double last = glfwGetTime(), perfT = 0; int perfFrames = 0; double perfAcc = 0; bool autoDropped = false;
  double lastClick = -1; double lmx = -1, lmy = -1;
  gLastActivity = last;
  while (!glfwWindowShouldClose(gWin)) {
    glfwPollEvents();
    double now = glfwGetTime();
    float dt = (float)std::clamp(now - last, 0.0, 0.1); last = now;

    if (!gDropped.empty()) { auto d = gDropped; gDropped.clear(); addFiles(d); }
    if (audio::takeTrackEnded()) playTrack(gTrack + 1);
    std::string st = audio::systemStatus();
    static std::string lastSt; if (!st.empty() && st != lastSt) { toast(st); if (audio::source() == Source::System) gTitle = "System audio (no permission)"; } lastSt = st;

    int fbW, fbH; glfwGetFramebufferSize(gWin, &fbW, &fbH);
    if (fbW <= 0 || fbH <= 0) { glfwWaitEventsTimeout(.1); continue; }
    const Levels& L = audio::update(dt, gTime);
    renderFrame(dt, fbW, fbH, L);

    // adaptive quality: if the GPU can't keep ~45 fps at 1080p, drop to 720p once
    perfAcc += dt; perfFrames++; perfT += dt;
    if (perfT > 3) { if (!autoDropped && gQuality > 0 && perfAcc / perfFrames > 1 / 42.0) { gQuality--; autoDropped = true; toast(std::string("Quality: ") + QUALITY_NAMES[gQuality] + " (auto)"); } perfT = perfAcc = 0; perfFrames = 0; }

    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
    double mx, my; glfwGetCursorPos(gWin, &mx, &my);
    if (mx != lmx || my != lmy || ImGui::IsMouseDown(0) || ImGui::IsAnyItemActive()) { gLastActivity = now; lmx = mx; lmy = my; }
    bool overUI = ImGui::GetIO().WantCaptureMouse;
    if (overUI) gLastActivity = now;
    bool idle = now - gLastActivity > 3 && !gWelcome && !gShowKeys;
    glfwSetInputMode(gWin, GLFW_CURSOR, idle ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_NORMAL);
    static float uiAlpha = 1; uiAlpha += ((idle || gHideUI) ? -dt * 2.5f : dt * 4.f); uiAlpha = std::clamp(uiAlpha, 0.f, 1.f);
    handleKeys();
    if (ImGui::IsMouseClicked(0) && !overUI) { if (now - lastClick < .35) { toggleFullscreen(); lastClick = -1; } else lastClick = now; }
    drawUI(uiAlpha);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(gWin);
  }
  audio::shutdown();
  ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
  glfwDestroyWindow(gWin); glfwTerminate();
  return 0;
}

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return main(__argc, __argv); }
#endif
