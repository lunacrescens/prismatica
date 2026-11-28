// Main-process side of lyrics: read a .lrc next to the song, and look songs up on LRCLIB (https://lrclib.net).
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const API = process.env.PRISMATICA_LRCLIB_URL || 'https://lrclib.net/api';
let UA = 'Prismatica (desktop music visualizer)';
let logFile = null;
function log(...a) { if (!logFile) return; try { fs.appendFileSync(logFile, new Date().toISOString() + ' ' + a.join(' ') + '\n'); } catch (e) {} }

function decode(buf) {
  if (buf[0] === 0xFF && buf[1] === 0xFE) return buf.toString('utf16le').replace(/^﻿/, '');
  if (buf[0] === 0xFE && buf[1] === 0xFF) return Buffer.from(buf).swap16().toString('utf16le').replace(/^﻿/, '');
  return buf.toString('utf8').replace(/^﻿/, '');
}

// "Song.mp3" -> "Song.lrc" (any letter case) or "Song.mp3.lrc" in the same folder.
function sidecar(file) {
  try {
    if (!file || !path.isAbsolute(file)) return null;
    const dir = path.dirname(file), base = path.basename(file), stem = base.replace(/\.[^.]+$/, '');
    const want = new Set([(stem + '.lrc').toLowerCase(), (base + '.lrc').toLowerCase()]);
    const hit = fs.readdirSync(dir).find((n) => want.has(n.toLowerCase()));
    if (!hit) return null;
    const full = path.join(dir, hit);
    if (fs.statSync(full).size > 2e6) return null;
    return decode(fs.readFileSync(full));
  } catch (e) { return null; }
}

