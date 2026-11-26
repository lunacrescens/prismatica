#pragma once
#include <string>
#include <vector>

// Decode a file that miniaudio can't (m4a/aac/alac/aiff on macOS via ExtAudioFile, m4a/aac/wma on Windows via Media Foundation).
// Output: interleaved float, native channel count and rate.
bool platformDecodeFile(const std::string& utf8Path, std::vector<float>& pcm, int& channels, int& sampleRate, std::string* err);

// macOS system-audio capture (ScreenCaptureKit, macOS 13+). Callback receives mono float samples.
typedef void (*CaptureCallback)(const float* mono, int frames, int sampleRate, void* user);
bool platformSystemCaptureStart(CaptureCallback cb, void* user, std::string* err);
void platformSystemCaptureStop();
std::string platformSystemCaptureStatus();   // error text once the async start fails, else ""

#ifdef _WIN32
std::wstring utf8ToWide(const std::string& s);
#endif
