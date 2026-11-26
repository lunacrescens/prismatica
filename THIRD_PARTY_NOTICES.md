# Third-party notices

Prismatica is MIT-licensed (see [LICENSE](LICENSE)). It builds on these projects, each under its own license.

## Native app and screensaver (`native/`)

| Project | Used for | License |
|---|---|---|
| [GLFW](https://www.glfw.org/) 3.4 | Windows, OpenGL context, input | zlib/libpng |
| [Dear ImGui](https://github.com/ocornut/imgui) 1.91.5 | On-screen controls | MIT |
| [miniaudio](https://miniaud.io/) 0.11.22 | Audio playback, capture, decoding | Public domain (Unlicense) or MIT No Attribution |
| [stb_vorbis](https://github.com/nothings/stb), stb_image_write | Ogg Vorbis decoding, screenshots | Public domain or MIT |
| [portable-file-dialogs](https://github.com/samhocevar/portable-file-dialogs) | Open-file dialog | WTFPL |
| [glad](https://github.com/Dav1dde/glad) (generated loader in `native/third_party/glad`) | OpenGL function loading | Generated code: public domain / CC0; Khronos specification: Apache 2.0 |
| [Roboto](https://fonts.google.com/specimen/Roboto) (embedded in `native/src/font_roboto.h`) | UI font | Apache 2.0 |

GLFW, Dear ImGui, miniaudio and portable-file-dialogs are downloaded by CMake at build time and are not stored in this repository.

## Electron app (`electron/`)

| Project | Used for | License |
|---|---|---|
| [Electron](https://www.electronjs.org/) | Desktop app runtime | MIT (bundles Chromium, BSD-style, and others: see `LICENSES.chromium.html` in each build) |
| [@electron/packager](https://github.com/electron/packager) | Building the apps (development only) | BSD-2-Clause |

## Services

- **[LRCLIB](https://lrclib.net)** provides the synced lyrics the Electron and browser versions look up online. Lyrics are fetched when you play a song and are not part of this repository.

## Website (`website/`)

- Fonts: [Inter](https://fonts.google.com/specimen/Inter) and [Space Grotesk](https://fonts.google.com/specimen/Space+Grotesk), loaded from Google Fonts, under the SIL Open Font License 1.1.
