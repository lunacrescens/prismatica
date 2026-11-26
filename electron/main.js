const { app, BrowserWindow, session, desktopCapturer, Menu, ipcMain, net } = require('electron');
const path = require('path');
const fs = require('fs');

const SHOT_DIR = process.env.PRISMATICA_SHOTS; // internal: render screenshots and exit
app.commandLine.appendSwitch('autoplay-policy', 'no-user-gesture-required');

// ---- GPU fallback: if WebGL2 can't start on the GPU (e.g. Macs Chromium no longer supports), relaunch with
// SwiftShader software rendering and remember that choice. Delete the marker file to try the GPU again.
const SOFT_MARKER = path.join(app.getPath('userData'), 'software-rendering');
const FORCE_GPU = process.argv.includes('--force-gpu');
if (FORCE_GPU) { try { fs.unlinkSync(SOFT_MARKER); } catch (e) {} }
const SOFTWARE = !SHOT_DIR && !FORCE_GPU && (process.argv.includes('--software-rendering') || fs.existsSync(SOFT_MARKER));
if (SOFTWARE) {
  app.commandLine.appendSwitch('use-angle', 'swiftshader');
  app.commandLine.appendSwitch('use-gl', 'angle');
  app.commandLine.appendSwitch('enable-unsafe-swiftshader');
  app.commandLine.appendSwitch('ignore-gpu-blocklist');
}
let fellBack = false;
function fallBackToSoftware(reason) {
  if (SOFTWARE || SHOT_DIR || fellBack) return;
  fellBack = true;
  console.log('GPU path failed (' + reason + '), relaunching with software rendering');
  try { fs.mkdirSync(path.dirname(SOFT_MARKER), { recursive: true }); fs.writeFileSync(SOFT_MARKER, reason + '\n'); } catch (e) {}
  app.relaunch({ args: process.argv.slice(1).filter((a) => a !== '--force-gpu').concat(['--software-rendering']) });
  app.exit(0);
}
app.on('child-process-gone', (e, d) => { if (d.type === 'GPU' && d.reason !== 'clean-exit') fallBackToSoftware('gpu-process-' + d.reason); });
if (SHOT_DIR) {
  app.commandLine.appendSwitch('use-angle', 'swiftshader');
  app.commandLine.appendSwitch('enable-unsafe-swiftshader');
  app.commandLine.appendSwitch('ignore-gpu-blocklist');
  app.commandLine.appendSwitch('no-sandbox');
  app.commandLine.appendSwitch('in-process-gpu');
}

function createWindow() {
  const win = new BrowserWindow({
    width: +process.env.PRISMATICA_W || 1280, height: +process.env.PRISMATICA_H || 720, useContentSize: true,
    minWidth: 640, minHeight: 360,
    backgroundColor: '#000000',
    title: 'Prismatica',
    autoHideMenuBar: true,
    show: !SHOT_DIR,
    webPreferences: { contextIsolation: true, nodeIntegration: false, backgroundThrottling: false, offscreen: false, preload: path.join(__dirname, 'preload.js') },
  });
  const query = SHOT_DIR ? { shot: '1' } : SOFTWARE ? { soft: '1' } : {};
  win.loadFile(path.join(__dirname, 'index.html'), { query });
  // the page sets #nogl when it can't get a working WebGL2 context
  win.webContents.on('did-navigate-in-page', (e, url) => { if (url.endsWith('#nogl')) fallBackToSoftware('no-webgl2'); });
  win.webContents.on('render-process-gone', (e, d) => { if (d.reason !== 'clean-exit') fallBackToSoftware('renderer-' + d.reason); });
  if (SHOT_DIR) runShots(win).catch((e) => { console.error(e); app.exit(1); });
}

async function runShots(win) {
  const wc = win.webContents;
  wc.on('console-message', (e) => console.log('[page]', e.message));
  await new Promise((r) => wc.once('did-finish-load', r));
  win.show();
  await wc.executeJavaScript('window.__ready');
  const plan = JSON.parse(process.env.PRISMATICA_PLAN || '[[0,5]]');
  fs.mkdirSync(SHOT_DIR, { recursive: true });
  for (const [scene, secs, ui] of plan) {
    const name = await wc.executeJavaScript(`window.__shot(${scene},${secs},${ui || 0})`);
    await new Promise((r) => setTimeout(r, 400));
    const img = await wc.capturePage();
    const file = path.join(SHOT_DIR, `${String(scene + 1).padStart(2, '0')}-${name.toLowerCase().replace(/\W+/g, '-')}${ui ? (ui === 2 ? '-welcome' : '-controls') : ''}.png`);
    fs.writeFileSync(file, img.toPNG());
    console.log('wrote', file);
  }
  app.exit(0);
}

app.whenReady().then(() => {
  // Microphone + system-audio capture permissions (no network content is ever loaded).
  session.defaultSession.setPermissionRequestHandler((wc, perm, cb) => cb(['media', 'fullscreen', 'display-capture'].includes(perm)));
  session.defaultSession.setPermissionCheckHandler(() => true);
  session.defaultSession.setDisplayMediaRequestHandler((request, callback) => {
    desktopCapturer.getSources({ types: ['screen'] }).then((sources) => {
      callback({ video: sources[0], audio: 'loopback' });
    }).catch(() => callback({}));
  });

  if (process.platform === 'darwin') {
    Menu.setApplicationMenu(Menu.buildFromTemplate([
      { role: 'appMenu' },
      { label: 'View', submenu: [{ role: 'togglefullscreen' }, { role: 'toggleDevTools' }] },
      { role: 'windowMenu' },
    ]));
  } else {
    Menu.setApplicationMenu(null);
  }
  require('./lyrics-main').register(ipcMain, app, net);
  createWindow();
  app.on('activate', () => { if (BrowserWindow.getAllWindows().length === 0) createWindow(); });
});
app.on('window-all-closed', () => app.quit());
