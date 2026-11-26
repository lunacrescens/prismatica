#define _USE_MATH_DEFINES
#include "audio.h"
#include "platform.h"
#include "miniaudio.h"
#include <atomic>
#include <mutex>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace audio {
namespace {
constexpr int FFT = 2048;
constexpr int RING = 1 << 15;

ma_context gCtx;
bool gCtxOk = false;
ma_device gPlay;               // always-open playback device (files + demo)
bool gPlayOk = false;
ma_device gCap;                // capture device (mic / input / Windows loopback)
bool gCapOk = false;
int gOutRate = 48000;

std::mutex gLock;              // guards the playback source below
ma_decoder gDec;
bool gDecOk = false;
std::vector<float> gBuf;       // in-memory source (demo or platform-decoded file), stereo interleaved at gOutRate
bool gBufLoop = false;
std::atomic<uint64_t> gFrame{0};
std::atomic<uint64_t> gTotal{0};
std::atomic<bool> gPaused{false}, gEnded{false};
std::atomic<float> gVolume{.8f};
std::atomic<int> gSource{(int)Source::None};
std::vector<float> gDemo;

float gRing[RING];
std::atomic<uint32_t> gW{0};
std::atomic<int> gRingRate{48000};

void pushMono(const float* m, int n, int rate) {
  uint32_t w = gW.load(std::memory_order_relaxed);
  for (int i = 0; i < n; i++) gRing[(w + i) & (RING - 1)] = m[i];
  gRingRate.store(rate, std::memory_order_relaxed);
  gW.store(w + n, std::memory_order_release);
}
void pushStereo(const float* s, int frames, int rate) {
  float tmp[1024];
  while (frames > 0) { int n = std::min(frames, 1024); for (int i = 0; i < n; i++) tmp[i] = .5f * (s[2 * i] + s[2 * i + 1]); pushMono(tmp, n, rate); s += 2 * n; frames -= n; }
}

void playCallback(ma_device*, void* out, const void*, ma_uint32 frames) {
  float* o = (float*)out;
  std::memset(o, 0, sizeof(float) * 2 * frames);
  Source src = (Source)gSource.load();
  if ((src != Source::Files && src != Source::Demo) || gPaused.load()) return;
  std::unique_lock<std::mutex> lk(gLock, std::try_to_lock);
  if (!lk.owns_lock()) return;
  ma_uint64 got = 0;
  if (gDecOk) {
    ma_decoder_read_pcm_frames(&gDec, o, frames, &got);
    if (got < frames) gEnded = true;
  } else if (!gBuf.empty()) {
    uint64_t total = gBuf.size() / 2, f = gFrame.load();
    while (got < frames) {
      if (f >= total) { if (gBufLoop) f = 0; else { gEnded = true; break; } }
      uint64_t n = std::min<uint64_t>(frames - got, total - f);
      std::memcpy(o + 2 * got, gBuf.data() + 2 * f, sizeof(float) * 2 * n);
      got += n; f += n;
    }
    gFrame.store(f);
  }
  if (gDecOk) gFrame += got;
  lk.unlock();
  pushStereo(o, (int)got, gOutRate);
  float v = gVolume.load();
  for (ma_uint32 i = 0; i < 2 * frames; i++) o[i] *= v;
}

void capCallback(ma_device* dev, void*, const void* in, ma_uint32 frames) {
  const float* s = (const float*)in;
  int ch = dev->capture.channels;
  float tmp[1024];
  while (frames > 0) {
    int n = std::min<int>(frames, 1024);
    for (int i = 0; i < n; i++) { float a = 0; for (int c = 0; c < ch; c++) a += s[i * ch + c]; tmp[i] = a / ch; }
    pushMono(tmp, n, dev->sampleRate);
    s += n * ch; frames -= n;
  }
}
void sckCallback(const float* mono, int frames, int rate, void*) {
  while (frames > 0) { int n = std::min(frames, 4096); pushMono(mono, n, rate); mono += n; frames -= n; }
}

void closeSource() {
  std::lock_guard<std::mutex> lk(gLock);
  if (gDecOk) { ma_decoder_uninit(&gDec); gDecOk = false; }
  gBuf.clear(); gBuf.shrink_to_fit(); gFrame = 0; gTotal = 0; gEnded = false;
}
void stopCapture() {
  if (gCapOk) { ma_device_uninit(&gCap); gCapOk = false; }
  platformSystemCaptureStop();
}

// ---------- analysis ----------
unsigned char gFreq[FFT / 2], gWave[FFT], gTex[TEXW * 2];
float gSmooth[FFT / 2], gRe[FFT], gIm[FFT], gWin[FFT];
float gHist[48]; int gHi = 0; float gCooldown = 0;
float gPeak[3] = {.3f, .3f, .3f};
int gRate = 48000;
Levels L;

void analyse(const float* x) {   // x: FFT mono samples, oldest first (mirrors Web Audio's AnalyserNode)
  static bool winInit = false;
  if (!winInit) { for (int i = 0; i < FFT; i++) gWin[i] = .42f - .5f * std::cos(2 * (float)M_PI * i / FFT) + .08f * std::cos(4 * (float)M_PI * i / FFT); winInit = true; }
  for (int i = 0; i < FFT; i++) {
    gWave[i] = (unsigned char)std::clamp(128.f + x[i] * 128.f, 0.f, 255.f);
    gRe[i] = x[i] * gWin[i]; gIm[i] = 0;
  }
  for (int i = 1, j = 0; i < FFT; i++) {
    int bit = FFT >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
    if (i < j) { std::swap(gRe[i], gRe[j]); }
  }
  for (int len = 2; len <= FFT; len <<= 1) {
    float ang = -2 * (float)M_PI / len, wr = std::cos(ang), wi = std::sin(ang);
    for (int i = 0; i < FFT; i += len) {
      float cr = 1, ci = 0;
      for (int k = 0; k < len / 2; k++) {
        int a = i + k, b = a + len / 2;
        float xr = gRe[b] * cr - gIm[b] * ci, xi = gRe[b] * ci + gIm[b] * cr;
        gRe[b] = gRe[a] - xr; gIm[b] = gIm[a] - xi; gRe[a] += xr; gIm[a] += xi;
        float nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
      }
    }
  }
  for (int i = 0; i < FFT / 2; i++) {
    float mag = std::hypot(gRe[i], gIm[i]) / FFT;
    gSmooth[i] = .72f * gSmooth[i] + .28f * mag;
    float db = 20 * std::log10(gSmooth[i] + 1e-12f);
    gFreq[i] = (unsigned char)std::clamp(255.f * (db + 100) / 70, 0.f, 255.f);
  }
}
float bandAvg(float lo, float hi) {
  int b0 = std::max(1, (int)std::floor(lo * FFT / gRate)), b1 = std::min(FFT / 2 - 1, (int)std::ceil(hi * FFT / gRate));
  float s = 0; for (int i = b0; i <= b1; i++) s += gFreq[i];
  return s / ((b1 - b0 + 1) * 255.f);
}
float ease(float cur, float target, float dt, float up, float down) { return cur + (target - cur) * (1 - std::exp(-dt * (target > cur ? up : down))); }

const Levels& finish(float dt, float time) {
  int dev = 0; for (int i = 0; i < FFT; i += 8) dev += std::abs(gWave[i] - 128);
  L.silent = dev < 40 ? L.silent + dt : 0;
  bool idle = L.silent > 1.5f;
  if (idle) {   // nothing playing: keep the picture gently alive
    for (int i = 0; i < FFT / 2; i++) gFreq[i] = (unsigned char)std::clamp(150 - i * .35f + 50 * std::sin(time * 1.3f + i * .05f) + 40 * std::sin(time * .7f - i * .013f), 0.f, 255.f);
    for (int i = 0; i < FFT; i++) gWave[i] = (unsigned char)(128 + 30 * std::sin(i * .012f + time * 2) * std::sin(i * .0031f - time) + 14 * std::sin(i * .05f - time * 3));
  }
  float raw[3] = {bandAvg(30, 140), bandAvg(200, 2000), bandAvg(3000, 12000)};
  float* lv[3] = {&L.bass, &L.mid, &L.treb};
  for (int k = 0; k < 3; k++) {
    gPeak[k] = std::max({gPeak[k] * std::pow(.9995f, dt * 60), raw[k], .12f});
    float v = std::pow(std::min(1.f, raw[k] / gPeak[k]), 2.2f);
    *lv[k] = ease(*lv[k], v, dt, 30, k == 0 ? 7.f : 5.f);
  }
  if (idle) { L.bass = .3f + .25f * std::sin(time * 1.9f); L.mid = .35f + .25f * std::sin(time * .83f + 1); L.treb = .3f + .2f * std::sin(time * 1.31f + 2); }
  L.vol = (L.bass + L.mid + L.treb) / 3;
  float avg = 0; for (float h : gHist) avg += h; avg /= 48;
  gHist[gHi++ % 48] = raw[0]; gCooldown -= dt; L.beatAge += dt;
  if (idle) { if (L.beatAge > 1.2f) { L.beat = .6f; L.beatAge = 0; } }
  else if (raw[0] > avg * 1.12f + .015f && raw[0] > gPeak[0] * .45f && gCooldown <= 0) { L.beat = 1; L.beatAge = 0; gCooldown = .22f; }
  L.beat *= std::exp(-dt * 6);
  L.at += dt * (.35f + 1.6f * L.vol + .8f * L.beat);
  float nyq = gRate / 2.f;
  for (int x = 0; x < TEXW; x++) {
    float f = 30 * std::pow(16000.f / 30, x / float(TEXW - 1)), pos = std::min<float>(FFT / 2 - 2, f / nyq * (FFT / 2));
    int i = (int)pos; float fr = pos - i;
    float v = (gFreq[i] * (1 - fr) + gFreq[i + 1] * fr) / 255;
    gTex[x] = (unsigned char)(255 * std::min(1.f, std::pow(v, 1.6f) * (1 + 1.2f * x / TEXW)));
    gTex[TEXW + x] = gWave[x * 2];
  }
  return L;
}
} // namespace

