PRISMATICA (native) - psychedelic music visualizer
==================================================
This is the native C++/OpenGL build: no Chromium inside, a few MB, and it runs on
Macs whose GPU Chromium no longer supports (e.g. OCLP-patched Haswell/Broadwell Macs).
It looks and behaves the same as the Electron/WebGL build.

SCREENSAVER  The Screensaver folder holds the Ripple Tank screensaver, a calm, silent version of
             one scene. Its README.txt explains how to install it.

WINDOWS  Run Prismatica.exe (one file, nothing to install).
         SmartScreen may warn because it's unsigned: More info -> Run anyway.

MAC      Universal app (Intel + Apple Silicon), macOS 10.15 or newer.
         Drag Prismatica.app to Applications. It isn't notarized, so the first launch is blocked:
           - System Settings -> Privacy & Security -> "Open Anyway", or
           - Terminal:  xattr -cr /Applications/Prismatica.app

AUDIO SOURCES
  Open           MP3 / FLAC / WAV / OGG everywhere, plus M4A/AAC/ALAC/AIFF (Mac) or M4A/AAC/WMA (Windows).
                 Drag files or whole folders onto the window to make a playlist.
  System         Windows: whatever the PC is playing (WASAPI loopback, no setup).
                 Mac: macOS 13+; asks for "Screen & System Audio Recording" permission once
                 (only audio is used). Allow it, then reopen Prismatica.
                 Any macOS: install BlackHole and pick it under Input instead.
  Input          Microphone, line-in, or any loopback device.
  Demo           Built-in generated loop.

KEYS
  Space        play / pause           Left / Right  previous / next scene
  1 - 0        jump to scene          R             random scene
  L            lock scene / auto      N / P         next / previous track
  O            open files             F             fullscreen (or double-click)
  Q            render quality         H             hide controls
  Esc          leave fullscreen       ?             key list
