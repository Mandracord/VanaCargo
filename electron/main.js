"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
const electron_1 = require("electron");
const node_path_1 = __importDefault(require("node:path"));
const promises_1 = __importDefault(require("node:fs/promises"));
const isDev = process.env.NODE_ENV === "development" ||
    !!process.env.VITE_DEV_SERVER_URL;
const devServerUrl = process.env.VITE_DEV_SERVER_URL || "http://localhost:5173";
async function createWindow() {
    const win = new electron_1.BrowserWindow({
        width: 1920,
        height: 1080,
        webPreferences: {
            contextIsolation: true,
            preload: node_path_1.default.join(__dirname, "preload.js"),
        },
    });
    // 🔴 CRITICAL PART — FORCE EXTERNAL LINKS TO SYSTEM BROWSER
    win.webContents.setWindowOpenHandler(({ url }) => {
        electron_1.shell.openExternal(url);
        return { action: "deny" };
    });
    // ALSO CATCH IN-APP NAVIGATION ATTEMPTS (safety net)
    win.webContents.on("will-navigate", (event, url) => {
        if (!url.startsWith(devServerUrl)) {
            event.preventDefault();
            electron_1.shell.openExternal(url);
        }
    });
    if (isDev) {
        await win.loadURL(devServerUrl);
        win.webContents.openDevTools({ mode: "detach" });
    }
    else {
        await win.loadFile(node_path_1.default.join(process.resourcesPath, "app.asar", "dist", "index.html"));
    }
}
electron_1.app.whenReady().then(() => {
    electron_1.ipcMain.handle("select-folder", async () => {
        const { canceled, filePaths } = await electron_1.dialog.showOpenDialog({
            properties: ["openDirectory"],
        });
        if (canceled || !filePaths.length)
            return null;
        const root = filePaths[0];
        const files = await gatherFiles(root);
        return { root, files };
    });
    electron_1.ipcMain.handle("save-file", async (_event, { defaultName, content }) => {
        const { canceled, filePath } = await electron_1.dialog.showSaveDialog({
            defaultPath: defaultName,
            filters: [{ name: "CSV", extensions: ["csv"] }],
        });
        if (canceled || !filePath)
            return false;
        await promises_1.default.writeFile(filePath, content, "utf-8");
        return true;
    });
    createWindow();
});
electron_1.app.on("window-all-closed", () => {
    if (process.platform !== "darwin")
        electron_1.app.quit();
});
async function gatherFiles(root) {
    const targets = [
        "res/items.lua",
        "res/key_items.lua",
        "res/item_descriptions.lua",
    ];
    const out = [];
    for (const rel of targets) {
        const full = node_path_1.default.join(root, rel);
        try {
            const content = await promises_1.default.readFile(full, "utf-8");
            out.push({ path: rel, content });
        }
        catch { }
    }
    const dataDir = node_path_1.default.join(root, "addons/findall/data");
    try {
        const entries = await promises_1.default.readdir(dataDir, { withFileTypes: true });
        for (const entry of entries) {
            if (entry.isFile() && entry.name.toLowerCase().endsWith(".lua")) {
                const rel = node_path_1.default.join("addons/findall/data", entry.name);
                const full = node_path_1.default.join(dataDir, entry.name);
                try {
                    const content = await promises_1.default.readFile(full, "utf-8");
                    out.push({ path: rel, content });
                }
                catch { }
            }
        }
    }
    catch { }
    return out;
}
