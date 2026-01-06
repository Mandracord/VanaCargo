<script setup lang="ts">
type Row = {
  id: string;
  name: string;
  group: string;
  category?: string;
  count?: number;
  desc?: string;
};

defineProps<{
  rows: Row[];
  showCount?: boolean;
  emptyMessage?: string;
}>();
</script>

<template>
  <div class="table-wrap">
    <table>
      <thead>
        <tr>
          <th>Item</th>
          <th v-if="showCount !== false">Count</th>
          <th>Description</th>
          <th>Storage</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="row in rows" :key="row.group + '-' + row.id">
          <td class="strong">{{ row.name }}</td>
          <td v-if="showCount !== false">{{ row.count ?? '—' }}</td>
          <td class="muted">{{ row.desc }}</td>
          <td>{{ row.group }}</td>
        </tr>
        <tr v-if="rows.length === 0">
          <td :colspan="showCount !== false ? 4 : 3" class="muted center">
            {{ emptyMessage || 'No items match the current filters.' }}
          </td>
        </tr>
      </tbody>
    </table>
  </div>
</template>
