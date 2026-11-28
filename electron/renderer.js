(() => {
  const SHOT = new URLSearchParams(location.search).has('shot');
  const SOFT = new URLSearchParams(location.search).has('soft');
  const $ = (id) => document.getElementById(id);
  const canvas = $('gl');
  const gl = canvas.getContext('webgl2', { alpha: false, antialias: false, powerPreference: 'high-performance' });
  const noGL = (why) => {
    if (!SOFT && !SHOT) location.hash = 'nogl';   // main process relaunches with software rendering
    document.body.innerHTML = '<p style="padding:40px">' + (SOFT ? 'WebGL2 is not available on this machine, even in software mode (' + why + ').' : 'Graphics unavailable, switching to software rendering...') + '</p>';
  };
  if (!gl) { noGL('no context'); return; }
  // A lost context (GPU driver reset, waking from sleep) normally comes back by itself: rebuild everything then.
  // Only if it stays gone do we give up on the GPU.
  let lostTimer = null;
  canvas.addEventListener('webglcontextlost', (e) => { e.preventDefault(); clearTimeout(lostTimer); lostTimer = setTimeout(() => noGL('context lost'), 8000); });
  canvas.addEventListener('webglcontextrestored', () => {
    clearTimeout(lostTimer);
    try { initGL(); } catch (e) { console.error(e); noGL('could not rebuild after a context loss'); }
  });
  let floatOK = false;

  // ---------- GL helpers ----------
  function program(vs, fs) {
    const p = gl.createProgram();
    for (const [type, src] of [[gl.VERTEX_SHADER, vs], [gl.FRAGMENT_SHADER, fs]]) {
      const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
      gl.attachShader(p, s);
    }
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
    const u = {}; const n = gl.getProgramParameter(p, gl.ACTIVE_UNIFORMS);
    for (let i = 0; i < n; i++) { const info = gl.getActiveUniform(p, i); u[info.name] = gl.getUniformLocation(p, info.name); }
    return { p, u };
  }
  let progF, progP, progC, audioTex, W = 0, H = 0, quality = SOFT ? 0 : 1, targets = [];
  function initGL() {                    // every GL object; run again after a lost context comes back
    floatOK = !!gl.getExtension('EXT_color_buffer_float') || !!gl.getExtension('EXT_color_buffer_half_float');
    gl.getExtension('OES_texture_float_linear');
    progF = program(SH.vert, SH.feedback); progP = program(SH.pvert, SH.pfrag); progC = program(SH.vert, SH.composite);
    gl.bindVertexArray(gl.createVertexArray());
    audioTex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, audioTex);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.R8, Audio.TEXW, 2, 0, gl.RED, gl.UNSIGNED_BYTE, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    W = H = 0; targets = [];             // resize() makes fresh render targets on the next frame
  }
  try { initGL(); } catch (e) { console.error(e); noGL('shader compile failed'); return; }

  function makeTarget() {
    const tex = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, tex);
    if (floatOK) gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA16F, W, H, 0, gl.RGBA, gl.HALF_FLOAT, null);
    else gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, W, H, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.MIRRORED_REPEAT); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.MIRRORED_REPEAT);
    const fbo = gl.createFramebuffer(); gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
    gl.clearColor(0, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT); gl.generateMipmap(gl.TEXTURE_2D);
    return { tex, fbo };
  }
  function resize() {
    const dpr = window.devicePixelRatio || 1;
    let w = Math.round(canvas.clientWidth * dpr), h = Math.round(canvas.clientHeight * dpr);
    const cap = SOFT ? [480, 720, 1080][quality] : [720, 1080, 1e5][quality]; // internal render height cap (lower on the CPU renderer)
    if (h > cap) { w = Math.round(w * cap / h); h = cap; }
    w = Math.max(2, w); h = Math.max(2, h);
    if (w === W && h === H) return;
    W = canvas.width = w; H = canvas.height = h;
    for (const t of targets) { gl.deleteTexture(t.tex); gl.deleteFramebuffer(t.fbo); }
    targets = [makeTarget(), makeTarget()];
  }

  // ---------- settings (remembered between launches) ----------
  const SETTINGS_KEY = 'prismatica.settings';
  const S = Object.assign({ sceneNames: true, auto: true, cycle: 30, quality: SOFT ? 0 : 1 }, (() => { try { return JSON.parse(localStorage.getItem(SETTINGS_KEY)) || {}; } catch (e) { return {}; } })());
  const saveSettings = () => { try { localStorage.setItem(SETTINGS_KEY, JSON.stringify(S)); } catch (e) {} };
  if (!SHOT) quality = S.quality;

  // ---------- scenes ----------
  const KEYS = Object.keys(BASE);
  let cur = Object.assign({}, SCENES[0].p), from = Object.assign({}, cur), sceneIdx = 0, fade = 1, auto = S.auto, sceneAge = 0;
  function setScene(i, instant) {
    sceneIdx = ((i % SCENES.length) + SCENES.length) % SCENES.length;
    from = Object.assign({}, cur); fade = instant ? 1 : 0; sceneAge = 0;
    if (instant) cur = Object.assign({}, SCENES[sceneIdx].p);
    $('scene').textContent = SCENES[sceneIdx].name; if (S.sceneNames) toast(SCENES[sceneIdx].name);
  }
  function stepScene(dt, L) {
    sceneAge += dt;
    if (auto && sceneAge > S.cycle * .8 && (L.beat > .9 || sceneAge > S.cycle)) setScene(sceneIdx + 1);   // switch on a beat near the interval
    if (fade < 1) {
      fade = Math.min(1, fade + dt / 3.5);
      const k = fade * fade * (3 - 2 * fade), to = SCENES[sceneIdx].p;
      for (const key of KEYS) cur[key] = from[key] + (to[key] - from[key]) * k;
      cur.kalN = (to.kal > 0 ? to : from).kalN; cur.flowerK = (k > .5 ? to : from).flowerK; // integers don't blend
    }
  }

  // ---------- frame ----------
  let time = 0, flip = 0;
  function setCommon(pr, L, dt) {
    const u = pr.u;
    gl.uniform2f(u.uRes, W, H); gl.uniform1f(u.uTime, time); gl.uniform1f(u.uAT, L.at);
    gl.uniform1f(u.uBass, L.bass); gl.uniform1f(u.uMid, L.mid); gl.uniform1f(u.uTreb, L.treb);
    gl.uniform1f(u.uBeat, L.beat); gl.uniform1f(u.uBeatAge, L.beatAge); gl.uniform1f(u.uDt60, Math.min(3, dt * 60));
    for (const key of KEYS) if (u[key]) gl.uniform1f(u[key], cur[key]);
    if (u.palShift) gl.uniform1f(u.palShift, cur.palShift + time * .011);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, audioTex); if (u.uAudio) gl.uniform1i(u.uAudio, 1);
  }
  function frame(dt, offline) {
    if (gl.isContextLost()) return;
    resize(); time += dt;
    const L = Audio.update(dt, time, offline);
    stepScene(dt, L);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, audioTex);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, Audio.TEXW, 2, gl.RED, gl.UNSIGNED_BYTE, Audio.tex);
    const src = targets[flip], dst = targets[1 - flip]; flip = 1 - flip;

    gl.bindFramebuffer(gl.FRAMEBUFFER, dst.fbo); gl.viewport(0, 0, W, H); gl.disable(gl.BLEND);
    gl.useProgram(progF.p); setCommon(progF, L, dt);
    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, src.tex); gl.uniform1i(progF.u.uPrev, 0);
    gl.drawArrays(gl.TRIANGLES, 0, 3);

    if (cur.pStar + cur.pPhyl > .01) {
      const N = 7000;
      gl.enable(gl.BLEND); gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_COLOR);
      gl.useProgram(progP.p); setCommon(progP, L, dt); gl.uniform1f(progP.u.uN, N);
      gl.drawArrays(gl.POINTS, 0, N); gl.disable(gl.BLEND);
    }

    gl.bindFramebuffer(gl.FRAMEBUFFER, null); gl.viewport(0, 0, W, H);
    gl.useProgram(progC.p); setCommon(progC, L, dt);
    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, dst.tex); gl.generateMipmap(gl.TEXTURE_2D);
    gl.uniform1i(progC.u.uTex, 0);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  // ---------- UI ----------
  let toastTimer; function toast(msg) { const t = $('toast'); t.textContent = msg; t.classList.add('show'); clearTimeout(toastTimer); toastTimer = setTimeout(() => t.classList.remove('show'), 2600); }
  const el = Audio.el; let playlist = [], track = -1;
  const fmt = (s) => isFinite(s) ? Math.floor(s / 60) + ':' + String(Math.floor(s % 60)).padStart(2, '0') : '0:00';
  function markSource() { document.querySelectorAll('#bar [data-src]').forEach((b) => b.classList.toggle('on', b.dataset.src === Audio.mode)); $('welcome').hidden = true; }
  function playTrack(i) {
    if (!playlist.length) return;
    track = ((i % playlist.length) + playlist.length) % playlist.length;
    if (Audio.mode !== 'files') Audio.useElement();
    el.src = playlist[track].url; el.play().catch(() => {});                            // failures are handled by the 'error' event
    $('title').textContent = playlist[track].name; markSource();
    loadLyrics();
  }
  let badRun = 0;                                                                      // unplayable files in a row
  el.addEventListener('playing', () => { badRun = 0; });
  el.addEventListener('error', () => {
    if (Audio.mode !== 'files' || !playlist[track] || !el.getAttribute('src')) return;
    Lyrics.clear(); $('lynone').hidden = true;
    badRun++;
    if (badRun >= playlist.length) { toast(playlist.length > 1 ? "None of these files can be played" : "Can't play " + playlist[track].name); badRun = 0; return; }
    toast("Can't play " + playlist[track].name + ', skipping');
    playTrack(track + 1);
  });
  // ---------- lyrics ----------
  const duration = () => new Promise((res) => { if (el.duration > 0 && isFinite(el.duration)) return res(el.duration); const done = () => res(isFinite(el.duration) ? el.duration : 0); el.addEventListener('loadedmetadata', done, { once: true }); setTimeout(done, 4000); });
  function loadLyrics() { $('lynone').hidden = true; if (track >= 0 && playlist[track]) Lyrics.load(playlist[track], duration); }
  Lyrics.onStatus = (s, src, wordTimed, info) => {
    info = info || {};
    $('lynone').hidden = s === 'found' || !Lyrics.enabled;
    if (!Lyrics.enabled) return;
    const what = info.searched ? ' for "' + info.searched + '"' : '';
    if (s === 'found' && info.approx) toast('Lyrics found, but for a different-length version: use [ and ] to fix timing');
    else if (s === 'none') { toast('No timed lyrics found' + what); $('lynoneText').textContent = 'No timed lyrics found'; }
    else if (s === 'unsynced') { toast('Only untimed lyrics exist' + what); $('lynoneText').textContent = 'Only untimed lyrics exist'; }
    else if (s === 'offline') { toast("Couldn't reach the lyrics service (lrclib.net)"); $('lynoneText').textContent = "Couldn't reach lrclib.net"; }
  };
  // ---- manual lyrics search
  function openSearch() {
    if (Audio.mode !== 'files' || !playlist[track]) return toast('Play a music file first');
    const q = Lyrics.query, panel = $('lysearch');
    $('lysq').value = q ? [q.artist, q.title].filter(Boolean).join(' ') : playlist[track].name;
    $('lysres').textContent = ''; $('lysmsg').textContent = 'Press Enter to search';
    panel.hidden = false; $('lysq').focus(); $('lysq').select();
    if ($('lysq').value.trim()) runSearch();
  }
  let searchSeq = 0;
  async function runSearch() {
    const api = window.prismatica; if (!api) return;
    const text = $('lysq').value.trim(); if (!text) return;
    const my = ++searchSeq; $('lysmsg').textContent = 'Searching…'; $('lysres').textContent = '';
    const res = await api.searchLyrics(text, el.duration || 0); if (my !== searchSeq) return;
    if (!Array.isArray(res)) { $('lysmsg').textContent = "Couldn't reach lrclib.net"; return; }
    if (!res.length) { $('lysmsg').textContent = 'Nothing found. Try just the song title, or "artist title".'; return; }
    $('lysmsg').textContent = 'Pick the right one (timed results first, closest length first):';
    for (const r of res) {
      const b = document.createElement('button'); b.disabled = !r.synced;
      const dd = el.duration && r.duration ? Math.round(r.duration - el.duration) : null;
      b.innerHTML = '<b></b><span></span>'; b.querySelector('b').textContent = r.artist + ' - ' + r.title;
      b.querySelector('span').textContent = [r.album, fmt(r.duration) + (dd ? ' (' + (dd > 0 ? '+' : '') + dd + 's)' : ''), r.synced ? 'timed' : 'untimed only'].filter(Boolean).join(' · ');
      b.onclick = async () => { $('lysmsg').textContent = 'Loading…'; if (await Lyrics.choose(r.id)) { closeSearch(); $('lynone').hidden = true; if (!Lyrics.enabled) setLyrics(true); toast('Lyrics: ' + r.artist + ' - ' + r.title); } else $('lysmsg').textContent = "Couldn't load those lyrics"; };
      $('lysres').appendChild(b);
    }
  }
  function closeSearch() { $('lysearch').hidden = true; searchSeq++; }
  $('lysq').addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); runSearch(); } else if (e.key === 'Escape') closeSearch(); e.stopPropagation(); });
  $('lysclose').onclick = closeSearch; $('lynoneBtn').onclick = openSearch;
  function nudgeLyrics(d) { if (!Lyrics.has) return; const o = Lyrics.nudge(d); toast('Lyrics timing ' + (o === 0 ? 'reset' : (o > 0 ? 'later by ' : 'earlier by ') + Math.abs(o).toFixed(1) + 's')); }
  function setLyrics(v) {
    Lyrics.setEnabled(v); $('lyr').classList.toggle('on', v); toast(v ? 'Lyrics on' : 'Lyrics off'); syncSettings();
    if (!v) { $('lynone').hidden = true; closeSearch(); }
    if (v && Audio.mode === 'files' && playlist[track] && playlist[track].lyricsPending) loadLyrics();
  }
  $('lyr').classList.toggle('on', Lyrics.enabled); $('lyr').onclick = () => setLyrics(!Lyrics.enabled);
  function addFiles(files) {
    const stem = (n) => n.replace(/\.[^.]+$/, '').toLowerCase();
    const lrcs = new Map([...files].filter((f) => /\.lrc$/i.test(f.name)).map((f) => [stem(f.name), f]));
    const list = [...files].filter((f) => !/\.lrc$/i.test(f.name) && (/^(audio|video)\//.test(f.type) || /\.(mp3|flac|wav|ogg|oga|opus|m4a|aac|webm|mp4)$/i.test(f.name)));
    if (!list.length && lrcs.size && Audio.mode === 'files' && playlist[track]) {   // a .lrc dropped on its own: use it for the song that's playing
      playlist[track].lrc = lrcs.get(stem(playlist[track].file.name)) || [...lrcs.values()][0]; if (!Lyrics.enabled) setLyrics(true); return loadLyrics();
    }
    if (!list.length) return toast('No playable audio files');
    list.sort((a, b) => (a.relPath || a.name).localeCompare(b.relPath || b.name, undefined, { numeric: true }));
    for (const it of playlist) URL.revokeObjectURL(it.url);
    badRun = 0;
    playlist = list.map((f) => ({ name: f.name.replace(/\.[^.]+$/, ''), url: URL.createObjectURL(f), file: f, lrc: lrcs.get(stem(f.name)) }));
    playTrack(0);
  }
  async function pick(src) {
    try {
      if (src === 'files') return $('file').click();
      Lyrics.clear(); $('lynone').hidden = true; closeSearch();
      if (src === 'system') { await Audio.useSystem(); $('title').textContent = 'System audio'; }
      if (src === 'mic') { await Audio.useMic(); $('title').textContent = 'Microphone'; }
      if (src === 'demo') { $('title').textContent = 'Rendering demo…'; await Audio.useDemo(); $('title').textContent = 'Demo track'; }
      markSource(); $('play').innerHTML = '&#10074;&#10074;';
    } catch (e) { toast(src === 'system' ? 'System audio capture was not allowed or is unavailable' : 'Could not start: ' + e.message); }
  }
  document.querySelectorAll('[data-src]').forEach((b) => b.addEventListener('click', () => pick(b.dataset.src)));
  $('file').addEventListener('change', (e) => { addFiles(e.target.files); e.target.value = ''; });
  el.addEventListener('ended', () => playTrack(track + 1));
  el.addEventListener('play', () => $('play').innerHTML = '&#10074;&#10074;'); el.addEventListener('pause', () => $('play').innerHTML = '&#9654;');
  el.addEventListener('timeupdate', () => { if (!seeking) $('seek').value = el.duration ? 1000 * el.currentTime / el.duration : 0; $('time').textContent = fmt(el.currentTime); });
  let seeking = false; $('seek').addEventListener('input', () => { seeking = true; }); $('seek').addEventListener('change', () => { if (el.duration) el.currentTime = el.duration * $('seek').value / 1000; seeking = false; });
  $('vol').addEventListener('input', () => Audio.setVolume($('vol').value / 100));
  function playPause() {
    if (Audio.mode === 'files') el.paused ? el.play() : el.pause();
    else if (Audio.mode !== 'none') $('play').innerHTML = Audio.toggleDemo() ? '&#10074;&#10074;' : '&#9654;';
    else pick('demo');
  }
  function setAuto(v, quiet) { auto = S.auto = v; saveSettings(); $('lock').textContent = auto ? 'Auto' : 'Locked'; $('lock').classList.toggle('on', !auto); if (!quiet) toast(auto ? 'Auto-cycle on' : 'Scene locked'); syncSettings(); }
  function setQuality(q, quiet) { quality = S.quality = q; saveSettings(); if (!quiet) toast('Quality: ' + QUALITY_NAMES()[q]); syncSettings(); }
  const QUALITY_NAMES = () => SOFT ? ['480p', '720p', '1080p'] : ['720p', '1080p', 'Native'];
  function setOnline(v) { Lyrics.setOnline(v); toast('Online lyrics lookup ' + (v ? 'on' : 'off')); if (v && Lyrics.enabled && !Lyrics.has && Audio.mode === 'files') loadLyrics(); syncSettings(); }
  // ---- settings panel
  function syncSettings() {
    $('setSceneNames').checked = S.sceneNames; $('setAuto').checked = auto; $('setCycle').value = String(S.cycle); $('setCycle').disabled = !auto;
    $('setLyrics').checked = Lyrics.enabled; $('setOnline').checked = Lyrics.online; $('setOnline').disabled = !Lyrics.enabled;
    QUALITY_NAMES().forEach((n, i) => { $('setQuality').options[i].textContent = n + (i === (SOFT ? 0 : 1) ? ' (default)' : ''); });
    $('setQuality').value = String(quality);
  }
  function toggleSettings(show) {
    const p = $('settingsPanel'); show = show === undefined ? p.hidden : show;
    p.hidden = !show; $('settingsBtn').classList.toggle('on', show); if (show) { $('keys').hidden = true; syncSettings(); }
  }
  $('settingsBtn').onclick = (e) => { e.stopPropagation(); toggleSettings(); };
  $('settingsPanel').addEventListener('mousedown', (e) => e.stopPropagation());
  window.addEventListener('mousedown', (e) => { if (!$('settingsPanel').hidden && e.target !== $('settingsBtn')) toggleSettings(false); });
  $('setSceneNames').onchange = (e) => { S.sceneNames = e.target.checked; saveSettings(); };
  $('setAuto').onchange = (e) => setAuto(e.target.checked, true);
  $('setCycle').onchange = (e) => { S.cycle = +e.target.value; saveSettings(); };
  $('setLyrics').onchange = (e) => setLyrics(e.target.checked);
  $('setOnline').onchange = (e) => setOnline(e.target.checked);
  $('setQuality').onchange = (e) => setQuality(+e.target.value, true);
  $('lock').textContent = auto ? 'Auto' : 'Locked'; $('lock').classList.toggle('on', !auto);
  function fullscreen() { document.fullscreenElement ? document.exitFullscreen() : document.documentElement.requestFullscreen(); }
  $('play').onclick = playPause; $('prevT').onclick = () => playTrack(track - 1); $('nextT').onclick = () => playTrack(track + 1);
  $('prevS').onclick = () => setScene(sceneIdx - 1); $('nextS').onclick = () => setScene(sceneIdx + 1);
  $('lock').onclick = () => setAuto(!auto); $('full').onclick = fullscreen; $('help').onclick = () => { $('keys').hidden = !$('keys').hidden; if (!$('keys').hidden) toggleSettings(false); };
  canvas.addEventListener('dblclick', fullscreen);
  window.addEventListener('keydown', (e) => {
    if (e.target.id === 'lysq') return;
    if (e.target.tagName === 'SELECT') return;
    if (e.target.tagName === 'INPUT' && e.key !== ' ' && e.target.type !== 'checkbox') return;
    if (e.ctrlKey || e.metaKey || e.altKey) return;                                  // leave Cmd+Q, Ctrl+H etc. to the system
    const k = e.key.toLowerCase();
    if (k === 's' || k === ',' || k === 'o') e.preventDefault();                       // these move focus; don't type the key into it
    if (k === ' ') { e.preventDefault(); playPause(); }
    else if (k === 'arrowright') setScene(sceneIdx + 1); else if (k === 'arrowleft') setScene(sceneIdx - 1);
    else if (k >= '0' && k <= '9') setScene(k === '0' ? 9 : +k - 1);
    else if (k === 'r') setScene(sceneIdx + 1 + Math.floor(Math.random() * (SCENES.length - 1)));
    else if (k === 'l') setAuto(!auto); else if (k === 'n') playTrack(track + 1); else if (k === 'p') playTrack(track - 1);
    else if (k === 'o') pick('files'); else if (k === 'f' || k === 'f11') fullscreen();
    else if (k === 'q') setQuality((quality + 1) % 3);
    else if (k === 'y' && e.shiftKey) setOnline(!Lyrics.online);
    else if (k === 'y') setLyrics(!Lyrics.enabled);
    else if (k === 's') openSearch();
    else if (k === '[') nudgeLyrics(-0.2); else if (k === ']') nudgeLyrics(0.2); else if (k === '\\') nudgeLyrics(-Lyrics.offset);
    else if (k === 'h') document.body.classList.toggle('hideui'); else if (k === '?' || k === '/') $('help').click();
    else if (k === ',') toggleSettings();
    else if (k === 'escape') { $('keys').hidden = true; closeSearch(); toggleSettings(false); }
  });
  // keep --bar equal to the control bar's real height (it wraps to two rows in narrow windows)
  const barH = () => document.documentElement.style.setProperty('--bar', $('bar').offsetHeight + 'px');
  if (window.ResizeObserver) new ResizeObserver(barH).observe($('bar')); barH();
  let idleTimer; function wake() { document.body.classList.remove('idle'); clearTimeout(idleTimer); idleTimer = setTimeout(() => { if (!$('bar').matches(':hover') && $('welcome').hidden) document.body.classList.add('idle'); }, 3000); }
  window.addEventListener('mousemove', wake); window.addEventListener('mousedown', wake); wake();
  let dragDepth = 0;
  window.addEventListener('dragenter', (e) => { e.preventDefault(); dragDepth++; document.body.classList.add('drag'); });
  window.addEventListener('dragover', (e) => { e.preventDefault(); });
  window.addEventListener('dragleave', () => { if (--dragDepth <= 0) { dragDepth = 0; document.body.classList.remove('drag'); } });
  window.addEventListener('drop', (e) => { e.preventDefault(); dragDepth = 0; document.body.classList.remove('drag'); droppedFiles(e.dataTransfer).then(addFiles); });
  // Dropped folders arrive as one empty "file"; read them through the entries API instead (up to 2000 files).
  async function droppedFiles(dt) {
    const entries = [...(dt.items || [])].map((it) => it.webkitGetAsEntry && it.webkitGetAsEntry()).filter(Boolean);
    if (!entries.some((en) => en.isDirectory)) return [...dt.files];
    const out = [];
    const walk = async (en) => {
      if (out.length >= 2000) return;
      if (en.isFile) { try { const f = await new Promise((res, rej) => en.file(res, rej)); f.relPath = en.fullPath; out.push(f); } catch (e) {} return; }
      const rd = en.createReader();
      for (;;) { const batch = await new Promise((res) => rd.readEntries(res, () => res([]))); if (!batch.length) break; for (const c of batch) await walk(c); }
    };
    for (const en of entries) await walk(en);
    return out;
  }

  setScene(0, true);
  if (SOFT) setTimeout(() => toast('Software rendering (GPU unsupported)'), 600);
  if (SOFT && window.prismatica && window.prismatica.tryGPU) { $('softRow').hidden = false; $('tryGPU').onclick = () => window.prismatica.tryGPU(); }

  // ---------- main loop / screenshot mode ----------
  if (!SHOT) {
    let last = performance.now();
    const loop = (now) => { const dt = Math.max(0, Math.min(.1, (now - last) / 1000)); last = now; try { frame(dt); if (Audio.mode === 'files') Lyrics.update(el.currentTime); } catch (e) { console.error(e); } requestAnimationFrame(loop); };
    requestAnimationFrame(loop);
  } else {
    let buf, simT = 0;
    window.__ready = Audio.renderDemo(44100).then((b) => { buf = b; return true; });
    window.__shot = async (scene, secs, ui) => {
      setScene(scene, true);
      document.body.classList.remove('idle'); document.body.classList.toggle('hideui', !ui || ui === 3);
      if (ui >= 3) {   // lyrics overlay demo (made-up sample lines; the third line carries real per-word times)
        Lyrics.setEnabled(true); $('lyr').classList.add('on');
        Lyrics.build(Lyrics.parseLRC('[00:00.50]Colours folding into sound\n[00:03.20]Every wave a little crown\n[00:06.00]<00:06.00>Hold <00:06.45>the <00:06.70>light <00:07.30>until <00:07.90>it <00:08.10>bends\n[00:09.50]Round the corner, round again\n[00:12.50]Nothing here is standing still\n[00:15.50]Watch the ceiling start to spill'));
        if (ui === 4) { ui = 1; }
      }
      $('toast').style.display = 'none'; $('welcome').hidden = ui !== 2;
      if (ui === 1) { $('title').textContent = 'Demo track'; $('time').textContent = fmt(simT % 32); $('seek').value = 1000 * (simT % 32) / 32; $('play').innerHTML = '&#10074;&#10074;'; document.querySelector('#bar [data-src=demo]').classList.add('on'); }
      for (let i = 0; i < secs * 60; i++) { simT += 1 / 60; sceneAge = 0; frame(1 / 60, { buf, t: simT }); if (i % 30 === 0) await new Promise((r) => setTimeout(r)); }
      if (Lyrics.has) { Lyrics.update(3.3); await new Promise((r) => setTimeout(r, 700)); Lyrics.update(7.62); }
      await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)));
      return SCENES[scene].name;
    };
  }
})();
