// Build Prismatica's Electron app for Windows and macOS, zipped with the names the website links to.
//   node scripts/package.js                      all targets
//   node scripts/package.js win32-x64            just Windows
//   node scripts/package.js darwin-arm64 darwin-x64
// macOS apps are ad-hoc signed when built on a Mac (needed to launch on Apple Silicon). Zipping uses the
// system's zip tool (tar on Windows 10+).
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
      ignore: [/^\/dist($|\/)/, /^\/scripts($|\/)/, /^\/build($|\/)/, /^\/node_modules($|\/)/, /^\/package-lock\.json$/, /^\/\.[^/]+/],
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
      zip(dir, zipPath, ['Prismatica.app', 'README.txt']);
    } else {
      // Windows zip holds a "Prismatica" folder
      const staged = path.join(out, 'Prismatica');
      fs.rmSync(staged, { recursive: true, force: true });
      fs.cpSync(dir, staged, { recursive: true });
      zip(out, zipPath, ['Prismatica']);
      fs.rmSync(staged, { recursive: true, force: true });
    }
    console.log('built', path.relative(root, zipPath));
  }
})().catch((e) => { console.error(e); process.exit(1); });
