<script setup lang="ts">
import { computed, onMounted, ref, watch } from "vue";
import { useRoute, RouterLink, RouterView } from "vue-router";
import { Search, Sun, Moon, RefreshCw } from "lucide-vue-next";
import { luaToJson } from "./utils/luaParser";

declare global {
  interface Window {
    electronAPI?: {
      selectFolder: () => Promise<{
        root: string;
        files: Array<{ path: string; content: string }>;
      } | null>;
      openExternal: (url: string) => void;
    };
  }
}


type Item = { id: number; en?: string; category?: string };
type KeyItem = { id: number; en?: string; category?: string };
type Inventory = Record<string, Record<string, number> | number>;
type Description = { id: number; en: string };
type Row = {
  id: string;
  name: string;
  group: string;
  category?: string;
  count?: number;
  desc?: string;
};

const characters = ref<Array<{ name: string; file: string }>>([]);

const loading = ref(true);
const error = ref("");
const items = ref<Record<string, Item>>({});
const keyItems = ref<Record<string, KeyItem>>({});
const inventories = ref<Record<string, Inventory>>({});
const descriptions = ref<Record<string, Description>>({});
const activeName = ref("");
const cachedLoaded = ref(false);

const filterTerm = ref("");
const categoryFilter = ref<"all" | string>("all");
const pageSize = ref(25);
const currentPage = ref(1);
const pageSizes = [25, 50, 75, 100];
const theme = ref<"light" | "dark">("light");
const rootPath = ref("");
const githubUrl = "https://github.com/Mandracord/VanaCargo";

type VirtualFile = {
  name: string;
  webkitRelativePath: string;
  text: () => Promise<string>;
};

const folderFiles = ref<VirtualFile[]>([]);

const route = useRoute();

function rowsToCSV(rows: Row[]) {
  const headers = [
    "character",
    "storage",
    "item_id",
    "item_name",
    "count",
    "category",
    "description",
  ];

  const escape = (v: unknown) => `"${String(v ?? "").replace(/"/g, '""')}"`;

  const lines = [
    headers.join(","),
    ...rows.map((r) =>
      [
        activeName.value,
        displayStorage(r.group),
        r.id,
        r.name,
        r.count ?? "",
        r.category ?? "",
        r.desc ?? "",
      ]
        .map(escape)
        .join(",")
    ),
  ];

  return lines.join("\n");
}

async function exportCSV() {
  const csv = rowsToCSV(tableRows.value);

  const filename = `VanaCargo_${activeName.value}_inventory.csv`;

  if ((window as any).electronAPI?.saveFile) {
    await (window as any).electronAPI.saveFile(filename, csv);
    return;
  }

  const blob = new Blob([csv], { type: "text/csv;charset=utf-8;" });
  const url = URL.createObjectURL(blob);

  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  a.click();

  URL.revokeObjectURL(url);
}

const activeInventory = computed<Inventory>(
  () => inventories.value[activeName.value] || {}
);
const routeGroup = computed(() => (route.meta.group as string) || "All");
const isKeyView = computed(() => routeGroup.value === "Key Items");

const keyItemList = computed(() => {
  const keys =
    (activeInventory.value["key items"] as Record<string, number>) || {};
  return Object.keys(keys)
    .map((id) => ({
      id,
      name: keyItems.value[id]?.en || `Key item #${id}`,
      category: keyItems.value[id]?.category || "Key Item",
    }))
    .sort((a, b) => a.name.localeCompare(b.name));
});
const itemGroups = computed(() => {
  const entries = Object.entries(activeInventory.value).filter(
    ([name]) => name !== "gil" && name !== "key items"
  );
  return entries.map(([name, payload]) => ({
    name,
    items: Object.entries((payload as Record<string, number>) || {}).map(
      ([id, count]) => ({
        id,
        count,
        name: items.value[id]?.en || `Item #${id}`,
        category: items.value[id]?.category || "Uncategorized",
      })
    ),
  }));
});

