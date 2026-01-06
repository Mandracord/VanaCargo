import { createRouter, createWebHistory } from 'vue-router';
import InventoryView from '../views/InventoryView.vue';
import KeyItemsView from '../views/KeyItemsView.vue';

const routes = [
  { path: '/', name: 'all', component: InventoryView, meta: { group: 'All' } },
  { path: '/inventory', name: 'inventory', component: InventoryView, meta: { group: 'Inventory' } },
  { path: '/mog-safe', name: 'mog-safe', component: InventoryView, meta: { group: 'Mog Safe' } },
  { path: '/storage', name: 'storage', component: InventoryView, meta: { group: 'Storage' } },
  { path: '/mog-locker', name: 'mog-locker', component: InventoryView, meta: { group: 'Mog Locker' } },
  { path: '/mog-satchel', name: 'mog-satchel', component: InventoryView, meta: { group: 'Mog Satchel' } },
  { path: '/mog-sack', name: 'mog-sack', component: InventoryView, meta: { group: 'Mog Sack' } },
  { path: '/mog-case', name: 'mog-case', component: InventoryView, meta: { group: 'Mog Case' } },
  { path: '/wardrobes', name: 'wardrobes', component: InventoryView, meta: { group: 'Wardrobes' } },
  { path: '/slips', name: 'slips', component: InventoryView, meta: { group: 'Slips' } },
  { path: '/key-items', name: 'key-items', component: KeyItemsView, meta: { group: 'Key Items' } },
];

const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  routes,
  scrollBehavior() {
    return { top: 0 };
  },
});

export default router;
