<script setup lang="ts">
import ItemsTable from '../components/ItemsTable.vue';
import type { PropType } from 'vue';

type Row = {
  id: string;
  name: string;
  group: string;
  category?: string;
  count?: number;
  desc?: string;
};

defineProps({
  rows: {
    type: Array as PropType<Row[]>,
    required: true,
  },
  showCount: {
    type: Boolean,
    default: true,
  },
});
</script>

<template>
  <ItemsTable :rows="rows" :show-count="showCount" />
</template>