// ---------- public ----------
bool init(std::string* err) {
  std::memset(gWave, 128, sizeof gWave);
  ma_backend nullBackend[] = {ma_backend_null};
  bool useNull = getenv("PRISMATICA_NULL_AUDIO") != nullptr;   // testing without a sound card
  if (ma_context_init(useNull ? nullBackend : nullptr, useNull ? 1 : 0, nullptr, &gCtx) != MA_SUCCESS) { if (err) *err = "Audio system unavailable"; return false; }
  gCtxOk = true;
  ma_device_config c = ma_device_config_init(ma_device_type_playback);
  c.playback.format = ma_format_f32; c.playback.channels = 2; c.sampleRate = 0;
  c.dataCallback = playCallback; c.performanceProfile = ma_performance_profile_conservative;
  if (ma_device_init(&gCtx, &c, &gPlay) == MA_SUCCESS) {
    gPlayOk = true; gOutRate = gPlay.sampleRate; ma_device_start(&gPlay);
  } else if (err) *err = "No audio output device";
  return true;
}
void shutdown() {
  stopCapture();
  if (gPlayOk) { ma_device_uninit(&gPlay); gPlayOk = false; }
  closeSource();
  if (gCtxOk) { ma_context_uninit(&gCtx); gCtxOk = false; }
}