const storageTabs = [
  { label: "All", route: { name: "all" }, icon: "fa-layer-group" },
  { label: "Inventory", route: { name: "inventory" }, icon: "fa-box" },
  { label: "Mog Safe", route: { name: "mog-safe" }, icon: "fa-vault" },
  { label: "Storage", route: { name: "storage" }, icon: "fa-warehouse" },
  { label: "Mog Locker", route: { name: "mog-locker" }, icon: "fa-lock" },
  {
    label: "Mog Satchel",
    route: { name: "mog-satchel" },
    icon: "fa-briefcase",
  },
  { label: "Mog Sack", route: { name: "mog-sack" }, icon: "fa-briefcase" },
  { label: "Mog Case", route: { name: "mog-case" }, icon: "fa-briefcase" },
  { label: "Wardrobes", route: { name: "wardrobes" }, icon: "fa-shirt" },
  { label: "Slips", route: { name: "slips" }, icon: "fa-ticket" },
  { label: "Key Items", route: { name: "key-items" }, icon: "fa-key" },
];

const availableCategories = computed(() => {
  const set = new Set<string>();
  itemGroups.value.forEach((group) =>
    group.items.forEach((item) => item.category && set.add(item.category))
  );
  return ["all", ...Array.from(set).sort((a, b) => a.localeCompare(b))];
});

const tableRows = computed(() => {
  const term = filterTerm.value.toLowerCase();
  const cat = categoryFilter.value;

  if (routeGroup.value === "Key Items") {
    return keyItemList.value
      .map<Row>((item) => ({
        ...item,
        group: "Key Items",
        count: 1,
        desc: item.category || "",
      }))
      .filter((row) => {
        const matchesCat = cat === "all" || row.category === cat;
        if (!term) return matchesCat;
        const matchesText =
          row.name.toLowerCase().includes(term) ||
          (row.category || "").toLowerCase().includes(term) ||
          row.group.toLowerCase().includes(term);
        return matchesText && matchesCat;
      })
      .sort((a, b) => {
        const adesc = (a.desc || "").toLowerCase();
        const bdesc = (b.desc || "").toLowerCase();
        if (adesc !== bdesc) return adesc.localeCompare(bdesc);
        return a.name.localeCompare(b.name);
      });
  }

  const groupsToUse = (() => {
    const groups = itemGroups.value;
    switch (routeGroup.value) {
      case "All":
        return groups;
      case "Inventory":
        return groups.filter((g) => g.name.toLowerCase() === "inventory");
      case "Mog Safe":
        return groups.filter((g) => g.name.toLowerCase().includes("safe"));
      case "Storage":
        return groups.filter((g) => g.name.toLowerCase() === "storage");
      case "Mog Locker":
        return groups.filter((g) => g.name.toLowerCase().includes("locker"));
      case "Mog Satchel":
        return groups.filter((g) => g.name.toLowerCase().includes("satchel"));
      case "Mog Sack":
        return groups.filter((g) => g.name.toLowerCase().includes("sack"));
      case "Mog Case":
        return groups.filter((g) => g.name.toLowerCase().includes("case"));
      case "Wardrobes":
        return groups
          .filter((g) => g.name.toLowerCase().includes("wardrobe"))
          .sort((a, b) =>
            a.name.localeCompare(b.name, undefined, { numeric: true })
          );
      case "Slips":
        return groups
          .filter((g) => g.name.toLowerCase().includes("slip"))
          .sort((a, b) =>
            a.name.localeCompare(b.name, undefined, { numeric: true })
          );
      default:
        return [];
    }
  })();

  return groupsToUse
    .flatMap((group) =>
      group.items.map<Row>((item) => ({
        ...item,
        group: group.name,
        desc: normalizeDesc(descriptions.value[item.id]?.en || ""),
      }))
    )
    .filter((row) => {
      const matchesCat = cat === "all" || row.category === cat;
      if (!term) return matchesCat;
      const matchesText =
        row.name.toLowerCase().includes(term) ||
        (row.category || "").toLowerCase().includes(term) ||
        row.group.toLowerCase().includes(term);
      return matchesText && matchesCat;
    })
    .sort((a, b) => {
      if (routeGroup.value === "Wardrobes" || routeGroup.value === "Slips") {
        const ao = storageOrder(a.group);
        const bo = storageOrder(b.group);
        if (ao.key === bo.key && ao.num !== bo.num) return ao.num - bo.num;
        if (ao.key !== bo.key) return ao.key.localeCompare(bo.key);
      }
      return a.name.localeCompare(b.name);
    });
});

const totalPages = computed(() =>
  Math.max(1, Math.ceil(tableRows.value.length / pageSize.value))
);

const pagedRows = computed(() => {
  const page = Math.min(currentPage.value, totalPages.value);
  const start = (page - 1) * pageSize.value;
  return tableRows.value.slice(start, start + pageSize.value);
});

watch([filterTerm, categoryFilter, routeGroup, tableRows], () => {
  currentPage.value = 1;
});

