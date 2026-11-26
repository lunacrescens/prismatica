// macOS screen saver (.saver bundle): a ScreenSaverView that hosts an OpenGL 3.2+ core view.
// Class names carry the scene name so two Prismatica savers can be loaded into the same host process.
#import <ScreenSaver/ScreenSaver.h>
#import <Cocoa/Cocoa.h>
#import <OpenGL/gl3.h>
#import <QuartzCore/QuartzCore.h>
#include "core.h"

#ifndef SAVER_CLASS
#define SAVER_CLASS PrismaticaSaverView
#endif

@interface SAVER_CLASS : ScreenSaverView
@end

@implementation SAVER_CLASS {
  NSOpenGLView* _gl;
  CFTimeInterval _last;
  BOOL _preview, _failed;
}

- (instancetype)initWithFrame:(NSRect)frame isPreview:(BOOL)isPreview {
  self = [super initWithFrame:frame isPreview:isPreview];
  if (!self) return nil;
  _preview = isPreview;
  NSOpenGLPixelFormatAttribute attrs[] = {
    NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
    NSOpenGLPFAColorSize, 24, NSOpenGLPFADoubleBuffer, NSOpenGLPFAAccelerated, NSOpenGLPFANoRecovery, 0};
  NSOpenGLPixelFormat* pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
  if (!pf) {   // no accelerated renderer: accept whatever the system offers
    NSOpenGLPixelFormatAttribute soft[] = {NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core, NSOpenGLPFAColorSize, 24, NSOpenGLPFADoubleBuffer, 0};
    pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:soft];
  }
  if (pf) {
    _gl = [[NSOpenGLView alloc] initWithFrame:self.bounds pixelFormat:pf];
    _gl.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
#ifdef SAVER_RETINA
    _gl.wantsBestResolutionOpenGLSurface = YES;     // full Retina pixel resolution
#else
    _gl.wantsBestResolutionOpenGLSurface = NO;      // 1x keeps it light on integrated graphics
#endif
    [self addSubview:_gl];
    GLint one = 1;
    [_gl.openGLContext setValues:&one forParameter:NSOpenGLContextParameterSwapInterval];
  } else {
    _failed = YES;
  }
  self.animationTimeInterval = 1.0 / 60.0;
  // macOS 14+ can leave the saver process running after the screen saver is dismissed; quit when told it stopped.
  if (!isPreview) {
    NSOperatingSystemVersion v14 = {14, 0, 0};
    if ([[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:v14]) {
      [[NSDistributedNotificationCenter defaultCenter] addObserver:self selector:@selector(saverWillStop:) name:@"com.apple.screensaver.willstop" object:nil];
    }
  }
  return self;
}

- (void)saverWillStop:(NSNotification*)note {
  if (!_preview) exit(0);
}

- (void)dealloc {
  [[NSDistributedNotificationCenter defaultCenter] removeObserver:self];
}

- (void)startAnimation {
  [super startAnimation];
  _last = 0;
}

- (void)drawRect:(NSRect)rect {
  [[NSColor blackColor] setFill];
  NSRectFill(rect);
}

- (void)animateOneFrame {
  if (_failed || !_gl) return;
  NSOpenGLContext* ctx = _gl.openGLContext;
  if (!ctx) return;
  [ctx makeCurrentContext];
  CFTimeInterval now = CACurrentMediaTime();
  float dt = _last > 0 ? (float)(now - _last) : 1.f / 60;
  _last = now;
  NSSize px = [_gl convertSizeToBacking:_gl.bounds.size];
  GLint fbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);     // the view's own target (non-zero when layer-backed)
  saver::render(dt, (int)px.width, (int)px.height, (GLuint)fbo);
  [ctx flushBuffer];
}

- (BOOL)hasConfigureSheet { return NO; }
- (NSWindow*)configureSheet { return nil; }
@end
