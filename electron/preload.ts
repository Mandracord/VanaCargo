import { contextBridge, ipcRenderer } from "electron";

contextBridge.exposeInMainWorld("electronAPI", {
  async selectFolder() {
    const result = await ipcRenderer.invoke("select-folder");
    if (!result) return null;
    return result as { root: string; files: Array<{ path: string; content: string }> };
  }
});