onMounted(() => {
  const stored = localStorage.getItem("theme");
  if (stored === "dark" || stored === "light") {
    theme.value = stored;
    applyTheme(stored);
  } else {
    applyTheme(theme.value);
  }

  const storedRoot = localStorage.getItem("rootPath");
  if (storedRoot) {
    rootPath.value = storedRoot;
  }

  const storedChars = localStorage.getItem("characters");
  if (storedChars) {
    try {
      const parsed = JSON.parse(storedChars) as {
        name: string;
        file: string;
      }[];
      if (Array.isArray(parsed) && parsed.length) {
        characters.value = parsed;
        activeName.value = parsed[0]?.name ?? "";
      }
    } catch {}
  }

  const cached = localStorage.getItem("cacheData");
  if (cached) {
    try {
      const parsed = JSON.parse(cached) as {
        items: Record<string, Item>;
        keyItems: Record<string, KeyItem>;
        descriptions: Record<string, Description>;
        inventories: Record<string, Inventory>;
        characters: Array<{ name: string; file: string }>;
      };
      items.value = parsed.items || {};
      keyItems.value = parsed.keyItems || {};
      descriptions.value = parsed.descriptions || {};
      inventories.value = parsed.inventories || {};
      if (parsed.characters?.length) {
        characters.value = parsed.characters;
        activeName.value = parsed.characters[0]?.name ?? "";
      }
      cachedLoaded.value = true;
    } catch {}
  }

  loadData();
});

async function loadData() {
  try {
    if (folderFiles.value.length) {
      await loadFromFolder();
    } else {
      if (cachedLoaded.value) {
        error.value = "";
        return;
      }
      error.value = "Select your Windower folder to load data.";
      return;
    }

    localStorage.setItem(
      "cacheData",
      JSON.stringify({
        items: items.value,
        keyItems: keyItems.value,
        descriptions: descriptions.value,
        inventories: inventories.value,
        characters: characters.value,
      })
    );
  } catch (err) {
    console.error(err);
    error.value =
      "Unable to load Lua data. Ensure the Windower root is set and files exist.";
  } finally {
    loading.value = false;
  }
}

function formatNumber(value: number | string | undefined) {
  return Number(value || 0).toLocaleString();
}

function normalizeDesc(text: string) {
  if (!text) return "";
  return text.replace(/\s+/g, " ").trim();
}

function displayCategory(cat?: string) {
  if (!cat) return "—";
  if (cat === "Maze") return "Slips";
  return cat;
}

function displayStorage(name: string) {
  if (!name) return "—";
  const wardrobe = name.match(/^wardrobe\s*(\d*)$/i);
  if (wardrobe) {
    const suffix = wardrobe[1] ? ` ${wardrobe[1]}` : "";
    return `Wardrobe${suffix}`;
  }
  const slip = name.match(/^slip\s*(\d*)$/i);
  if (slip) {
    const suffix = slip[1] ? ` ${slip[1]}` : "";
    return `Slip${suffix}`;
  }
  return name
    .split(/\s+/)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join(" ");
}

function storageOrder(name: string) {
  const w = name.match(/^wardrobe\s*(\d*)$/i);
  if (w) return { key: "wardrobe", num: Number(w[1] || 1) };
  const s = name.match(/^slip\s*(\d*)$/i);
  if (s) return { key: "slip", num: Number(s[1] || 0) };
  return { key: name.toLowerCase(), num: 0 };
}

function normalizePath(p: string) {
  return p.replace(/\\/g, "/");
}

