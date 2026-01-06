# FFXI Inventory Viewer (Vue + Vite)

Simple proof-of-concept viewer that parses exported Lua inventory files and maps item IDs to readable names.

## Getting started

1. Install deps: `npm install`
2. Run dev server: `npm run dev` (Vite will serve `public/data`).
3. Visit the printed local URL.

## Data files

- Lua sources are served from `public/data/`. Current seeds:
  - `items.lua`
  - `key_items.lua`
  - `Meliora.lua`
- To add another character, drop its `name.lua` file into `public/data/` and add an entry to the `characters` array in `src/App.vue`, e.g. `{ name: 'Alt', file: '/data/Alt.lua' }`.

## Notes

- The Lua parser is a lightweight regex transform in `src/utils/luaParser.ts`; swap it for a real parser if the data gets more complex.
- Styling lives in `src/style.css` and the main view in `src/App.vue`.
