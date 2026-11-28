const { contextBridge, ipcRenderer, webUtils } = require('electron');

contextBridge.exposeInMainWorld('prismatica', {
  // text of a .lrc file sitting next to this audio file, or null
  sidecarLyrics: (file) => { let p = ''; try { p = webUtils.getPathForFile(file); } catch (e) {} return p ? ipcRenderer.invoke('lyrics:sidecar', p) : Promise.resolve(null); },
  // automatic LRCLIB lookup: { title, artist, album, duration } -> { synced, plain, match, approx, searched } | { error }
  fetchLyrics: (q) => ipcRenderer.invoke('lyrics:fetch', q),
  // manual search: text -> [{ id, title, artist, album, duration, synced }] | { error }
  searchLyrics: (text, duration) => ipcRenderer.invoke('lyrics:search', text, duration),
  lyricsById: (id) => ipcRenderer.invoke('lyrics:byId', id),
  lyricsLogPath: () => ipcRenderer.invoke('lyrics:logPath'),
  // drop the remembered software-rendering fallback and restart on the graphics card
  tryGPU: () => ipcRenderer.invoke('graphics:tryGPU'),
});