bool playFile(const std::string& path, std::string* err) {
  stopCapture();
  gSource = (int)Source::None;
  closeSource();
  ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 2, gOutRate);
  ma_result r;
#ifdef _WIN32
  r = ma_decoder_init_file_w(utf8ToWide(path).c_str(), &dc, &gDec);
#else
  r = ma_decoder_init_file(path.c_str(), &dc, &gDec);
#endif
  if (r == MA_SUCCESS) {
    ma_uint64 len = 0; ma_decoder_get_length_in_pcm_frames(&gDec, &len);
    std::lock_guard<std::mutex> lk(gLock);
    gDecOk = true; gTotal = len; gFrame = 0;
  } else {
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::u8path(path), ec)) { if (err) *err = "File not found"; return false; }
    std::vector<float> pcm; int ch = 0, sr = 0;
    if (!platformDecodeFile(path, pcm, ch, sr, err)) return false;
    uint64_t inFrames = pcm.size() / ch;
    uint64_t outFrames = ma_convert_frames(nullptr, 0, ma_format_f32, 2, gOutRate, pcm.data(), inFrames, ma_format_f32, ch, sr);
    std::vector<float> out(outFrames * 2);
    ma_convert_frames(out.data(), outFrames, ma_format_f32, 2, gOutRate, pcm.data(), inFrames, ma_format_f32, ch, sr);
    std::lock_guard<std::mutex> lk(gLock);
    gBuf.swap(out); gBufLoop = false; gTotal = outFrames; gFrame = 0;
  }
  gPaused = false; gEnded = false;
  gSource = (int)Source::Files;
  return true;
}

