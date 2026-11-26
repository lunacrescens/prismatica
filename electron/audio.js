// Audio engine: sources (files, system loopback, microphone, demo loop) -> analyser -> levels + texture data.
const Audio = (() => {
  const FFT = 2048, TEXW = 512;
  let ctx, analyser, gain, srcNode = null, stream = null, demoBuf = null, demoSrc = null;
  const el = document.getElementById('audio');
  let elNode = null;
  const freq = new Uint8Array(FFT / 2), wave = new Uint8Array(FFT);
  const tex = new Uint8Array(TEXW * 2);           // row 0: spectrum (log freq), row 1: waveform
  const L = { bass: 0, mid: 0, treb: 0, vol: 0, beat: 0, beatAge: 9, at: 0, silent: 9 };
  const peak = { bass: .3, mid: .3, treb: .3 };
  const hist = new Float32Array(48); let hi = 0, cooldown = 0;
  let mode = 'none', sampleRate = 44100;

  function init() {
    if (ctx) return;
    ctx = new AudioContext({ latencyHint: 'playback' });
    sampleRate = ctx.sampleRate;
    analyser = ctx.createAnalyser(); analyser.fftSize = FFT; analyser.smoothingTimeConstant = .72;
    gain = ctx.createGain(); gain.gain.value = .8;
    gain.connect(ctx.destination);
  }
  function disconnect() {
    if (srcNode && srcNode !== elNode) { try { srcNode.disconnect(); } catch (e) {} }
    if (elNode) { try { elNode.disconnect(); } catch (e) {} }
    if (stream) { stream.getTracks().forEach((t) => t.stop()); stream = null; }
    if (demoSrc) { try { demoSrc.stop(); } catch (e) {} demoSrc = null; }
    el.pause(); srcNode = null;
    try { analyser.disconnect(); } catch (e) {}
  }
  function useElement() {
    init(); disconnect(); ctx.resume();
    if (!elNode) elNode = ctx.createMediaElementSource(el);
    elNode.connect(analyser); analyser.connect(gain); srcNode = elNode; mode = 'files';
  }
  function useStream(s, m) {
    init(); disconnect(); ctx.resume();
    stream = s; srcNode = ctx.createMediaStreamSource(s); srcNode.connect(analyser); mode = m; // not routed to speakers
  }
  async function useSystem() {
    // No voice processing: on Linux, auto gain would turn the system-audio input down to almost nothing
    const s = await navigator.mediaDevices.getDisplayMedia({ video: true, audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false } });
    s.getVideoTracks().forEach((t) => t.stop());
    if (!s.getAudioTracks().length) throw new Error('No system audio track was provided');
    useStream(new MediaStream(s.getAudioTracks()), 'system');
  }
  async function useMic() {
    const s = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false } });
    useStream(s, 'mic');
  }
  async function useDemo() {
    init(); disconnect(); ctx.resume();
    if (!demoBuf) demoBuf = await renderDemo(ctx.sampleRate);
    demoSrc = ctx.createBufferSource(); demoSrc.buffer = demoBuf; demoSrc.loop = true;
    demoSrc.connect(analyser); analyser.connect(gain); demoSrc.start(); srcNode = demoSrc; mode = 'demo';
  }

  // ---- built-in demo loop, synthesized sample-by-sample (120 BPM, 16 bars, loops seamlessly) ----
  async function renderDemo(sr) {
    const bpm = 120, beat = 60 / bpm, bars = 16, six = beat / 4, N = Math.round(bars * 4 * beat * sr);
    const Lc = new Float32Array(N), Rc = new Float32Array(N), arpL = new Float32Array(N), arpR = new Float32Array(N), duck = new Float32Array(N).fill(1);
    let seed = 1234567; const rnd = () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 2147483648 - 1; };
    const hz = (m) => 440 * Math.pow(2, (m - 69) / 12), TAU = 2 * Math.PI;
    const chords = [[57, 60, 64, 67], [53, 57, 60, 64], [48, 52, 55, 59], [55, 59, 62, 65]]; // Am7 Fmaj7 Cmaj7 G7
    const saw = (ph) => 2 * (ph - Math.floor(ph + .5));
    for (let bar = 0; bar < bars; bar++) {
      const ch = chords[(bar >> 1) % 4], t0 = bar * 4 * beat, full = bar >= 4, breakdown = bar === 11;
      for (let b = 0; b < 4; b++) {
        const i0 = Math.round((t0 + b * beat) * sr);
        if (!breakdown && bar >= 2) { // kick + sidechain duck
          let ph = 0; const len = Math.round(.36 * sr);
          for (let i = 0; i < len && i0 + i < N; i++) { const t = i / sr; ph += (42 + 120 * Math.exp(-t * 32)) / sr; const v = Math.sin(TAU * ph) * Math.exp(-t * 8) * .95; Lc[i0 + i] += v; Rc[i0 + i] += v; }
          for (let i = 0; i < beat * sr && i0 + i < N; i++) duck[i0 + i] = 1 - .65 * Math.exp(-i / sr * 9);
        }
        if (full && !breakdown && (b & 1)) { // snare
          let lp = 0; const len = Math.round(.22 * sr);
          for (let i = 0; i < len && i0 + i < N; i++) { const t = i / sr, n = rnd(); lp += .35 * (n - lp); const v = ((n - lp) * .5 + lp * .5) * Math.exp(-t * 20) * .5 + Math.sin(TAU * 190 * t) * Math.exp(-t * 30) * .3; Lc[i0 + i] += v; Rc[i0 + i] += v; }
        }
        for (let s = 0; s < 4; s++) {
          const j0 = i0 + Math.round(s * six * sr), step = b * 4 + s;
          if (bar >= 1) { // hats
            let prev = 0; const open = s === 2, vol = (open ? .2 : .08) * (full ? 1 : .6), len = Math.round((open ? .1 : .035) * sr), pan = step % 2 ? .35 : -.35;
            for (let i = 0; i < len && j0 + i < N; i++) { const n = rnd(), v = (n - prev) * .7 * Math.exp(-i / sr * (open ? 35 : 110)) * vol; prev = n; Lc[j0 + i] += v * (1 - pan); Rc[j0 + i] += v * (1 + pan); }
          }
          if (bar >= 2 && s !== 0) { // offbeat bass
            const f = hz(ch[0] - 24 + (step % 8 === 7 ? 12 : 0)), len = Math.round(.2 * sr), cut = 700 + 500 * Math.sin(bar); let ph = 0, y1 = 0, y2 = 0;
            for (let i = 0; i < len && j0 + i < N; i++) { const t = i / sr; ph += f / sr; const k = Math.min(.9, TAU * (120 + cut * Math.exp(-t * 16)) / sr); y1 += k * (saw(ph) - y1); y2 += k * (y1 - y2); const v = y2 * Math.min(1, t * 400) * Math.exp(-t * 12) * .55; Lc[j0 + i] += v; Rc[j0 + i] += v; }
          }
          { // arp
            const note = ch[(step * 3 + (bar & 1)) % 4] + 12 * (((step >> 1) + bar) % 3) + 12, f = hz(note), len = Math.round(.24 * sr), sq = step % 2, vol = breakdown ? .15 : .085;
            const cut = 2400 + 1800 * Math.sin(bar * .8 + step * .2), pan = .5 * Math.sin(step * 1.3); let ph = 0, y = 0;
            for (let i = 0; i < len && j0 + i < N; i++) { const t = i / sr; ph += f / sr; const fr = ph - Math.floor(ph), raw = sq ? (fr < .5 ? 1 : -1) : 4 * Math.abs(fr - .5) - 1; const k = Math.min(.9, TAU * (400 + cut * Math.exp(-t * 9)) / sr); y += k * (raw - y); const v = y * Math.min(1, t * 300) * Math.exp(-t * 11) * vol; arpL[j0 + i] += v * (1 - pan); arpR[j0 + i] += v * (1 + pan); }
          }
        }
      }
      // pad: two detuned saws per chord note through a swept two-pole lowpass
      const pi0 = Math.round(t0 * sr), plen = Math.round(4 * beat * sr);
      ch.forEach((n, ni) => [-9, 9].forEach((cents, di) => {
        const f = hz(n) * Math.pow(2, cents / 1200), pan = (ni - 1.5) * .25 + (di - .5) * .3; let ph = ni * .37 + di * .11, y1 = 0, y2 = 0;
        for (let i = 0; i < plen && pi0 + i < N; i++) {
          const t = i / sr, x = t / (4 * beat); ph += f / sr;
          const cutHz = x < .5 ? 400 + ((breakdown ? 3500 : 1400) - 400) * x * 2 : (breakdown ? 3500 : 1400) + (500 - (breakdown ? 3500 : 1400)) * (x - .5) * 2;
          const k = Math.min(.9, TAU * cutHz / sr); y1 += k * (saw(ph) - y1); y2 += k * (y1 - y2);
          const env = Math.min(1, t / .3, (4 * beat - t) / .2), v = y2 * env * .05 * duck[pi0 + i];
          Lc[pi0 + i] += v * (1 - pan); Rc[pi0 + i] += v * (1 + pan);
        }
      }));
    }
    // ping-pong delay on the arp (wrapped, so the loop point is seamless), then soft-clip
    const D = Math.round(beat * .75 * sr);
    for (let pass = 0; pass < 2; pass++) for (let i = 0; i < N; i++) { const j = (i + D) % N; if (pass === 0 && j < i) continue; if (pass === 1 && j > i) continue; const l = arpL[i], r = arpR[i]; arpL[j] += r * .45; arpR[j] += l * .45; }
    for (let i = 0; i < N; i++) { Lc[i] = Math.tanh((Lc[i] + arpL[i]) * 1.15) * .85; Rc[i] = Math.tanh((Rc[i] + arpR[i]) * 1.15) * .85; }
    const buf = new AudioBuffer({ length: N, sampleRate: sr, numberOfChannels: 2 });
    buf.copyToChannel(Lc, 0); buf.copyToChannel(Rc, 1);
    return buf;
  }

  // ---- offline analysis (used only for deterministic screenshot rendering) ----
  const re = new Float32Array(FFT), im = new Float32Array(FFT), smooth = new Float32Array(FFT / 2);
  function analyseBufferAt(buf, t) {
    const d = buf.getChannelData(0), d2 = buf.getChannelData(1), n = d.length; let s0 = Math.floor(t * buf.sampleRate) % n;
    for (let i = 0; i < FFT; i++) {
      const v = (d[(s0 + i) % n] + d2[(s0 + i) % n]) * .5;
      wave[i] = Math.max(0, Math.min(255, 128 + v * 128));
      const w = .42 - .5 * Math.cos(2 * Math.PI * i / FFT) + .08 * Math.cos(4 * Math.PI * i / FFT);
      re[i] = v * w; im[i] = 0;
    }
    for (let i = 1, j = 0; i < FFT; i++) { let bit = FFT >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { const tr = re[i]; re[i] = re[j]; re[j] = tr; } }
    for (let len = 2; len <= FFT; len <<= 1) {
      const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
      for (let i = 0; i < FFT; i += len) { let cr = 1, ci = 0;
        for (let k = 0; k < len / 2; k++) { const a = i + k, b = a + len / 2;
          const xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
          re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
          const nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr; } }
    }
    for (let i = 0; i < FFT / 2; i++) {
      const mag = Math.hypot(re[i], im[i]) / FFT;
      smooth[i] = .72 * smooth[i] + .28 * mag;
      const db = 20 * Math.log10(smooth[i] + 1e-12);
      freq[i] = Math.max(0, Math.min(255, 255 * (db + 100) / 70));
    }
  }

  function bandAvg(lo, hi) { // Hz range -> 0..1
    const b0 = Math.max(1, Math.floor(lo * FFT / sampleRate)), b1 = Math.min(FFT / 2 - 1, Math.ceil(hi * FFT / sampleRate));
    let s = 0; for (let i = b0; i <= b1; i++) s += freq[i]; return s / ((b1 - b0 + 1) * 255);
  }
  const ease = (cur, target, dt, up, down) => cur + (target - cur) * (1 - Math.exp(-dt * (target > cur ? up : down)));

  function update(dt, time, offline) {
    if (offline) { sampleRate = offline.buf.sampleRate; analyseBufferAt(offline.buf, offline.t); }
    else if (analyser && srcNode) { analyser.getByteFrequencyData(freq); analyser.getByteTimeDomainData(wave); }
    else { freq.fill(0); wave.fill(128); }
    let dev = 0; for (let i = 0; i < FFT; i += 8) dev += Math.abs(wave[i] - 128);
    L.silent = dev < 40 ? L.silent + dt : 0;
    if (L.silent > 1.5) { // nothing playing: keep the picture gently alive
      for (let i = 0; i < FFT / 2; i++) freq[i] = Math.max(0, 150 - i * .35 + 50 * Math.sin(time * 1.3 + i * .05) + 40 * Math.sin(time * .7 - i * .013));
      for (let i = 0; i < FFT; i++) wave[i] = 128 + 30 * Math.sin(i * .012 + time * 2) * Math.sin(i * .0031 - time) + 14 * Math.sin(i * .05 - time * 3);
    }
    const raw = { bass: bandAvg(30, 140), mid: bandAvg(200, 2000), treb: bandAvg(3000, 12000) };
    for (const k in raw) {
      peak[k] = Math.max(peak[k] * Math.pow(.9995, dt * 60), raw[k], .12);
      const v = Math.pow(Math.min(1, raw[k] / peak[k]), 2.2);
      L[k] = ease(L[k], v, dt, 30, k === 'bass' ? 7 : 5);
    }
    if (L.silent > 1.5) { L.bass = .3 + .25 * Math.sin(time * 1.9); L.mid = .35 + .25 * Math.sin(time * .83 + 1); L.treb = .3 + .2 * Math.sin(time * 1.31 + 2); }
    L.vol = (L.bass + L.mid + L.treb) / 3;
    // beat detection on bass energy against its recent average
    let avg = 0; for (let i = 0; i < hist.length; i++) avg += hist[i]; avg /= hist.length;
    hist[hi++ % hist.length] = raw.bass; cooldown -= dt; L.beatAge += dt;
    if (L.silent > 1.5) { if (L.beatAge > 1.2) { L.beat = .6; L.beatAge = 0; } }
    else if (raw.bass > avg * 1.12 + .015 && raw.bass > peak.bass * .45 && cooldown <= 0) { L.beat = 1; L.beatAge = 0; cooldown = .22; }
    L.beat *= Math.exp(-dt * 6);
    L.at += dt * (.35 + 1.6 * L.vol + .8 * L.beat);
    // texture rows
    const nyq = sampleRate / 2;
    for (let x = 0; x < TEXW; x++) {
      const f = 30 * Math.pow(16000 / 30, x / (TEXW - 1)), pos = Math.min(FFT / 2 - 2, f / nyq * (FFT / 2)), i = pos | 0, fr = pos - i;
      const v = (freq[i] * (1 - fr) + freq[i + 1] * fr) / 255;
      tex[x] = 255 * Math.min(1, Math.pow(v, 1.6) * (1 + 1.2 * x / TEXW));
      tex[TEXW + x] = wave[x * 2];
    }
    return L;
  }
  return { el, tex, TEXW, L, update, useElement, useSystem, useMic, useDemo, renderDemo,
    setVolume: (v) => { init(); gain.gain.value = v; }, get mode() { return mode; }, get ctx() { return ctx; },
    toggleDemo() { if (ctx.state === 'running') ctx.suspend(); else ctx.resume(); return ctx.state !== 'running'; } };
})();
