#include "platform.h"
#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreMedia/CoreMedia.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <mutex>

// ---------- file decode via ExtAudioFile (mp3, m4a/aac, alac, aiff, caf, wav...) ----------
bool platformDecodeFile(const std::string& path, std::vector<float>& pcm, int& channels, int& sampleRate, std::string* err) {
  @autoreleasepool {
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    ExtAudioFileRef file = nullptr;
    if (ExtAudioFileOpenURL((__bridge CFURLRef)url, &file) != noErr || !file) { if (err) *err = "Unsupported audio format"; return false; }
    AudioStreamBasicDescription src = {}; UInt32 sz = sizeof(src);
    ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileDataFormat, &sz, &src);
    channels = std::max<int>(1, std::min<int>(2, (int)src.mChannelsPerFrame));
    sampleRate = (int)(src.mSampleRate > 0 ? src.mSampleRate : 44100);
    AudioStreamBasicDescription dst = {};
    dst.mSampleRate = sampleRate; dst.mFormatID = kAudioFormatLinearPCM;
    dst.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    dst.mChannelsPerFrame = channels; dst.mBitsPerChannel = 32; dst.mFramesPerPacket = 1;
    dst.mBytesPerFrame = dst.mBytesPerPacket = 4 * channels;
    if (ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat, sizeof(dst), &dst) != noErr) {
      ExtAudioFileDispose(file); if (err) *err = "Could not decode this file"; return false;
    }
    pcm.clear();
    std::vector<float> chunk(4096 * channels);
    for (;;) {
      AudioBufferList abl; abl.mNumberBuffers = 1;
      abl.mBuffers[0].mNumberChannels = channels; abl.mBuffers[0].mDataByteSize = (UInt32)(chunk.size() * 4); abl.mBuffers[0].mData = chunk.data();
      UInt32 frames = 4096;
      if (ExtAudioFileRead(file, &frames, &abl) != noErr || frames == 0) break;
      pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + frames * channels);
    }
    ExtAudioFileDispose(file);
    if (pcm.empty()) { if (err) *err = "Could not decode this file"; return false; }
    return true;
  }
}

// ---------- system audio via ScreenCaptureKit (macOS 13+) ----------
static std::mutex gStatusLock;
static std::string gStatus;
static void setStatus(const std::string& s) { std::lock_guard<std::mutex> lk(gStatusLock); gStatus = s; }

API_AVAILABLE(macos(13.0))
@interface PrismaticaCapture : NSObject <SCStreamOutput, SCStreamDelegate>
@property(nonatomic, strong) SCStream* stream;
@property(nonatomic) CaptureCallback cb;
@property(nonatomic) void* user;
@property(nonatomic) BOOL stopped;
@end

@implementation PrismaticaCapture
- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sb ofType:(SCStreamOutputType)type {
  if (type != SCStreamOutputTypeAudio || self.stopped || !CMSampleBufferIsValid(sb)) return;
  CMFormatDescriptionRef fd = CMSampleBufferGetFormatDescription(sb);
  const AudioStreamBasicDescription* asbd = fd ? CMAudioFormatDescriptionGetStreamBasicDescription(fd) : nullptr;
  if (!asbd || !(asbd->mFormatFlags & kAudioFormatFlagIsFloat) || asbd->mBitsPerChannel != 32) return;
  struct { AudioBufferList list; AudioBuffer extra[7]; } storage;
  CMBlockBufferRef block = nullptr;
  if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(sb, nullptr, &storage.list, sizeof(storage), nullptr, nullptr,
        kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment, &block) != noErr) return;
  int frames = (int)CMSampleBufferGetNumSamples(sb);
  static thread_local std::vector<float> mono;
  mono.assign(frames, 0.f);
  const AudioBufferList& abl = storage.list;
  bool nonInterleaved = (asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
  int nch = 0;
  if (nonInterleaved) {
    for (UInt32 b = 0; b < abl.mNumberBuffers; b++) {
      const float* d = (const float*)abl.mBuffers[b].mData;
      int n = std::min<int>(frames, abl.mBuffers[b].mDataByteSize / 4);
      for (int i = 0; i < n; i++) mono[i] += d[i];
      nch++;
    }
  } else if (abl.mNumberBuffers > 0) {
    int ch = std::max<int>(1, abl.mBuffers[0].mNumberChannels);
    const float* d = (const float*)abl.mBuffers[0].mData;
    int n = std::min<int>(frames, abl.mBuffers[0].mDataByteSize / 4 / ch);
    for (int i = 0; i < n; i++) for (int c = 0; c < ch; c++) mono[i] += d[i * ch + c];
    nch = ch;
  }
  if (nch > 1) for (float& v : mono) v /= nch;
  if (self.cb && frames > 0) self.cb(mono.data(), frames, (int)asbd->mSampleRate, self.user);
  if (block) CFRelease(block);
}
- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
  if (!self.stopped) setStatus(std::string("System audio capture stopped: ") + error.localizedDescription.UTF8String);
}
@end

static id gCapture = nil;   // PrismaticaCapture*

bool platformSystemCaptureStart(CaptureCallback cb, void* user, std::string* err) {
  platformSystemCaptureStop();
  setStatus("");
  if (@available(macOS 13.0, *)) {
    PrismaticaCapture* cap = [PrismaticaCapture new];
    cap.cb = cb; cap.user = user;
    gCapture = cap;
    [SCShareableContent getShareableContentExcludingDesktopWindows:NO onScreenWindowsOnly:YES completionHandler:^(SCShareableContent* content, NSError* error) {
      if (cap.stopped) return;
      if (error || content.displays.count == 0) {
        setStatus("Allow Prismatica under System Settings > Privacy & Security > Screen & System Audio Recording, then reopen it");
        return;
      }
      SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:content.displays.firstObject excludingWindows:@[]];
      SCStreamConfiguration* cfg = [SCStreamConfiguration new];
      cfg.capturesAudio = YES;
      cfg.excludesCurrentProcessAudio = YES;
      cfg.sampleRate = 48000;
      cfg.channelCount = 2;
      cfg.width = 16; cfg.height = 16;                 // video is required but unused: keep it tiny
      cfg.minimumFrameInterval = CMTimeMake(1, 2);
      cfg.queueDepth = 3;
      SCStream* stream = [[SCStream alloc] initWithFilter:filter configuration:cfg delegate:cap];
      NSError* addErr = nil;
      dispatch_queue_t q = dispatch_queue_create("prismatica.sck.audio", DISPATCH_QUEUE_SERIAL);
      if (![stream addStreamOutput:cap type:SCStreamOutputTypeAudio sampleHandlerQueue:q error:&addErr]) {
        setStatus(std::string("System audio capture failed: ") + addErr.localizedDescription.UTF8String);
        return;
      }
      cap.stream = stream;
      [stream startCaptureWithCompletionHandler:^(NSError* startErr) {
        if (startErr) setStatus("Allow Prismatica under System Settings > Privacy & Security > Screen & System Audio Recording, then reopen it");
      }];
    }];
    return true;
  }
  if (err) *err = "System audio needs macOS 13 or newer - use a loopback device like BlackHole under Input instead";
  return false;
}

void platformSystemCaptureStop() {
  if (@available(macOS 13.0, *)) {
    PrismaticaCapture* cap = (PrismaticaCapture*)gCapture;
    if (!cap) return;
    cap.stopped = YES;
    if (cap.stream) [cap.stream stopCaptureWithCompletionHandler:^(NSError*) {}];
    gCapture = nil;
  }
}

std::string platformSystemCaptureStatus() { std::lock_guard<std::mutex> lk(gStatusLock); return gStatus; }
