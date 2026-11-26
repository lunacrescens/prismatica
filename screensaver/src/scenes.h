#pragma once
// A scene is a set of weights for every layer; scenes cross-fade by interpolating all of them.
#include <cstddef>

#define SCENE_PARAMS(X) \
  X(zoom, .02f) X(rot, 0) X(swirl, 0) X(warp, 0) X(warpF, 1) X(ripple, 0) X(decay, .95f) X(hue, .01f) \
  X(wave, 0) X(line, 0) X(ring, 0) X(orbs, 0) X(plasma, 0) X(tunnel, 0) X(flower, 0) X(flowerK, 5) X(bars, 0) \
  X(shock, 0) X(web, 0) X(pStar, 0) X(pPhyl, 0) X(kal, 0) X(kalN, 6) X(chroma, .3f) X(palMix, 0) X(palShift, 0) \
  X(exposure, 1.1f)

struct SceneParams {
#define X(name, def) float name = def;
  SCENE_PARAMS(X)
#undef X
};

static const char* const PARAM_NAMES[] = {
#define X(name, def) #name,
  SCENE_PARAMS(X)
#undef X
};
static const int PARAM_COUNT = sizeof(PARAM_NAMES) / sizeof(PARAM_NAMES[0]);
inline float* paramPtr(SceneParams& p, int i) { return reinterpret_cast<float*>(&p) + i; }

struct Scene { const char* name; SceneParams p; };

inline Scene makeScene(const char* name, void (*fill)(SceneParams&)) { Scene s{name, {}}; fill(s.p); return s; }

static Scene SCENES[] = {
  makeScene("Event Horizon", [](SceneParams& p) { p.zoom=.03f; p.rot=.004f; p.swirl=.03f; p.warp=.4f; p.decay=.962f; p.hue=.02f; p.wave=1; p.shock=1; p.pStar=1; p.orbs=.6f; p.chroma=.6f; }),
  makeScene("Mandala Engine", [](SceneParams& p) { p.zoom=.014f; p.rot=.01f; p.warp=.7f; p.warpF=2; p.decay=.95f; p.ring=1; p.flower=1; p.flowerK=5; p.orbs=1; p.kal=1; p.kalN=8; p.palMix=.3f; p.palShift=.15f; p.pPhyl=.5f; }),
  makeScene("Liquid Aurora", [](SceneParams& p) { p.zoom=.006f; p.rot=-.002f; p.warp=2.2f; p.warpF=1.6f; p.decay=.985f; p.hue=.012f; p.plasma=1; p.line=1; p.orbs=.5f; p.palMix=.45f; p.palShift=.4f; p.chroma=.2f; }),
  makeScene("Neon Tunnel", [](SceneParams& p) { p.zoom=.05f; p.swirl=.06f; p.decay=.93f; p.tunnel=1; p.wave=.7f; p.pStar=1; p.shock=1; p.chroma=1; p.palShift=.55f; p.hue=.03f; }),
  makeScene("Spectrum Bloom", [](SceneParams& p) { p.zoom=.022f; p.rot=.003f; p.ripple=1.2f; p.decay=.955f; p.ring=1; p.bars=1; p.pPhyl=1; p.shock=.6f; p.palShift=.7f; p.hue=.015f; }),
  makeScene("Kaleido Fever", [](SceneParams& p) { p.zoom=.02f; p.rot=-.012f; p.warp=1.2f; p.warpF=2.4f; p.decay=.96f; p.plasma=.7f; p.line=1; p.orbs=1; p.web=.6f; p.kal=1; p.kalN=12; p.palMix=.75f; p.palShift=.25f; p.chroma=.5f; }),
  makeScene("Phyllotaxis", [](SceneParams& p) { p.zoom=-.012f; p.rot=.02f; p.swirl=-.03f; p.decay=.955f; p.pPhyl=1; p.flower=.8f; p.flowerK=7; p.shock=1; p.bars=.5f; p.palShift=.85f; p.hue=.025f; }),
  makeScene("Ripple Tank", [](SceneParams& p) { p.zoom=.008f; p.ripple=2.4f; p.warp=.5f; p.decay=.975f; p.hue=.03f; p.wave=1; p.orbs=1; p.web=.5f; p.palShift=.05f; p.palMix=.25f; p.chroma=.4f; }),
  makeScene("Hyperflower", [](SceneParams& p) { p.zoom=.03f; p.swirl=-.05f; p.warp=.6f; p.decay=.95f; p.flower=1; p.flowerK=3; p.tunnel=.45f; p.ring=.6f; p.pStar=.7f; p.kal=1; p.kalN=6; p.palShift=.33f; p.palMix=.2f; }),
  makeScene("Meltdown", [](SceneParams& p) { p.zoom=.016f; p.rot=.008f; p.swirl=.04f; p.warp=3; p.warpF=2.2f; p.ripple=1; p.decay=.972f; p.hue=.04f; p.wave=.8f; p.line=.8f; p.ring=.6f; p.orbs=1; p.plasma=.6f; p.web=.7f; p.flower=.6f; p.flowerK=4; p.shock=1; p.pStar=.8f; p.pPhyl=.6f; p.kal=1; p.kalN=4; p.palMix=1; p.palShift=.6f; p.chroma=1; p.exposure=.95f; }),
};
static const int SCENE_COUNT = sizeof(SCENES) / sizeof(SCENES[0]);
