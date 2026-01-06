"use strict";
const { contextBridge, ipcRenderer, shell } = require("electron");
contextBridge.exposeInMainWorld("electronAPI", {
    selectFolder: () => ipcRenderer.invoke("select-folder"),
    openExternal: (url) => shell.openExternal(url),
});
