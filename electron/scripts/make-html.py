#!/usr/bin/env python3
"""Fold the Electron app into one self-contained Prismatica.html for any modern browser.
Usage: python3 scripts/make-html.py [output path]   (default: dist/Prismatica.html)
Reuses the app's own files; only the Electron-specific bits are swapped for browser equivalents."""
import re, pathlib, sys

APP = pathlib.Path(__file__).resolve().parent.parent          # the electron/ folder
OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else APP / 'dist' / 'Prismatica.html')
rd = lambda n: (APP / n).read_text(encoding='utf-8')

def must(src, old, new, what):
    if old not in src: sys.exit(f'patch failed: {what}')
    return src.replace(old, new)

# ---- browser stand-in for the Electron preload: LRCLIB lookup straight from the page ----
main = rd('lyrics-main.js')
core = main[main.index('// ---------- name cleanup'):main.index('function register(')]
core = must(core, "{ headers: { 'User-Agent': UA, 'Lrclib-Client': UA }, signal: ctl.signal }", "{ signal: ctl.signal }",
            'plain GET (custom headers would trigger a CORS preflight)')
core = must(core, "  const key = crypto.createHash('sha1').update(", "  const key = 'lyc:' + (", 'cache key')
core = must(core, ".join('|')).digest('hex');", ".join('|'));", 'cache key end')
core = must(core, "  const cacheFile = cacheDir && path.join(cacheDir, key + '.json');\n", '', 'cache file')
core = must(core, "try { if (cacheFile) { const c = JSON.parse(fs.readFileSync(cacheFile, 'utf8'));",
            "try { const raw = localStorage.getItem(key); if (raw) { const c = JSON.parse(raw);", 'cache read')
core = must(core, "try { if (cacheFile && out.synced) { fs.mkdirSync(cacheDir, { recursive: true }); fs.writeFileSync(cacheFile, JSON.stringify(out)); } } catch (e) {}",
            "try { if (out.synced) localStorage.setItem(key, JSON.stringify(out)); } catch (e) {}", 'cache write')
for bad in ('fs.', 'path.', 'crypto.', 'require('):
    if bad in core: sys.exit(f'leftover Node API in lyrics core: {bad}')
shim = """// Browser stand-in for the Electron preload (window.prismatica): lyrics lookups go straight to LRCLIB.
(() => {
const API = 'https://lrclib.net/api';
const log = () => {};
""" + core + """
const f = (u, o) => fetch(u, o);
const str = (v) => String(v || '').slice(0, 300);
window.prismatica = {
  sidecarLyrics: async () => null,            // a page can't read the song's folder; drop the .lrc in with the song instead
  fetchLyrics: (q) => (!q || !str(q.title).trim()) ? Promise.resolve({ synced: null, plain: false })
    : lookup(f, { title: str(q.title), artist: str(q.artist), album: str(q.album), duration: +q.duration || 0 }, null),
  searchLyrics: async (text, duration) => { try { return await search(f, str(text), +duration || 0); } catch (e) { return { error: String(e.message || e) }; } },
  lyricsById: async (id) => { try { return await getById(f, +id); } catch (e) { return { error: String(e.message || e) }; } },
  lyricsLogPath: async () => null,
};
})();
"""

# ---- renderer: no Electron relaunch on WebGL failure ----
renderer = rd('renderer.js')
renderer = must(renderer, "if (!SOFT && !SHOT) location.hash = 'nogl';   // main process relaunches with software rendering\n", '', 'no relaunch')
renderer = re.sub(r"document\.body\.innerHTML = '<p style=\"padding:40px\">' \+ \(SOFT \?.*?\) \+ '</p>';",
    "document.body.innerHTML = '<p style=\"padding:40px;max-width:640px;line-height:1.6\">Prismatica needs WebGL 2, which this browser can\\'t provide (' + why + ').<br><br>"
    "Try a different browser, or turn on hardware acceleration in its settings. On Macs where Chrome\\'s WebGL is broken, Safari or Firefox may still work.</p>';",
    renderer, count=1, flags=re.S)
if "Prismatica needs WebGL 2" not in renderer: sys.exit('patch failed: WebGL message')
# the browser version plays music files and the demo; system audio and the microphone are desktop-app only
renderer = must(renderer, "      if (src === 'system') { await Audio.useSystem(); $('title').textContent = 'System audio'; }\n", '', 'no system audio')
renderer = must(renderer, "      if (src === 'mic') { await Audio.useMic(); $('title').textContent = 'Microphone'; }\n", '', 'no microphone')

# ---- page ----
html = rd('index.html')
html = re.sub(r'<meta http-equiv="Content-Security-Policy"[^>]*>\n', '', html)   # inline scripts + LRCLIB need a looser policy than the app's
html = must(html, '<link rel="stylesheet" href="style.css">', '<style>\n' + rd('style.css') + '\n</style>', 'inline css')
html = must(html, '    <button data-src="system"><b>System audio</b><span>Visualize whatever your computer is playing</span></button>\n', '', 'welcome: no system audio')
html = must(html, '    <button data-src="mic"><b>Microphone</b><span>React to the room or a line-in</span></button>\n', '', 'welcome: no microphone')
html = must(html, '      <button data-src="system" title="Capture system audio">System</button>\n', '', 'bar: no system audio')
html = must(html, '      <button data-src="mic" title="Microphone">Mic</button>\n', '', 'bar: no microphone')
html = must(html, '  </div>\n</div>\n\n<div id="bar">',
    '  </div>\n  <p class="getapp">Want it to react to Spotify, YouTube or a microphone? '
    '<a href="https://lunacrescens.github.io/prismatica/download.html" target="_blank" rel="noopener">Get the desktop app</a></p>\n'
    '</div>\n\n<div id="bar">', 'desktop app note')
html = must(html, '</style>', '#welcome p.getapp{margin:22px 0 0;font-size:13px;opacity:.6}\n#welcome p.getapp a{color:inherit}\n</style>', 'note style')
scripts = [('shaders.js', rd('shaders.js')), ('audio.js', rd('audio.js')), ('lyrics.js', rd('lyrics.js')), (None, shim), ('renderer.js', renderer)]
tags = re.findall(r'<script src="[^"]+"></script>\n?', html)
if len(tags) != 4: sys.exit(f'expected 4 script tags, found {len(tags)}')
first = html.index(tags[0])
for t in tags: html = html.replace(t, '')
block = ''.join(f'<script>\n// ---- {n or "browser lyrics lookup"} ----\n{s.replace("</script", "<\\/script")}\n</script>\n' for n, s in scripts)
html = html[:first] + block + html[first:]
import base64
icon = base64.b64encode((APP / 'build' / 'icon-64.png').read_bytes()).decode()
html = must(html, '<title>Prismatica</title>', '<title>Prismatica</title>\n<meta name="viewport" content="width=device-width, initial-scale=1">\n'
            f'<link rel="icon" type="image/png" href="data:image/png;base64,{icon}">', 'viewport + icon')
OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text(html, encoding='utf-8')
print(f'wrote {OUT} ({len(html) // 1024} KB)')