void playDemo() {
  stopCapture();
  gSource = (int)Source::None;
  closeSource();
  if (gDemo.empty()) gDemo = renderDemo(gOutRate);
  { std::lock_guard<std::mutex> lk(gLock); gBuf = gDemo; gBufLoop = true; gTotal = gBuf.size() / 2; gFrame = 0; }
  gPaused = false;
  gSource = (int)Source::Demo;
}

std::vector<InputDevice> inputDevices() {
  std::vector<InputDevice> out;
  ma_device_info* caps = nullptr; ma_uint32 nCaps = 0;
  if (gCtxOk && ma_context_get_devices(&gCtx, nullptr, nullptr, &caps, &nCaps) == MA_SUCCESS)
    for (ma_uint32 i = 0; i < nCaps; i++) out.push_back({caps[i].name, caps[i].isDefault != 0});
  return out;
}

bool startInput(int index, std::string* err) {
  stopCapture();
  gSource = (int)Source::None;
  closeSource();
  ma_device_config c = ma_device_config_init(ma_device_type_capture);
  c.capture.format = ma_format_f32; c.capture.channels = 0; c.sampleRate = 0; c.dataCallback = capCallback;
  ma_device_info* caps = nullptr; ma_uint32 nCaps = 0;
  if (index >= 0 && ma_context_get_devices(&gCtx, nullptr, nullptr, &caps, &nCaps) == MA_SUCCESS && (ma_uint32)index < nCaps)
    c.capture.pDeviceID = &caps[index].id;
  if (ma_device_init(&gCtx, &c, &gCap) != MA_SUCCESS) { if (err) *err = "Could not open that input device"; return false; }
  gCapOk = true;
  if (ma_device_start(&gCap) != MA_SUCCESS) { stopCapture(); if (err) *err = "Could not start recording (check microphone permission)"; return false; }
  gSource = (int)Source::Input;
  return true;
}

bool startSystem(std::string* err) {
  stopCapture();
  gSource = (int)Source::None;
  closeSource();
#ifdef _WIN32
  ma_device_config c = ma_device_config_init(ma_device_type_loopback);
  c.capture.format = ma_format_f32; c.capture.channels = 2; c.sampleRate = 0; c.dataCallback = capCallback;
  if (ma_device_init(&gCtx, &c, &gCap) != MA_SUCCESS) { if (err) *err = "System audio capture is unavailable"; return false; }
  gCapOk = true;
  if (ma_device_start(&gCap) != MA_SUCCESS) { stopCapture(); if (err) *err = "Could not start system audio capture"; return false; }
#else
  if (!platformSystemCaptureStart(sckCallback, nullptr, err)) return false;
#endif
  gSource = (int)Source::System;
  return true;
}
std::string systemStatus() { return platformSystemCaptureStatus(); }

void stop() { stopCapture(); gSource = (int)Source::None; closeSource(); }
Source source() { return (Source)gSource.load(); }

void togglePause() { gPaused = !gPaused; }
bool paused() { return gPaused; }
double position() { return gFrame.load() / (double)gOutRate; }
double duration() { return gTotal.load() / (double)gOutRate; }
void seek(double s) {
  std::lock_guard<std::mutex> lk(gLock);
  uint64_t f = (uint64_t)std::clamp(s * gOutRate, 0.0, (double)gTotal.load());
  if (gDecOk) ma_decoder_seek_to_pcm_frame(&gDec, f);
  gFrame = f; gEnded = false;
}
void setVolume(float v) { gVolume = v; }
bool takeTrackEnded() { return gEnded.exchange(false) && source() == Source::Files; }

