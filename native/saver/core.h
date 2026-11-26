// Prismatica screensaver core: one scene, no audio, slow motion. Needs a current OpenGL 3.2+ core context
// and the GL function names in scope (glad on Windows/Linux, <OpenGL/gl3.h> on macOS).
#pragma once
#include "../src/scenes.h"
#include "../src/shaders.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

#ifndef SAVER_SIM_CAP
#define SAVER_SIM_CAP 1080       // max internal render height; the Retina Mac build raises it
#endif
#ifndef SAVER_RENDER_SCALE
#define SAVER_RENDER_SCALE 1.0f  // render at this fraction of the window's pixels, then upscale (Retina 1.5x build: 0.75)
#endif
#ifndef SAVER_SCENE
#define SAVER_SCENE 2            // 2 = Liquid Aurora, 7 = Ripple Tank
#endif

namespace saver {
namespace {
const float SPEED = 0.38f;       // everything runs at this fraction of the visualizer's pace
const int TEXW = 512;

struct Prog { GLuint id = 0; GLint params[64]; GLint uRes, uTime, uAT, uBass, uMid, uTreb, uBeat, uBeatAge, uDt60, palShift, uAudio, uPrev, uTex, uLodBias; };
struct Target { GLuint tex = 0, fbo = 0; };
Prog gF, gC;
GLuint gVao = 0, gAudioTex = 0;
Target gT[2], gOut;          // gOut: scaled-down final image when SAVER_RENDER_SCALE < 1
int gOutW = 0, gOutH = 0;
int gSimW = 0, gSimH = 0, gFlip = 0;
bool gHalfFloat = true, gReady = false;
float gTime = 0, gAT = 0;
SceneParams gP;
unsigned char gTexData[TEXW * 2];

GLuint compile(GLenum type, const char* src) {
  GLuint s = glCreateShader(type); glShaderSource(s, 1, &src, nullptr); glCompileShader(s);
  GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[2048]; glGetShaderInfoLog(s, sizeof log, nullptr, log); fprintf(stderr, "shader error:\n%s\n", log); }
  return s;
}
void build(Prog& p, const char* vs, const char* fs) {
  p.id = glCreateProgram();
  GLuint a = compile(GL_VERTEX_SHADER, vs), b = compile(GL_FRAGMENT_SHADER, fs);
  glAttachShader(p.id, a); glAttachShader(p.id, b); glLinkProgram(p.id); glDeleteShader(a); glDeleteShader(b);
  for (int i = 0; i < PARAM_COUNT; i++) p.params[i] = glGetUniformLocation(p.id, PARAM_NAMES[i]);
  auto u = [&](const char* n) { return glGetUniformLocation(p.id, n); };
  p.uRes = u("uRes"); p.uTime = u("uTime"); p.uAT = u("uAT"); p.uBass = u("uBass"); p.uMid = u("uMid"); p.uTreb = u("uTreb");
  p.uBeat = u("uBeat"); p.uBeatAge = u("uBeatAge"); p.uDt60 = u("uDt60"); p.palShift = u("palShift"); p.uAudio = u("uAudio"); p.uPrev = u("uPrev"); p.uTex = u("uTex"); p.uLodBias = u("uLodBias");
}
Target makeTarget(int w, int h) {
  Target t;
  glGenTextures(1, &t.tex); glBindTexture(GL_TEXTURE_2D, t.tex);
  if (gHalfFloat) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
  else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
  glGenFramebuffers(1, &t.fbo); glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE && gHalfFloat) {
    glDeleteFramebuffers(1, &t.fbo); glDeleteTextures(1, &t.tex); gHalfFloat = false; return makeTarget(w, h);
  }
  glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); glGenerateMipmap(GL_TEXTURE_2D);
  return t;
}
void ensureTargets(int fbW, int fbH) {
  int w = fbW, h = fbH; const int cap = SAVER_SIM_CAP;
  if (h > cap) { w = (int)std::lround(w * (double)cap / h); h = cap; }
  w = std::max(2, w); h = std::max(2, h);
  if (w == gSimW && h == gSimH) return;
  for (auto& t : gT) if (t.fbo) { glDeleteFramebuffers(1, &t.fbo); glDeleteTextures(1, &t.tex); }
  gSimW = w; gSimH = h; gT[0] = makeTarget(w, h); gT[1] = makeTarget(w, h);
}
void setCommon(const Prog& p, float dt, float w, float h, float bass, float mid, float treb) {
  glUniform2f(p.uRes, w, h); glUniform1f(p.uTime, gTime); glUniform1f(p.uAT, gAT);
  glUniform1f(p.uBass, bass); glUniform1f(p.uMid, mid); glUniform1f(p.uTreb, treb);
  glUniform1f(p.uBeat, 0); glUniform1f(p.uBeatAge, 9); glUniform1f(p.uDt60, std::min(3.f, dt * 60));
  for (int i = 0; i < PARAM_COUNT; i++) if (p.params[i] >= 0) glUniform1f(p.params[i], *paramPtr(gP, i));
  if (p.palShift >= 0) glUniform1f(p.palShift, gP.palShift + gTime * .011f);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gAudioTex);
  if (p.uAudio >= 0) glUniform1i(p.uAudio, 1);
}
} // namespace

