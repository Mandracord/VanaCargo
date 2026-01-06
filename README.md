# Vana Cargo

Offline inventory viewer for **Final Fantasy XI**.  
Desktop application built with **Vue + Electron**, designed to read Windower-exported Lua inventory files and present them in a clean, multi-character UI.

## Overview

Vana Cargo is **not a Windower addon** and does not interact with the game client.  
It operates entirely offline by parsing Lua data files already exported by the addon FindAll.

Each character is loaded into its own tab, allowing quick inspection and comparison of inventories across characters.

## Usage

1. Launch the Vana Cargo desktop app
2. Pick your Windower root folder (where your Windower folder is located, e.g C:\Windower)
3. Data will load automatically

No network access or game process is required.

## Inventory Data

Inventory data is read from Lua files containing structured tables.

Expected inputs include:
- Counts of stackable items
- Key item tables
- Per-character inventory snapshot

## Supported Platforms

- Windows (portable executable)
- Linux (through Wine)

## Technical Notes

- Application: Vue (TypeScript) application
- Desktop shell: Electron
- Lua parsing is intentionally lightweight and tailored to known Windower export formats
- The app is read-only by design and will never modify game data or files

## License

MIT

