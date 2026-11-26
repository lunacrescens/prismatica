// Lyrics: LRC parsing (line-timed and word-timed), tag reading, lookup order, and the word-by-word overlay.
const Lyrics = (() => {
  // ---------- LRC parsing ----------
  const stamp = (m, s) => (+m) * 60 + parseFloat(String(s).replace(',', '.'));
  const CJK = /[぀-ヿ㐀-鿿가-힯]/;
  function splitWords(text) {
    if (!/\s/.test(text.trim()) && CJK.test(text)) return [...text.trim()];           // no spaces: one unit per character
    return text.trim().split(/\s+/).filter(Boolean);
  }
  const weight = (w) => 0.18 + 0.055 * Math.min(14, [...w].length);                   // rough sung length of a word, seconds

  function parseLRC(text) {
    if (!text) return null;
    text = text.replace(/^﻿/, '');
    let offset = 0; const rows = [];
    for (const raw of text.split(/\r?\n/)) {
      let line = raw.trim(); if (!line) continue;
      const off = line.match(/^\[offset:\s*([+-]?\d+)\s*\]/i); if (off) { offset = +off[1] / 1000; continue; }
      const times = []; let m;
      while ((m = line.match(/^\[(\d{1,3}):(\d{1,2}(?:[.,:]\d{1,3})?)\]/))) {
        times.push(stamp(m[1], m[2].replace(/:(\d+)$/, '.$1'))); line = line.slice(m[0].length);
      }
      if (!times.length) continue;                                                     // metadata tag or junk
      for (const t of times) rows.push({ t, body: line.trim() });
    }
    if (!rows.length) return null;
    rows.sort((a, b) => a.t - b.t);
    const lines = []; let wordTimed = false;
    for (let i = 0; i < rows.length; i++) {
      const { t, body } = rows[i]; if (!body) continue;                                // empty row = "previous line ends here"
      const next = i + 1 < rows.length ? rows[i + 1].t : null;
      const words = [];
      if (/<\d{1,3}:\d{1,2}(?:[.,]\d{1,3})?>/.test(body)) {
        // enhanced LRC: <mm:ss.xx>word <mm:ss.xx>word ... [<mm:ss.xx> end]
        wordTimed = true;
        const parts = body.split(/<(\d{1,3}):(\d{1,2}(?:[.,]\d{1,3})?)>/);            // [text, m, s, text, m, s, text...]
        const segs = []; if (parts[0].trim()) segs.push({ t, text: parts[0] });
        for (let k = 1; k < parts.length; k += 3) segs.push({ t: stamp(parts[k], parts[k + 1]), text: parts[k + 2] || '' });
        for (let k = 0; k < segs.length; k++) {
          const ws = splitWords(segs[k].text); if (!ws.length) continue;
          let segEnd = k + 1 < segs.length ? segs[k + 1].t : (next != null ? Math.min(next, segs[k].t + 1.2 * ws.reduce((a, w) => a + weight(w), 0)) : segs[k].t + ws.reduce((a, w) => a + weight(w), 0));
          if (segEnd <= segs[k].t) segEnd = segs[k].t + 0.2;
          const total = ws.reduce((a, w) => a + weight(w), 0); let c = segs[k].t;
          for (const w of ws) { const d = (segEnd - segs[k].t) * weight(w) / total; words.push({ text: w, t: c, end: c + d }); c += d; }
        }
      } else {
        // line-timed: estimate each word's slot from its length, squeezed into the gap before the next line
        const ws = splitWords(body); const est = ws.reduce((a, w) => a + weight(w), 0);
        const gap = next != null ? next - t : null;
        const span = gap != null ? Math.max(0.2, Math.min(gap - 0.05, est * 1.6)) : est * 1.3;
        let c = t; for (const w of ws) { const d = span * weight(w) / est; words.push({ text: w, t: c, end: c + d }); c += d; }
      }
      if (!words.length) continue;
      lines.push({ t: words[0].t, end: words[words.length - 1].end, words, text: words.map((w) => w.text).join(' ') });
    }
    if (!lines.length) return null;
    for (const l of lines) { l.t -= offset; l.end -= offset; for (const w of l.words) { w.t -= offset; w.end -= offset; } }
    return { lines, wordTimed };
  }
  const looksSynced = (s) => !!s && /^\s*\[\d{1,3}:\d{1,2}/m.test(s);

  // ---------- tags: just enough of ID3v2, FLAC and MP4 to get title / artist / album / embedded lyrics ----------
  function decodeText(bytes, enc) {
    try {
      if (enc === 1) return new TextDecoder(bytes[0] === 0xFE && bytes[1] === 0xFF ? 'utf-16be' : 'utf-16le').decode(bytes).replace(/^﻿/, '');
      if (enc === 2) return new TextDecoder('utf-16be').decode(bytes);
      if (enc === 3) return new TextDecoder('utf-8').decode(bytes);
      return new TextDecoder('windows-1252').decode(bytes);
    } catch (e) { return ''; }
  }
  const clean = (s) => s.replace(/\0+$/g, '').split('\0')[0].trim();
  function parseID3(b) {
    const out = {}; if (b.length < 10 || b[0] !== 0x49 || b[1] !== 0x44 || b[2] !== 0x33) return out;
    const ver = b[3], flags = b[5];
    const size = (b[6] & 127) << 21 | (b[7] & 127) << 14 | (b[8] & 127) << 7 | (b[9] & 127);
    let p = 10; const end = Math.min(b.length, 10 + size);
    if (flags & 0x40 && ver >= 3) p += ver === 4 ? ((b[10] & 127) << 21 | (b[11] & 127) << 14 | (b[12] & 127) << 7 | (b[13] & 127)) : 4 + (b[10] << 24 | b[11] << 16 | b[12] << 8 | b[13]);
    const idLen = ver === 2 ? 3 : 4, hdr = ver === 2 ? 6 : 10;
    const map = { TIT2: 'title', TT2: 'title', TPE1: 'artist', TP1: 'artist', TALB: 'album', TAL: 'album', USLT: 'lyrics', ULT: 'lyrics' };
    while (p + hdr <= end) {
      const id = String.fromCharCode(...b.subarray(p, p + idLen)); if (!/^[A-Z0-9]+$/.test(id)) break;
      let fs = ver === 2 ? b[p + 3] << 16 | b[p + 4] << 8 | b[p + 5]
        : ver === 4 ? (b[p + 4] & 127) << 21 | (b[p + 5] & 127) << 14 | (b[p + 6] & 127) << 7 | (b[p + 7] & 127)
        : (b[p + 4] << 24 | b[p + 5] << 16 | b[p + 6] << 8 | b[p + 7]) >>> 0;
      p += hdr; if (fs <= 0 || p + fs > end) break;
      const key = map[id];
      if (key && !out[key]) {
        const d = b.subarray(p, p + fs), enc = d[0];
        if (key === 'lyrics') {                                                       // enc, 3-byte language, descriptor\0, text
          let q = 4; const wide = enc === 1 || enc === 2;
          while (q < d.length - (wide ? 1 : 0)) { if (wide ? (d[q] === 0 && d[q + 1] === 0) : d[q] === 0) { q += wide ? 2 : 1; break; } q += wide ? 2 : 1; }
          out.lyrics = decodeText(d.subarray(q), enc).replace(/\0+$/g, '');
        } else out[key] = clean(decodeText(d.subarray(1), enc));
      }
      p += fs;
    }
    return out;
  }
  function parseVorbisComments(b, p, out) {
    const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
    if (p + 8 > b.length) return; const vl = dv.getUint32(p, true); p += 4 + vl; if (p + 4 > b.length) return;
    const n = dv.getUint32(p, true); p += 4;
    for (let i = 0; i < n && p + 4 <= b.length; i++) {
      const l = dv.getUint32(p, true); p += 4; if (p + l > b.length) break;
      const s = new TextDecoder('utf-8').decode(b.subarray(p, p + l)); p += l;
      const eq = s.indexOf('='); if (eq < 0) continue; const k = s.slice(0, eq).toUpperCase(), v = s.slice(eq + 1);
      if (k === 'TITLE' && !out.title) out.title = v.trim(); else if (k === 'ARTIST' && !out.artist) out.artist = v.trim();
      else if (k === 'ALBUM' && !out.album) out.album = v.trim(); else if ((k === 'LYRICS' || k === 'UNSYNCEDLYRICS' || k === 'SYNCEDLYRICS') && (!out.lyrics || looksSynced(v))) out.lyrics = v;
    }
  }
  function parseFLAC(b) {
    const out = {}; if (b.length < 8 || String.fromCharCode(b[0], b[1], b[2], b[3]) !== 'fLaC') return out;
    let p = 4;
    while (p + 4 <= b.length) {
      const last = b[p] & 0x80, type = b[p] & 0x7f, len = b[p + 1] << 16 | b[p + 2] << 8 | b[p + 3]; p += 4;
      if (type === 4) { parseVorbisComments(b.subarray(p, Math.min(b.length, p + len)), 0, out); break; }
      p += len; if (last) break;
    }
    return out;
  }
  async function parseMP4(file) {
    const out = {}; const rd = async (o, n) => new Uint8Array(await file.slice(o, o + n).arrayBuffer());
    const u32 = (b, o) => (b[o] << 24 | b[o + 1] << 16 | b[o + 2] << 8 | b[o + 3]) >>> 0;
    let pos = 0, moov = null;
    for (let i = 0; i < 64 && pos + 8 <= file.size; i++) {                              // walk top-level atoms to find moov
      const h = await rd(pos, 16); if (h.length < 8) break;
      let size = u32(h, 0); const type = String.fromCharCode(h[4], h[5], h[6], h[7]);
      if (size === 1) size = u32(h, 8) * 4294967296 + u32(h, 12); else if (size === 0) size = file.size - pos;
      if (size < 8) break;
      if (type === 'moov') { if (size <= 24e6) moov = await rd(pos, size); break; }
      pos += size;
    }
    if (!moov) return out;
    const find = (b, s, e, name) => { for (let p = s; p + 8 <= e;) { const sz = u32(b, p); if (sz < 8 || p + sz > e) return null; if (String.fromCharCode(b[p + 4], b[p + 5], b[p + 6], b[p + 7]) === name) return [p + 8, p + sz]; p += sz; } return null; };
    let r = find(moov, 8, moov.length, 'udta'); if (!r) return out;
    r = find(moov, r[0], r[1], 'meta'); if (!r) return out;
    r = find(moov, r[0] + 4, r[1], 'ilst'); if (!r) return out;
    for (const [atom, key] of [['©nam', 'title'], ['©ART', 'artist'], ['©alb', 'album'], ['©lyr', 'lyrics']]) {
      const a = find(moov, r[0], r[1], atom); if (!a) continue; const d = find(moov, a[0], a[1], 'data'); if (!d) continue;
      out[key] = new TextDecoder('utf-8').decode(moov.subarray(d[0] + 8, d[1])).trim();
    }
    return out;
  }
  async function readTags(file) {
    try {
      const head = new Uint8Array(await file.slice(0, 12).arrayBuffer());
      const sig = String.fromCharCode(...head.subarray(0, 4));
      if (sig.startsWith('ID3')) {
        const size = ((head[6] & 127) << 21 | (head[7] & 127) << 14 | (head[8] & 127) << 7 | (head[9] & 127)) + 10;
        return parseID3(new Uint8Array(await file.slice(0, Math.min(size, 12e6)).arrayBuffer()));
      }
      if (sig === 'fLaC') return parseFLAC(new Uint8Array(await file.slice(0, Math.min(file.size, 4e6)).arrayBuffer()));
      if (String.fromCharCode(...head.subarray(4, 8)) === 'ftyp') return await parseMP4(file);
    } catch (e) { console.warn('tag read failed', e); }
    return {};
  }
  // "03 - Artist - Title", "03. Title", "Artist - Title (Official Audio)" -> best guess
  function guessFromName(name) {
    let s = name.replace(/\.[^.]+$/, '').replace(/_/g, ' ').replace(/\s*[\[(](official|audio|video|lyrics?|hd|hq|remaster(ed)?|visuali[sz]er|explicit|mv)[^\])]*[\])]/gi, '').trim();
    s = s.replace(/^\d{1,3}\s*[-.)]\s*/, '');
    const parts = s.split(/\s+[-–—]\s+/);
    if (parts.length >= 2) return { artist: parts[0].trim(), title: parts.slice(1).join(' - ').trim() };
    return { title: s };
  }

  if (typeof document === 'undefined') return { parseLRC, looksSynced, parseID3, parseFLAC, guessFromName };

  // ---------- overlay ----------
  const $ = (id) => document.getElementById(id);
  const panel = $('lyrics'), scroller = $('lyricsLines');
  const store = { get(k, d) { try { const v = localStorage.getItem(k); return v === null ? d : v === '1'; } catch (e) { return d; } }, set(k, v) { try { localStorage.setItem(k, v ? '1' : '0'); } catch (e) {} } };
  let enabled = store.get('lyrics', true), online = store.get('lyricsOnline', true);
  let data = null, lineEls = [], cur = -2, curWord = -2, token = 0, onStatus = () => {};
  let item = null, offset = 0, lastQuery = null;
  const sig = (it) => it && it.file ? it.file.name + ':' + it.file.size : it ? it.name : '';
  const kv = { get(k) { try { return localStorage.getItem(k); } catch (e) { return null; } }, set(k, v) { try { v == null ? localStorage.removeItem(k) : localStorage.setItem(k, String(v)); } catch (e) {} } };

  function build(parsed) {
    data = parsed; lineEls = []; cur = -2; curWord = -2; scroller.textContent = '';
    if (data) for (const l of data.lines) {
      const div = document.createElement('div'); div.className = 'ly';
      l.els = l.words.map((w) => { const s = document.createElement('span'); s.textContent = w.text; div.appendChild(s); div.appendChild(document.createTextNode(' ')); return s; });
      scroller.appendChild(div); lineEls.push(div);
    }
    show();
  }
  function show() { panel.classList.toggle('on', enabled && !!data); }
  function clear() { token++; item = null; build(null); }

  // Called every frame with the playback position in seconds.
  function update(t) {
    if (!data || !enabled) return;
    t -= offset;                                                                       // + offset = lyrics later
    const L = data.lines; let lo = 0, hi = L.length - 1, i = -1;
    while (lo <= hi) { const mid = (lo + hi) >> 1; if (L[mid].t <= t) { i = mid; lo = mid + 1; } else hi = mid - 1; }
    if (i !== cur) {
      if (cur >= 0 && lineEls[cur]) { lineEls[cur].classList.remove('cur'); }
      cur = i; curWord = -2;
      if (i >= 0) lineEls[i].classList.add('cur');
      const target = lineEls[Math.max(0, i)];                                          // before the first line, park it as "up next"
      const y = target ? target.offsetTop + (i >= 0 ? target.offsetHeight / 2 : -target.offsetHeight * 0.2) : 0;
      scroller.style.transform = `translateY(${Math.round(panel.clientHeight * 0.42 - y)}px)`;
    }
    if (i < 0) return;
    const gap = i + 1 < L.length ? L[i + 1].t - L[i].end : Infinity;                 // only dim during a real instrumental break
    lineEls[i].classList.toggle('done', gap > 3 && t > L[i].end + 1.5);
  }

  // Lookup order: .lrc dropped with the song -> .lrc next to the file -> lyrics embedded in the file -> LRCLIB (if allowed).
  async function load(it, getDuration) {
    item = it; offset = +(kv.get('lyroff:' + sig(it)) || 0); lastQuery = null;
    const my = ++token; build(null);
    const use = (text, src, info) => { if (my !== token) return true; const p = looksSynced(text) ? parseLRC(text) : null; if (!p) return false; build(p); onStatus('found', src, p.wordTimed, info); return true; };
    try {
      if (it.lrc && use(await it.lrc.text(), 'file')) return;
      const api = window.prismatica;
      if (api && it.file) { const side = await api.sidecarLyrics(it.file); if (my !== token) return; if (side && use(side, 'file')) return; }
      const tags = it.file ? await readTags(it.file) : {}; if (my !== token) return;
      if (tags.lyrics && use(tags.lyrics, 'embedded')) return;
      if (!enabled) { it.lyricsPending = true; return; }                             // don't go online for lyrics nobody is looking at
      it.lyricsPending = false;
      const guess = guessFromName(it.file ? it.file.name : it.name);
      const q = { title: tags.title || guess.title, artist: tags.artist || guess.artist || '', album: tags.album || '', duration: Math.round(await getDuration()) || 0 };
      lastQuery = q;
      if (!online || !api) { onStatus('none', null, false, { searched: 'online lookup is off' }); return; }
      if (my !== token || !q.title) return;
      const picked = kv.get('lyrid:' + sig(it));                                        // a result chosen by hand earlier
      if (picked) { const r = await api.lyricsById(picked); if (my !== token) return; if (r && r.synced && use(r.synced, 'online', { picked: true })) return; }
      const res = await api.fetchLyrics(q); if (my !== token) return;
      if (res && res.synced && use(res.synced, 'online', res)) return;
      onStatus(res && res.error ? 'offline' : res && res.plain ? 'unsynced' : 'none', null, false, res || {});
    } catch (e) { console.warn('lyrics', e); if (my === token) onStatus('none'); }
  }

  // manual pick from the search panel; remembered for this file
  async function choose(id) {
    const api = window.prismatica; if (!api || !item) return false;
    const my = ++token; const r = await api.lyricsById(id); if (my !== token) return false;
    const p = r && r.synced && looksSynced(r.synced) ? parseLRC(r.synced) : null;
    if (!p) return false;
    kv.set('lyrid:' + sig(item), id); build(p); return true;
  }
  function nudge(d) { if (!item) return offset; offset = Math.round((offset + d) * 100) / 100; if (Math.abs(offset) < 1e-6) offset = 0; kv.set('lyroff:' + sig(item), offset || null); cur = -2; return offset; }

  return {
    parseLRC, load, clear, update, build, choose, nudge,
    get offset() { return offset; }, get query() { return lastQuery; }, get item() { return item; },
    get enabled() { return enabled; }, get online() { return online; }, get has() { return !!data; },
    setEnabled(v) { enabled = v; store.set('lyrics', v); cur = -2; show(); },
    setOnline(v) { online = v; store.set('lyricsOnline', v); },
    set onStatus(f) { onStatus = f; },
  };
})();
if (typeof module !== 'undefined') module.exports = Lyrics;