// ---------- name cleanup ----------
const JUNK = /\b(official|music|lyric|lyrics|video|audio|visuali[sz]er|hd|hq|4k|1080p|720p|remaster(ed)?(\s*\d{4})?|\d{4}\s*remaster(ed)?|explicit|clean|mv|m\/v|full\s*song|with\s*lyrics|color\s*coded|extended|radio\s*edit|single\s*version|album\s*version|mono|stereo|bonus\s*track)\b/i;
function cleanTitle(s) {
  s = String(s || '').replace(/_/g, ' ');
  s = s.replace(/\[[^\]]*\]/g, ' ');                                                   // [dQw4w9WgXcQ], [Official Video], [HD]
  s = s.replace(/\(([^)]*)\)/g, (m, inner) => (JUNK.test(inner) || /^\s*(feat|ft|featuring|prod|with)\b/i.test(inner)) ? ' ' : m);
  s = s.replace(/\s[-–—|]\s*(official|lyrics?|audio|video|visuali[sz]er|remaster(ed)?|\d{4}\s*remaster(ed)?|hd|hq|4k)\b.*$/i, '');
  s = s.replace(/\s(feat\.?|ft\.?|featuring)\s.*$/i, '');
  s = s.replace(/["“”]/g, '').replace(/\s+/g, ' ').trim();
  return s;
}
function cleanArtist(s) {
  s = String(s || '').replace(/_/g, ' ');
  s = s.replace(/\s*-\s*topic\s*$/i, '').replace(/vevo$/i, '').replace(/\s*\b(official|music)\s*$/i, '');
  s = s.replace(/\s(feat\.?|ft\.?|featuring)\s.*$/i, '').replace(/\s+/g, ' ').trim();
  return s;
}
const firstArtist = (s) => s.split(/\s*(?:,|&|\band\b|\bx\b|\/|;|\+)\s*/i)[0].trim();

const norm = (s) => String(s || '').toLowerCase().normalize('NFKD').replace(/[̀-ͯ]/g, '').replace(/&/g, ' and ')
  .replace(/[^\p{L}\p{N}]+/gu, ' ').trim();
const tokens = (s) => norm(s).split(' ').filter((t) => t && !['the', 'a', 'an'].includes(t));
// 0..1: how well two names agree (containment both ways, so "Song" ~ "Song - 2011 Remaster")
function sim(a, b) {
  const A = tokens(a), B = tokens(b);
  if (!A.length || !B.length) return norm(a) && norm(a) === norm(b) ? 1 : 0;
  const SB = new Set(B), SA = new Set(A);
  const inB = A.filter((t) => SB.has(t)).length / A.length, inA = B.filter((t) => SA.has(t)).length / B.length;
  return Math.max(Math.min(inA, inB), 0.85 * Math.max(inA, inB) * (Math.min(A.length, B.length) / Math.max(A.length, B.length)) ** 0.25);
}

// Score one LRCLIB record against what we know. Returns null when it's clearly a different song.
function score(r, q) {
  if (!r || r.instrumental || (!r.syncedLyrics && !r.plainLyrics)) return null;
  const rt = cleanTitle(r.trackName) || r.trackName;
  const ts = Math.max(sim(r.trackName, q.title), sim(rt, q.cleanTitle || q.title), sim(rt, q.title));
  if (ts < 0.6) return null;
  const as = q.artist ? Math.max(sim(r.artistName, q.artist), sim(r.artistName, q.cleanArtist || q.artist), sim(firstArtist(r.artistName || ''), firstArtist(q.cleanArtist || q.artist))) : 0.5;
  const dd = q.duration && r.duration ? Math.abs(r.duration - q.duration) : null;
  if (dd != null && dd > 25 && !(ts >= 0.9 && as >= 0.8 && dd <= 60)) return null;   // far too different a version (unless it's clearly the same song)
  let s = ts * 40 + as * 40 + (r.syncedLyrics ? 60 : 0);
  if (dd != null) s -= dd <= 2 ? 0 : dd <= 5 ? (dd - 2) * 3 : 9 + (dd - 5) * 2.5;
  if (q.artist && as < 0.4) s -= 35;                                                   // probably a cover / different artist
  return { s, dd, ts, as };
}
function pickBest(results, q) {
  let best = null;
  for (const r of Array.isArray(results) ? results : []) { const sc = score(r, q); if (sc && (!best || sc.s > best.sc.s)) best = { r, sc }; }
  return best;
}

async function getJSON(fetchFn, url) {
  const ctl = new AbortController(); const timer = setTimeout(() => ctl.abort(), 10000);
  try {
    const res = await fetchFn(url, { headers: { 'User-Agent': UA, 'Lrclib-Client': UA }, signal: ctl.signal });
    log('GET', url.replace(API, ''), '->', res.status);
    if (res.status === 404) return null;
    if (!res.ok) throw new Error('HTTP ' + res.status);
    return await res.json();
  } finally { clearTimeout(timer); }
}
const P = (o) => Object.entries(o).filter(([, v]) => v).map(([k, v]) => k + '=' + encodeURIComponent(v)).join('&');

// Try progressively looser searches; stop as soon as a confident, timed match shows up.
async function lookup(fetchFn, q, cacheDir) {
  q.cleanTitle = cleanTitle(q.title) || q.title;
  q.cleanArtist = cleanArtist(q.artist);
  const key = crypto.createHash('sha1').update(['v3', norm(q.cleanTitle), norm(q.cleanArtist), q.duration || 0].join('|')).digest('hex');
  const cacheFile = cacheDir && path.join(cacheDir, key + '.json');
  try { if (cacheFile) { const c = JSON.parse(fs.readFileSync(cacheFile, 'utf8')); log('cache hit', q.cleanTitle); return c; } } catch (e) {}
  log('lookup', JSON.stringify({ title: q.title, artist: q.artist, album: q.album, duration: q.duration, cleanTitle: q.cleanTitle, cleanArtist: q.cleanArtist }));
  const pool = new Map(); let best = null, anyOk = false, lastErr = null;
  const consider = (list) => { for (const r of [].concat(list || [])) if (r && r.id != null && !pool.has(r.id)) pool.set(r.id, r); best = pickBest([...pool.values()], q); };
  const good = () => best && best.r.syncedLyrics && best.sc.ts >= 0.85 && (!q.artist || best.sc.as >= 0.6) && (best.sc.dd == null || best.sc.dd <= 3);
  const steps = [];
  if (q.cleanArtist && q.duration) {
    steps.push(() => `${API}/get?${P({ track_name: q.title, artist_name: q.artist, album_name: q.album, duration: q.duration })}`);
    steps.push(() => `${API}/get?${P({ track_name: q.cleanTitle, artist_name: q.cleanArtist, duration: q.duration })}`);
  }
  if (q.cleanArtist) steps.push(() => `${API}/search?${P({ track_name: q.cleanTitle, artist_name: q.cleanArtist })}`);
  if (q.cleanArtist && firstArtist(q.cleanArtist) !== q.cleanArtist) steps.push(() => `${API}/search?${P({ track_name: q.cleanTitle, artist_name: firstArtist(q.cleanArtist) })}`);
  steps.push(() => `${API}/search?${P({ q: [q.cleanArtist, q.cleanTitle].filter(Boolean).join(' ') })}`);
  steps.push(() => `${API}/search?${P({ track_name: q.cleanTitle })}`);
  const tried = new Set();
  for (const step of steps) {
    const url = step(); if (tried.has(url)) continue; tried.add(url);              // e.g. the cleaned-up names are the same as the originals
    try { consider(await getJSON(fetchFn, url)); anyOk = true; } catch (e) { lastErr = e; log('error', String(e && e.message || e)); }
    if (good()) break;
  }
  if (!anyOk) return { error: String(lastErr && lastErr.message || lastErr), searched: q.cleanTitle };
  const out = best ? {
    synced: best.r.syncedLyrics || null, plain: !!best.r.plainLyrics, id: best.r.id,
    match: { title: best.r.trackName, artist: best.r.artistName, duration: best.r.duration },
    approx: best.sc.dd != null && best.sc.dd > 3, searched: q.cleanTitle,
  } : { synced: null, plain: false, searched: [q.cleanArtist, q.cleanTitle].filter(Boolean).join(' - ') };
  log('result', out.synced ? `synced id=${out.id} "${out.match.artist} - ${out.match.title}" dd=${best.sc.dd}` : out.plain ? 'plain only' : 'nothing', `(${pool.size} candidates)`);
  try { if (cacheFile && out.synced) { fs.mkdirSync(cacheDir, { recursive: true }); fs.writeFileSync(cacheFile, JSON.stringify(out)); } } catch (e) {}   // only remember hits
  return out;
}

// Manual search for the picker: a short list of candidates.
async function search(fetchFn, text, duration) {
  const list = await getJSON(fetchFn, `${API}/search?${P({ q: text })}`) || [];
  return list.filter((r) => r && !r.instrumental && (r.syncedLyrics || r.plainLyrics))
    .map((r) => ({ id: r.id, title: r.trackName, artist: r.artistName, album: r.albumName, duration: r.duration, synced: !!r.syncedLyrics }))
    .sort((a, b) => (b.synced - a.synced) || (duration ? Math.abs((a.duration || 1e9) - duration) - Math.abs((b.duration || 1e9) - duration) : 0))
    .slice(0, 12);
}
async function getById(fetchFn, id) { const r = await getJSON(fetchFn, `${API}/get/${encodeURIComponent(id)}`); return r ? { synced: r.syncedLyrics || null, plain: !!r.plainLyrics } : null; }

function register(ipcMain, app, net) {
  UA = `Prismatica/${app.getVersion()} (desktop music visualizer; https://github.com/lunacrescens/prismatica)`;
  const dir = app.getPath('userData');
  const cacheDir = path.join(dir, 'lyrics-cache');
  logFile = path.join(dir, 'lyrics-log.txt');
  try { if (fs.statSync(logFile).size > 512e3) fs.renameSync(logFile, logFile + '.old'); } catch (e) {}
  const f = (u, o) => net.fetch(u, o);
  const str = (v) => String(v || '').slice(0, 300);
  ipcMain.handle('lyrics:sidecar', (e, file) => sidecar(file));
  ipcMain.handle('lyrics:fetch', (e, q) => {
    if (!q || !str(q.title).trim()) return { synced: null, plain: false };
    return lookup(f, { title: str(q.title), artist: str(q.artist), album: str(q.album), duration: +q.duration || 0 }, cacheDir);
  });
  ipcMain.handle('lyrics:search', async (e, text, duration) => { try { return await search(f, str(text), +duration || 0); } catch (err) { return { error: String(err.message || err) }; } });
  ipcMain.handle('lyrics:byId', async (e, id) => { try { return await getById(f, +id); } catch (err) { return { error: String(err.message || err) }; } });
  ipcMain.handle('lyrics:logPath', () => logFile);
}

module.exports = { register, sidecar, pickBest, lookup, search, cleanTitle, cleanArtist, sim };
