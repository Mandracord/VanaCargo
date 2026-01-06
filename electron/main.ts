import { app, BrowserWindow, dialog, ipcMain, shell } from "electron";
import path from "node:path";
import fs from "node:fs/promises";

const isDev =
  process.env.NODE_ENV === "development" ||
  !!process.env.VITE_DEV_SERVER_URL;

const devServerUrl =
  process.env.VITE_DEV_SERVER_URL || "http://localhost:5173";

async function createWindow() {
  const win = new BrowserWindow({
    width: 1920,
    height: 1080,
    webPreferences: {
      contextIsolation: true,
      preload: path.join(__dirname, "preload.js"),
    },
  });

  // 🔴 CRITICAL PART — FORCE EXTERNAL LINKS TO SYSTEM BROWSER
  win.webContents.setWindowOpenHandler(({ url }) => {
    shell.openExternal(url);
    return { action: "deny" };
  });

  // ALSO CATCH IN-APP NAVIGATION ATTEMPTS (safety net)
  win.webContents.on("will-navigate", (event, url) => {
    if (!url.startsWith(devServerUrl)) {
      event.preventDefault();
      shell.openExternal(url);
    }
  });

  if (isDev) {
    await win.loadURL(devServerUrl);
    win.webContents.openDevTools({ mode: "detach" });
  } else {
    await win.loadFile(
      path.join(process.resourcesPath, "app.asar", "dist", "index.html")
    );
  }
}

app.whenReady().then(() => {
  ipcMain.handle("select-folder", async () => {
    const { canceled, filePaths } = await dialog.showOpenDialog({
      properties: ["openDirectory"],
    });

    if (canceled || !filePaths.length) return null;

    const root = filePaths[0];
    const files = await gatherFiles(root);

    return { root, files };
  });

  ipcMain.handle(
    "save-file",
    async (
      _event,
      { defaultName, content }: { defaultName: string; content: string }
    ) => {
      const { canceled, filePath } = await dialog.showSaveDialog({
        defaultPath: defaultName,
        filters: [{ name: "CSV", extensions: ["csv"] }],
      });

      if (canceled || !filePath) return false;

      await fs.writeFile(filePath, content, "utf-8");
      return true;
    }
  );

  createWindow();
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});

async function gatherFiles(root: string) {
  const targets = [
    "res/items.lua",
    "res/key_items.lua",
    "res/item_descriptions.lua",
  ];

  const out: Array<{ path: string; content: string }> = [];

  for (const rel of targets) {
    const full = path.join(root, rel);
    try {
      const content = await fs.readFile(full, "utf-8");
      out.push({ path: rel, content });
    } catch {}
  }

  const dataDir = path.join(root, "addons/findall/data");
  try {
    const entries = await fs.readdir(dataDir, { withFileTypes: true });
    for (const entry of entries) {
      if (entry.isFile() && entry.name.toLowerCase().endsWith(".lua")) {
        const rel = path.join("addons/findall/data", entry.name);
        const full = path.join(dataDir, entry.name);
        try {
          const content = await fs.readFile(full, "utf-8");
          out.push({ path: rel, content });
        } catch {}
      }
    }
  } catch {}

  return out;
}