async function loadFromFolder() {
  const hasFindall = folderFiles.value.some((f) =>
    /addons[/\\]findall[/\\]/i.test(
      normalizePath(f.webkitRelativePath || f.name)
    )
  );
  if (!hasFindall) {
    error.value =
      "FindAll addon not detected. Please select a Windower root with addons/findall installed.";
    return;
  }

  const find = (rel: string) => {
    const target = normalizePath(rel).replace(/^\/+/, "");
    return folderFiles.value.find((f) =>
      normalizePath(f.webkitRelativePath || f.name).endsWith(target)
    );
  };

  const itemsFile = find("res/items.lua");
  const keyItemsFile = find("res/key_items.lua");
  const descFile = find("res/item_descriptions.lua");
  if (!itemsFile || !keyItemsFile) {
    throw new Error("Required res files not found in selected folder.");
  }

  const readLua = async (file: VirtualFile) => {
    const text = await file.text();
    return luaToJson(text);
  };

  items.value = await readLua(itemsFile);
  keyItems.value = await readLua(keyItemsFile);
  descriptions.value = descFile ? await readLua(descFile) : {};

  const charFiles = folderFiles.value.filter((f) =>
    /addons[/\\]findall[/\\]data[/\\].+\.lua$/i.test(
      normalizePath(f.webkitRelativePath || f.name)
    )
  );

  const nextChars = charFiles.map((f) => {
    const rel = normalizePath(f.webkitRelativePath || f.name);
    const nameMatch = rel.match(/addons[/\\]findall[/\\]data[/\\](.+)\.lua$/i);
    const name = nameMatch && nameMatch[1] ? nameMatch[1] : rel;
    return { name, file: rel };
  });

  if (nextChars.length) {
    characters.value = nextChars;
    activeName.value = nextChars[0]?.name ?? "";
  } else {
    error.value =
      "Missing addons/findall/data character files. This addon is required.";
    return;
  }

  for (const f of charFiles) {
    const rel = normalizePath(f.webkitRelativePath || f.name);
    const nameMatch = rel.match(/addons[/\\]findall[/\\]data[/\\](.+)\.lua$/i);
    const name = nameMatch && nameMatch[1] ? nameMatch[1] : rel;
    inventories.value[name] = await readLua(f);
  }

  error.value = "";
}

function toggleTheme() {
  const next = theme.value === "light" ? "dark" : "light";
  theme.value = next;
  localStorage.setItem("theme", next);
  applyTheme(next);
}

function applyTheme(value: "light" | "dark") {
  const root = document.documentElement;
  if (value === "dark") {
    root.classList.add("dark");
  } else {
    root.classList.remove("dark");
  }
}

async function selectFolderNative() {
  if (!window.electronAPI?.selectFolder) {
    error.value =
      "Native folder picker unavailable in browser build. Use desktop build to avoid browser prompts.";
    return;
  }
  try {
    const result = await window.electronAPI.selectFolder();
    if (!result) return;
    rootPath.value = result.root;
    folderFiles.value = result.files.map((f) => {
      const rel = normalizePath(f.path);
      const name = rel.split("/").pop() || rel;
      return {
        name,
        webkitRelativePath: rel,
        text: async () => f.content,
      };
    });
    await loadData();
  } catch (e) {
    console.error(e);
    error.value = "Failed to read selected folder.";
  }
}
</script>
<template>
  <div :class="['app', theme === 'dark' ? 'theme-dark' : 'theme-light']">
    <header class="card header relative">
      <div class="header-main">
        <div class="brand">
          <svg
            xmlns="http://www.w3.org/2000/svg"
            width="28"
            height="28"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            stroke-width="2"
            stroke-linecap="round"
            stroke-linejoin="round"
            class="lucide lucide-box"
          >
            <path
              d="M21 8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16Z"
            />
            <path d="m3.3 7 8.7 5 8.7-5" />
            <path d="M12 22V12" />
          </svg>
          <h2>Vana Cargo</h2>
        </div>
        <div class="root-input">
          <input
            v-model="rootPath"
            type="text"
            placeholder="C:\Program & Files (x86)\Windower..?"
            readonly
          />
          <button class="btn ghost" type="button" @click="selectFolderNative">
            Select folder
          </button>
        </div>
        <p v-if="error" class="error-text">{{ error }}</p>
      </div>
      <div class="header-actions-right">
        <a
          class="btn ghost icon-only"
          :href="githubUrl"
          target="_blank"
          rel="noreferrer"
          title="Github Repo"
        >
          <i class="fa-brands fa-github" aria-hidden="true"></i>
        </a>
        <div
          class="theme-toggle"
          role="button"
          :aria-label="`Switch to ${theme === 'light' ? 'dark' : 'light'} mode`"
          @click="toggleTheme"
        >
          <div :class="['toggle-thumb', theme === 'dark' ? 'dark' : 'light']">
            <Sun v-if="theme === 'light'" :size="14" />
            <Moon v-else :size="14" />
          </div>
        </div>
      </div>
    </header>

    <main class="layout">
      <aside class="card sidebar">
        <div class="section-title row">
          <span>Character List</span>
          <button
            class="btn ghost icon-only small"
            type="button"
            @click="loadData"
            title="Refresh"
          >
            <RefreshCw :size="14" />
          </button>
        </div>
        <div class="chip-list">
          <button
            v-for="c in characters"
            :key="c.name"
            :class="['chip', activeName === c.name ? 'active' : '']"
            @click="activeName = c.name"
          >
            <i class="fa-solid fa-user" aria-hidden="true"></i>
            {{ c.name }}
          </button>
        </div>
      </aside>

      <section class="content">
        <div class="card tabs-card">
          <div class="tabs">
            <RouterLink
              v-for="tab in storageTabs"
              :key="tab.label"
              :to="tab.route"
              class="tab"
              :class="routeGroup === tab.label ? 'active' : ''"
            >
              <i
                v-if="tab.icon"
                :class="['fa-solid', tab.icon]"
                aria-hidden="true"
              ></i>
              {{ tab.label }}
            </RouterLink>
          </div>
        </div>

        <div class="card">
          <div class="filters-row">
            <div class="filters-left">
              <label class="filter">
                <Search :size="16" />
                <input
                  v-model="filterTerm"
                  type="search"
                  placeholder="Search items"
                />
              </label>

              <label class="filter">
                <span>Category</span>
                <select v-model="categoryFilter">
                  <option v-for="c in availableCategories" :key="c" :value="c">
                    {{ c === "all" ? "All categories" : displayCategory(c) }}
                  </option>
                </select>
              </label>
            </div>

            <div class="filters-right">
              <button
                class="btn ghost small"
                @click="exportCSV"
                title="Export inventory to CSV"
              >
                <i class="fa-solid fa-file-csv"></i>
                Export CSV
              </button>

              <span class="gil">
                <i class="fa-solid fa-coins"></i>
                {{ formatNumber((activeInventory.gil as number) || 0) }}
              </span>
            </div>
          </div>

          <RouterView v-slot="{ Component }">
            <component
              :is="Component"
              :rows="
                pagedRows.map((row) => ({
                  ...row,
                  group: displayStorage(row.group),
                  desc: row.desc || displayCategory(row.category),
                }))
              "
              :show-count="!isKeyView"
            />
          </RouterView>

          <div class="table-footer">
            <div class="pager">
              <span class="page-size">
                Rows
                <select v-model.number="pageSize">
                  <option v-for="size in pageSizes" :key="size" :value="size">
                    {{ size }}
                  </option>
                </select>
              </span>

              <button
                class="btn ghost"
                :disabled="currentPage === 1"
                @click="currentPage = Math.max(1, currentPage - 1)"
              >
                Prev
              </button>

              <span>
                Page {{ Math.min(currentPage, totalPages) }} / {{ totalPages }}
              </span>

              <button
                class="btn ghost"
                :disabled="currentPage >= totalPages"
                @click="currentPage = Math.min(totalPages, currentPage + 1)"
              >
                Next
              </button>
            </div>
          </div>
        </div>
      </section>
    </main>
  </div>
