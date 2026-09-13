<script setup>
import { ref } from 'vue'

const props = defineProps([
  'platform',
  'config'
])

const config = ref(props.config)
</script>

<template>
  <div class="config-page">
    <!-- FEC Percentage -->
    <div class="mb-3">
      <label for="fec_percentage" class="form-label">{{ $t('config.fec_percentage') }}</label>
      <input type="text" class="form-control" id="fec_percentage" placeholder="20" v-model="config.fec_percentage" />
      <div class="form-text">{{ $t('config.fec_percentage_desc') }}</div>
    </div>

    <div class="mb-3">
      <label for="max_bitrate" class="form-label">{{ $t('config.max_bitrate') }}</label>
      <input type="number" class="form-control" id="max_bitrate" placeholder="0" v-model="config.max_bitrate" />
      <div class="form-text">{{ $t('config.max_bitrate_desc') }}</div>
    </div>
  </div>
</template>
