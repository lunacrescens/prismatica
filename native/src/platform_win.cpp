#include "platform.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

std::wstring utf8ToWide(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
  return w;
}

// Media Foundation is loaded on demand so the app still starts on Windows "N" editions without it.
typedef HRESULT(WINAPI* PFN_MFStartup)(ULONG, DWORD);
typedef HRESULT(WINAPI* PFN_MFShutdown)();
typedef HRESULT(WINAPI* PFN_MFCreateMediaType)(IMFMediaType**);
typedef HRESULT(WINAPI* PFN_MFCreateSourceReaderFromURL)(LPCWSTR, IMFAttributes*, IMFSourceReader**);
static PFN_MFStartup pMFStartup; static PFN_MFShutdown pMFShutdown;
static PFN_MFCreateMediaType pMFCreateMediaType; static PFN_MFCreateSourceReaderFromURL pMFCreateSourceReaderFromURL;
static bool loadMF() {
  static int state = 0;
  if (state) return state > 0;
  HMODULE plat = LoadLibraryW(L"mfplat.dll"), rw = LoadLibraryW(L"mfreadwrite.dll");
  if (plat && rw) {
    pMFStartup = (PFN_MFStartup)(void*)GetProcAddress(plat, "MFStartup");
    pMFShutdown = (PFN_MFShutdown)(void*)GetProcAddress(plat, "MFShutdown");
    pMFCreateMediaType = (PFN_MFCreateMediaType)(void*)GetProcAddress(plat, "MFCreateMediaType");
    pMFCreateSourceReaderFromURL = (PFN_MFCreateSourceReaderFromURL)(void*)GetProcAddress(rw, "MFCreateSourceReaderFromURL");
  }
  state = (pMFStartup && pMFShutdown && pMFCreateMediaType && pMFCreateSourceReaderFromURL) ? 1 : -1;
  return state > 0;
}

template <class T> static void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

// Media Foundation decode (m4a / aac / wma / anything Windows has a codec for).
bool platformDecodeFile(const std::string& path, std::vector<float>& pcm, int& channels, int& sampleRate, std::string* err) {
  if (!loadMF()) { if (err) *err = "This format needs Windows Media Foundation (Media Feature Pack)"; return false; }
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool coInit = SUCCEEDED(hr);
  if (FAILED(pMFStartup(MF_VERSION, MFSTARTUP_LITE))) { if (err) *err = "Media Foundation unavailable"; if (coInit) CoUninitialize(); return false; }
  IMFSourceReader* reader = nullptr; IMFMediaType* want = nullptr; IMFMediaType* got = nullptr;
  bool ok = false;
  do {
    if (FAILED(pMFCreateSourceReaderFromURL(utf8ToWide(path).c_str(), nullptr, &reader))) { if (err) *err = "Unsupported audio format"; break; }
    reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    pMFCreateMediaType(&want);
    want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    if (FAILED(reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, want))) { if (err) *err = "No audio decoder for this file"; break; }
    if (FAILED(reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &got))) break;
    UINT32 ch = 0, sr = 0;
    got->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
    got->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
    if (!ch || !sr) break;
    channels = (int)ch; sampleRate = (int)sr;
    pcm.clear();
    for (;;) {
      DWORD flags = 0; IMFSample* sample = nullptr;
      if (FAILED(reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample))) break;
      if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { release(sample); break; }
      if (!sample) continue;
      IMFMediaBuffer* buf = nullptr;
      if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf))) {
        BYTE* data = nullptr; DWORD len = 0;
        if (SUCCEEDED(buf->Lock(&data, nullptr, &len))) {
          const float* f = (const float*)data;
          pcm.insert(pcm.end(), f, f + len / sizeof(float));
          buf->Unlock();
        }
        release(buf);
      }
      release(sample);
    }
    ok = !pcm.empty();
    if (!ok && err) *err = "Could not decode this file";
  } while (false);
  release(got); release(want); release(reader);
  pMFShutdown();
  if (coInit) CoUninitialize();
  return ok;
}

// Windows uses WASAPI loopback through miniaudio directly (see audio.cpp).
bool platformSystemCaptureStart(CaptureCallback, void*, std::string* err) { if (err) *err = "unused"; return false; }
void platformSystemCaptureStop() {}
std::string platformSystemCaptureStatus() { return ""; }
