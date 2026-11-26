PRISMATICA (Electron / WebGL build) - psychedelic music visualizer
==================================================================
There is also a native C++/OpenGL build of Prismatica that looks the same; use it if this one is slow.

GPU FALLBACK  If WebGL can't start on your graphics card (e.g. OCLP-patched Macs), Prismatica
              restarts itself in software rendering and remembers that. It works, but at a lower
              resolution and frame rate. To try the GPU again, launch it once with --force-gpu.

SCREENSAVER  The Screensaver folder holds the Ripple Tank screensaver, a calm, silent version of
             one scene. Its README.txt explains how to install it.

WINDOWS  Unzip the whole folder, run Prismatica.exe (keep it next to the other files).
         SmartScreen may warn because the app is unsigned: More info -> Run anyway.

MAC      Unzip, drag Prismatica.app to Applications.
         The app is not notarized, so the first launch is blocked. Either:
           - System Settings -> Privacy & Security -> "Open Anyway", or
           - Terminal:  xattr -cr /Applications/Prismatica.app
         Apple Silicon Macs: use the arm64 zip. Intel Macs: use the x64 zip.

AUDIO SOURCES
  Open music     MP3 / FLAC / WAV / OGG / M4A, or drag files onto the window
  System audio   whatever the computer is playing (Spotify, a browser, a game...)
                 Mac: needs macOS 14.2+ and asks for audio-capture permission once
  Microphone     room sound or line-in
  Demo track     built-in generated loop

LYRICS  (music files only - not System audio / Microphone)
  Press Y or the Lyrics button. The line being sung sits bottom-left and lights up as a whole line.
  Prismatica looks for lyrics in this order:
    1. a .lrc file with the same name next to the song (Song.mp3 + Song.lrc), or dropped in with it
    2. timed lyrics embedded in the file's tags
    3. a lyrics result you picked by hand for this file before
    4. LRCLIB (lrclib.net), using the file's tags or its filename. Junk like "(Official Video)",
       "[video id]", "feat. ...", "- Topic" and "VEVO" is stripped before searching. Sends the song's
       title, artist, album and length. Shift+Y turns the online lookup off/on.
  If nothing is found: press S (or click "Search lyrics") and pick the right song from the list.
  Your pick is remembered for that file.
  Timing off? [ and ] shift the lyrics 0.2s earlier/later (remembered per song), \ resets.
  Troubleshooting: every lookup is written to lyrics-log.txt in Prismatica's data folder
    Windows: %APPDATA%\Prismatica      Mac: ~/Library/Application Support/Prismatica

KEYS
  Space        play / pause           Left / Right  previous / next scene
  1 - 0        jump to scene          R             random scene
  L            lock scene / auto      N / P         next / previous track
  O            open files             F             fullscreen (or double-click)
  Q            render quality         H             hide controls
  Y            lyrics on / off        Shift+Y       online lyrics lookup on / off
  S            search lyrics by hand  [ / ]         lyrics earlier / later (\ resets)
  ,            settings (gear button): scene name pop-up, auto-cycle speed, lyrics, quality
