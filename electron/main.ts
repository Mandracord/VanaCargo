import { app, BrowserWindow, dialog, ipcMain } from "electron";
import { fileURLToPath } from "node:url";
import * as path from "node:path";
import * as fs from "node:fs/promises";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const isDev = process.env.NODE_ENV === "development" || process.env.VITE_DEV_SERVER_URL;
const devServerUrl = process.env.VITE_DEV_SERVER_URL || "http://localhost:5173";

async function createWindow() {
  const win = new BrowserWindow({
    width: 1280,
    height: 800,
    webPreferences: {
      contextIsolation: true,
      preload: path.join(__dirname, "preload.js")
    }
  });

  if (isDev) {
    await win.loadURL(devServerUrl);
    win.webContents.openDevTools({ mode: "detach" });
  } else {
    const indexPath = path.join(__dirname, "../dist/index.html");
    await win.loadFile(indexPath);
  }
}

app.whenReady().then(() => {
  ipcMain.handle("select-folder", async () => {
    const { canceled, filePaths } = await dialog.showOpenDialog({
      properties: ["openDirectory"]
    });
    if (canceled || !filePaths.length) return null;
    const root = filePaths[0];
    const files = await gatherFiles(root);
    return { root, files };
  });

  createWindow();

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") {
    app.quit();
  }
});

async function gatherFiles(root: string) {
  const targets = [
    "res/items.lua",
    "res/key_items.lua",
    "res/item_descriptions.lua"
  ];
  const out: Array<{ path: string; content: string }> = [];

  for (const rel of targets) {
    const full = path.join(root, rel);
    try {
      const content = await fs.readFile(full, "utf-8");
      out.push({ path: rel, content });
    } catch {
      // ignore missing files
    }
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
        } catch {
          // ignore read errors
        }
      }
    }
  } catch {
    // missing addon folder
  }

  return out;
}