</template>

<style scoped>
.filters-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  flex-wrap: wrap;
  margin-bottom: 12px;
}

.filters-left {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}

.filters-right {
  display: flex;
  align-items: center;
  gap: 12px;
}

.filter {
  display: inline-flex;
  align-items: center;
  gap: 8px;
}

.gil {
  display: flex;
  align-items: center;
  gap: 6px;
  font-weight: 500;
}

.table-footer {
  display: flex;
  justify-content: flex-end;
  margin-top: 8px;
  padding-top: 8px;
  border-top: 1px solid var(--border-subtle);
}

.pager {
  display: flex;
  align-items: center;
  gap: 10px;
}

.page-size {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  margin-right: 12px;
  font-size: 0.9rem;
  color: var(--text-muted);
}

.item-link,
.item-link:visited,
.item-link:hover,
.item-link:active,
.item-link:focus {
  text-decoration: none !important;
  color: inherit;
}

select {
  appearance: none;
  -webkit-appearance: none;
  -moz-appearance: none;

  height: 32px;
  padding: 0 28px 0 10px;

  border-radius: 8px;
  border: 1px solid var(--border-subtle);
  background-color: var(--surface);
  color: var(--text-primary);

  font: inherit;
  line-height: 1;
}

select:focus {
  outline: none;
  border-color: var(--accent);
}

input[type="search"] {
  height: 32px;
  padding: 0 12px;
  border-radius: 8px;
  border: 1px solid var(--border-subtle);
  font: inherit;
}

:deep(table) {
  width: 100%;
}

:deep(thead),
:deep(tbody tr) {
  display: table;
  width: 100%;
  table-layout: fixed;
}

:deep(tbody) {
  display: block;
  min-height: 160px;
}

</style>