inline const char* sceneName() { return SCENES[SAVER_SCENE].name; }

inline void init() {
  if (gReady) return;
  gP = SCENES[SAVER_SCENE].p;
  build(gF, VS_FULLSCREEN, FS_FEEDBACK); build(gC, VS_FULLSCREEN, FS_COMPOSITE);
  glGenVertexArrays(1, &gVao);
  glGenTextures(1, &gAudioTex); glBindTexture(GL_TEXTURE_2D, gAudioTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, TEXW, 2, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gReady = true;
}

// realDt: seconds since the last frame. Draws into `outFbo` (0 = the window) at fbW x fbH pixels.
inline void render(float realDt, int fbW, int fbH, GLuint outFbo = 0) {
  if (!gReady) init();
  if (fbW <= 0 || fbH <= 0) return;
  const int winW = fbW, winH = fbH; const GLuint winFbo = outFbo;
  const bool scaled = SAVER_RENDER_SCALE < .999f;
  if (scaled) {   // draw everything at the lower size into gOut, blit-upscale at the end
    fbW = std::max(2, (int)std::lround(winW * SAVER_RENDER_SCALE)); fbH = std::max(2, (int)std::lround(winH * SAVER_RENDER_SCALE));
    if (fbW != gOutW || fbH != gOutH) {
      if (gOut.fbo) { glDeleteFramebuffers(1, &gOut.fbo); glDeleteTextures(1, &gOut.tex); }
      glGenTextures(1, &gOut.tex); glBindTexture(GL_TEXTURE_2D, gOut.tex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fbW, fbH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glGenFramebuffers(1, &gOut.fbo); glBindFramebuffer(GL_FRAMEBUFFER, gOut.fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gOut.tex, 0);
      gOutW = fbW; gOutH = fbH;
    }
    outFbo = gOut.fbo;
  }
  float dt = std::min(realDt, .1f) * SPEED;
  gTime += dt; gAT += dt * .9f;
  float t = gTime;
  // Stand-ins for the music: slow, smooth swells instead of a live spectrum and waveform.
  float bass = .30f + .16f * std::sin(t * .37f), mid = .38f + .18f * std::sin(t * .23f + 1.f), treb = .30f + .12f * std::sin(t * .31f + 2.f);
  for (int x = 0; x < TEXW; x++) {
    float f = x / float(TEXW);
    float s = .55f + .25f * std::sin(f * 9.f + t * .5f) + .15f * std::sin(f * 23.f - t * .33f);
    float w = .11f * std::sin(f * 12.566f + t * .8f) + .06f * std::sin(f * 31.4f - t * .55f) + .03f * std::sin(f * 62.8f + t * 1.1f);
    gTexData[x] = (unsigned char)(255 * std::clamp(s, 0.f, 1.f));
    gTexData[TEXW + x] = (unsigned char)(255 * std::clamp(.5f + w, 0.f, 1.f));
  }
  ensureTargets(fbW, fbH);
  glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gAudioTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEXW, 2, GL_RED, GL_UNSIGNED_BYTE, gTexData);
  Target& src = gT[gFlip]; Target& dst = gT[1 - gFlip]; gFlip = 1 - gFlip;
  glBindVertexArray(gVao);
  glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo); glViewport(0, 0, gSimW, gSimH);
  glUseProgram(gF.id); setCommon(gF, dt, (float)gSimW, (float)gSimH, bass, mid, treb);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, src.tex); glUniform1i(gF.uPrev, 0);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindFramebuffer(GL_FRAMEBUFFER, outFbo); glViewport(0, 0, fbW, fbH);
  glUseProgram(gC.id); setCommon(gC, dt, (float)fbW, (float)fbH, bass, mid, treb);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, dst.tex); glGenerateMipmap(GL_TEXTURE_2D);
  glUniform1i(gC.uTex, 0);
  if (gC.uLodBias >= 0) glUniform1f(gC.uLodBias, std::max(0.f, std::log2(gSimH / 900.f)));   // tuned look = a 900px-tall render (1x MacBook Pro)
  glDrawArrays(GL_TRIANGLES, 0, 3);
  if (scaled) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, gOut.fbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, winFbo);
    glBlitFramebuffer(0, 0, fbW, fbH, 0, 0, winW, winH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, winFbo);
  }
}
} // namespace saver
