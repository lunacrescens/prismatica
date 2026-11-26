#pragma once
#include <string>
#include <vector>

struct Levels { float bass = 0, mid = 0, treb = 0, vol = 0, beat = 0, beatAge = 9, at = 0, silent = 9; };

enum class Source { None, Files, Demo, Input, System };

struct InputDevice { std::string name; bool isDefault; };

namespace audio {
constexpr int TEXW = 512;

bool init(std::string* err);
void shutdown();

// sources
bool playFile(const std::string& path, std::string* err);
void playDemo();
bool startInput(int deviceIndex, std::string* err);     // -1 = default input
bool startSystem(std::string* err);                     // loopback (Windows) / ScreenCaptureKit (macOS 13+)
std::string systemStatus();                              // non-empty = error message to show (async capture start)
void stop();
std::vector<InputDevice> inputDevices();
Source source();

// transport (Files / Demo)
void togglePause();
bool paused();
double position();
double duration();
void seek(double seconds);
void setVolume(float v);
bool takeTrackEnded();

// analysis: call once per frame. Writes texture rows (spectrum, waveform) and returns levels.
const Levels& update(float dt, float time);
// deterministic offline analysis of the demo track at time t (screenshots / tests)
const Levels& updateOffline(float dt, float time, double t);
const unsigned char* texture();   // TEXW*2 bytes, row 0 spectrum, row 1 waveform

// built-in demo loop, stereo interleaved float
std::vector<float> renderDemo(int sampleRate);
}
