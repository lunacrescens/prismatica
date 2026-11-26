// Build Prismatica's Electron app for Windows and macOS, zipped with the names the website links to.
//   node scripts/package.js                      all targets
//   node scripts/package.js win32-x64            just Windows
//   node scripts/package.js darwin-arm64 darwin-x64
// macOS apps are ad-hoc signed when built on a Mac (needed to launch on Apple Silicon). Zipping uses the
// system's zip tool (tar on Windows 10+).
//
// The Ripple Tank screensaver (built from screensaver/) ships inside each zip, in a "Screensaver" folder.
// Point SCREENSAVER_DIR at a folder holding it:
//   Windows zip: "Prismatica Ripple Tank.scr"
//   macOS zips:  "Prismatica Ripple Tank.saver" and "Prismatica Ripple Tank HD.saver"
// Without SCREENSAVER_DIR the zips are built without it (fine for testing, not for a release).
const { packager } = require('@electron/packager');
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const out = path.join(root, 'dist');
const ZIP_NAMES = {
  'win32-x64': 'Prismatica-Electron-Windows-x64.zip',
  'darwin-arm64': 'Prismatica-Electron-Mac-AppleSilicon-arm64.zip',
  'darwin-x64': 'Prismatica-Electron-Mac-Intel-x64.zip',
};
const targets = process.argv.slice(2).length ? process.argv.slice(2) : Object.keys(ZIP_NAMES);
const SAVER_DIR = process.env.SCREENSAVER_DIR ? path.resolve(process.env.SCREENSAVER_DIR) : null;
const SAVER_FILES = { win32: ['Prismatica Ripple Tank.scr'], darwin: ['Prismatica Ripple Tank.saver', 'Prismatica Ripple Tank HD.saver'] };
const SAVER_README = { win32: 'README-saver-windows.txt', darwin: 'README-saver-mac.txt' };

// Copy the screensaver into <dest>/Screensaver; returns false when SCREENSAVER_DIR isn't set.
function addScreensaver(platform, dest) {
  if (!SAVER_DIR) { console.warn('  SCREENSAVER_DIR not set: building without the Ripple Tank screensaver'); return false; }
  const into = path.join(dest, 'Screensaver');
  fs.mkdirSync(into, { recursive: true });
  for (const f of SAVER_FILES[platform]) {
    const from = path.join(SAVER_DIR, f);
    if (!fs.existsSync(from)) throw new Error(`screensaver file missing: ${from}`);
    fs.cpSync(from, path.join(into, f), { recursive: true, verbatimSymlinks: true });
  }
  fs.copyFileSync(path.join(root, '..', 'screensaver', 'res', SAVER_README[platform]), path.join(into, 'README.txt'));
  return true;
}

function zip(cwd, zipPath, items) {
  fs.rmSync(zipPath, { force: true });
  if (process.platform === 'win32') execFileSync('tar', ['-a', '-cf', zipPath, ...items], { cwd, stdio: 'inherit' });
  else execFileSync('zip', ['-qry9', zipPath, ...items], { cwd, stdio: 'inherit' });   // -y keeps the .app's symlinks
}

(async () => {
  for (const t of targets) {
    if (!ZIP_NAMES[t]) throw new Error(`unknown target ${t}; use one of ${Object.keys(ZIP_NAMES).join(', ')}`);
    const [platform, arch] = t.split('-');
    const [dir] = await packager({
      dir: root, out, overwrite: true, asar: true, prune: false,   // no runtime dependencies: leave node_modules out entirely
      name: 'Prismatica', platform, arch,
      icon: path.join(root, 'build', platform === 'win32' ? 'icon.ico' : 'icon.icns'),
      ignore: [/^\/dist($|\/)/, /^\/scripts($|\/)/, /^\/build($|\/)/, /^\/node_modules($|\/)/, /^\/saver($|\/)/, /^\/saver-macos\.tgz$/, /^\/package-lock\.json$/, /^\/\.[^/]+/],
      appBundleId: 'com.lunacrescens.prismatica', appCategoryType: 'public.app-category.music',
      appCopyright: 'Copyright (c) 2026 lunacrescens',
      win32metadata: { CompanyName: 'lunacrescens', FileDescription: 'Prismatica music visualizer', ProductName: 'Prismatica' },
      extendInfo: {
        NSMicrophoneUsageDescription: 'Prismatica uses the microphone to visualize sound when you choose Microphone.',
        NSAudioCaptureUsageDescription: 'Prismatica listens to system audio to visualize it when you choose System audio.',
      },
    });
    fs.copyFileSync(path.join(root, 'build', 'README-dist.txt'), path.join(dir, 'README.txt'));
    const zipPath = path.join(out, ZIP_NAMES[t]);
    if (platform === 'darwin') {
      if (process.platform === 'darwin') execFileSync('codesign', ['--force', '--deep', '--sign', '-', path.join(dir, 'Prismatica.app')], { stdio: 'inherit' });
      else console.warn('  (not on a Mac: the app is unsigned and Apple Silicon Macs will refuse to open it)');
      const items = ['Prismatica.app', 'README.txt'];
      if (addScreensaver('darwin', dir)) items.push('Screensaver');
      zip(dir, zipPath, items);
    } else {
      // Windows zip holds a "Prismatica" folder
      const staged = path.join(out, 'Prismatica');
      fs.rmSync(staged, { recursive: true, force: true });
      fs.cpSync(dir, staged, { recursive: true });
      addScreensaver('win32', staged);
      zip(out, zipPath, ['Prismatica']);
      fs.rmSync(staged, { recursive: true, force: true });
    }
    console.log('built', path.relative(root, zipPath));
  }
})().catch((e) => { console.error(e); process.exit(1); });