const Levels& update(float dt, float time) {
  float x[FFT];
  uint32_t w = gW.load(std::memory_order_acquire);
  for (int i = 0; i < FFT; i++) x[i] = gRing[(w - FFT + i) & (RING - 1)];
  if (source() == Source::None || (source() != Source::Input && source() != Source::System && gPaused)) std::fill(x, x + FFT, 0.f);
  gRate = gRingRate.load();
  analyse(x);
  return finish(dt, time);
}

const Levels& updateOffline(float dt, float time, double t) {
  if (gDemo.empty()) gDemo = renderDemo(44100);
  int sr = 44100; gRate = sr;
  size_t n = gDemo.size() / 2, s0 = (size_t)(t * sr) % n;
  float x[FFT];
  for (int i = 0; i < FFT; i++) { size_t k = (s0 + i) % n; x[i] = .5f * (gDemo[2 * k] + gDemo[2 * k + 1]); }
  analyse(x);
  return finish(dt, time);
}
const unsigned char* texture() { return gTex; }

// ---------- built-in demo loop (120 BPM, 16 bars, loops seamlessly) ----------
std::vector<float> renderDemo(int sr) {
  const double bpm = 120, beat = 60 / bpm, six = beat / 4; const int bars = 16;
  const size_t N = (size_t)std::llround(bars * 4 * beat * sr);
  std::vector<float> Lc(N), Rc(N), aL(N), aR(N), duck(N, 1.f);
  uint32_t seed = 1234567;
  auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed / 2147483648.0 - 1; };
  auto hz = [](double m) { return 440 * std::pow(2.0, (m - 69) / 12); };
  const double TAU = 2 * M_PI;
  auto saw = [](double ph) { return 2 * (ph - std::floor(ph + .5)); };
  const int chords[4][4] = {{57, 60, 64, 67}, {53, 57, 60, 64}, {48, 52, 55, 59}, {55, 59, 62, 65}};
  for (int bar = 0; bar < bars; bar++) {
    const int* ch = chords[(bar >> 1) % 4];
    double t0 = bar * 4 * beat; bool full = bar >= 4, brk = bar == 11;
    for (int b = 0; b < 4; b++) {
      size_t i0 = (size_t)std::llround((t0 + b * beat) * sr);
      if (!brk && bar >= 2) {   // kick + sidechain duck
        double ph = 0; size_t len = (size_t)(.36 * sr);
        for (size_t i = 0; i < len && i0 + i < N; i++) { double t = (double)i / sr; ph += (42 + 120 * std::exp(-t * 32)) / sr; float v = (float)(std::sin(TAU * ph) * std::exp(-t * 8) * .95); Lc[i0 + i] += v; Rc[i0 + i] += v; }
        for (size_t i = 0; i < beat * sr && i0 + i < N; i++) duck[i0 + i] = (float)(1 - .65 * std::exp(-(double)i / sr * 9));
      }
      if (full && !brk && (b & 1)) {   // snare
        double lp = 0; size_t len = (size_t)(.22 * sr);
        for (size_t i = 0; i < len && i0 + i < N; i++) { double t = (double)i / sr, n = rnd(); lp += .35 * (n - lp); float v = (float)(((n - lp) * .5 + lp * .5) * std::exp(-t * 20) * .5 + std::sin(TAU * 190 * t) * std::exp(-t * 30) * .3); Lc[i0 + i] += v; Rc[i0 + i] += v; }
      }
      for (int s = 0; s < 4; s++) {
        size_t j0 = i0 + (size_t)std::llround(s * six * sr); int step = b * 4 + s;
        if (bar >= 1) {   // hats
          double prev = 0; bool open = s == 2; double vol = (open ? .2 : .08) * (full ? 1 : .6), pan = step % 2 ? .35 : -.35;
          size_t len = (size_t)((open ? .1 : .035) * sr);
          for (size_t i = 0; i < len && j0 + i < N; i++) { double n = rnd(); float v = (float)((n - prev) * .7 * std::exp(-(double)i / sr * (open ? 35 : 110)) * vol); prev = n; Lc[j0 + i] += v * (float)(1 - pan); Rc[j0 + i] += v * (float)(1 + pan); }
        }
        if (bar >= 2 && s != 0) {   // offbeat bass
          double f = hz(ch[0] - 24 + (step % 8 == 7 ? 12 : 0)), cut = 700 + 500 * std::sin(bar), ph = 0, y1 = 0, y2 = 0;
          size_t len = (size_t)(.2 * sr);
          for (size_t i = 0; i < len && j0 + i < N; i++) { double t = (double)i / sr; ph += f / sr; double k = std::min(.9, TAU * (120 + cut * std::exp(-t * 16)) / sr); y1 += k * (saw(ph) - y1); y2 += k * (y1 - y2); float v = (float)(y2 * std::min(1.0, t * 400) * std::exp(-t * 12) * .55); Lc[j0 + i] += v; Rc[j0 + i] += v; }
        }
        {   // arp
          int note = ch[(step * 3 + (bar & 1)) % 4] + 12 * (((step >> 1) + bar) % 3) + 12;
          double f = hz(note), vol = brk ? .15 : .085, cut = 2400 + 1800 * std::sin(bar * .8 + step * .2), pan = .5 * std::sin(step * 1.3), ph = 0, y = 0;
          bool sq = step % 2; size_t len = (size_t)(.24 * sr);
          for (size_t i = 0; i < len && j0 + i < N; i++) { double t = (double)i / sr; ph += f / sr; double fr = ph - std::floor(ph), rw = sq ? (fr < .5 ? 1 : -1) : 4 * std::abs(fr - .5) - 1; double k = std::min(.9, TAU * (400 + cut * std::exp(-t * 9)) / sr); y += k * (rw - y); float v = (float)(y * std::min(1.0, t * 300) * std::exp(-t * 11) * vol); aL[j0 + i] += v * (float)(1 - pan); aR[j0 + i] += v * (float)(1 + pan); }
        }
      }
    }
    // pad: two detuned saws per chord note through a swept two-pole lowpass
    size_t pi0 = (size_t)std::llround(t0 * sr), plen = (size_t)std::llround(4 * beat * sr);
    double top = brk ? 3500 : 1400;
    for (int ni = 0; ni < 4; ni++) for (int di = 0; di < 2; di++) {
      double f = hz(ch[ni]) * std::pow(2.0, (di ? 9 : -9) / 1200.0), pan = (ni - 1.5) * .25 + (di - .5) * .3, ph = ni * .37 + di * .11, y1 = 0, y2 = 0;
      for (size_t i = 0; i < plen && pi0 + i < N; i++) {
        double t = (double)i / sr, x = t / (4 * beat); ph += f / sr;
        double cutHz = x < .5 ? 400 + (top - 400) * x * 2 : top + (500 - top) * (x - .5) * 2;
        double k = std::min(.9, TAU * cutHz / sr); y1 += k * (saw(ph) - y1); y2 += k * (y1 - y2);
        double env = std::min({1.0, t / .3, (4 * beat - t) / .2});
        float v = (float)(y2 * env * .05 * duck[pi0 + i]);
        Lc[pi0 + i] += v * (float)(1 - pan); Rc[pi0 + i] += v * (float)(1 + pan);
      }
    }
  }
  // ping-pong delay on the arp (wrapped, so the loop point is seamless), then soft-clip
  size_t D = (size_t)std::llround(beat * .75 * sr);
  for (int pass = 0; pass < 2; pass++) for (size_t i = 0; i < N; i++) {
    size_t j = (i + D) % N;
    if (pass == 0 && j < i) continue;
    if (pass == 1 && j > i) continue;
    float l = aL[i], r = aR[i]; aL[j] += r * .45f; aR[j] += l * .45f;
  }
  std::vector<float> out(N * 2);
  for (size_t i = 0; i < N; i++) { out[2 * i] = std::tanh((Lc[i] + aL[i]) * 1.15f) * .85f; out[2 * i + 1] = std::tanh((Rc[i] + aR[i]) * 1.15f) * .85f; }
  return out;
}
} // namespace audio
