# Third-party notices

Prismatica is MIT-licensed (see [LICENSE](LICENSE)). It builds on these projects, each under its own license.

## Ripple Tank screensaver (`screensaver/`)

| Project | Used for | License |
|---|---|---|
| [GLFW](https://www.glfw.org/) 3.4 | Windows, OpenGL context, input (Windows) | zlib/libpng |
| stb_image_write (bundled with GLFW) | Test screenshots (Linux build only) | Public domain or MIT |
| [glad](https://github.com/Dav1dde/glad) (generated loader in `screensaver/third_party/glad`) | OpenGL function loading | Generated code: public domain / CC0; Khronos specification: Apache 2.0 |

GLFW is downloaded by CMake at build time and is not stored in this repository.

## Electron app (`electron/`)

| Project | Used for | License |
|---|---|---|
| [Electron](https://www.electronjs.org/) | Desktop app runtime | MIT (bundles Chromium, BSD-style, and others: see `LICENSES.chromium.html` in each build) |
| [@electron/packager](https://github.com/electron/packager) | Building the apps (development only) | BSD-2-Clause |

## Services

- **[LRCLIB](https://lrclib.net)** provides the synced lyrics the Electron and browser versions look up online. Lyrics are fetched when you play a song and are not part of this repository.

## Website (`website/`)

- Fonts: [Inter](https://fonts.google.com/specimen/Inter) and [Space Grotesk](https://fonts.google.com/specimen/Space+Grotesk), loaded from Google Fonts, under the SIL Open Font License 1.1.
