---
name: internal-ram-tight
description: Inclinometer firmware has only ~15-17 KB internal RAM free; new buffers/stacks must go to PSRAM or SD writes fail
metadata:
  node_type: memory
  type: project
  modified: 2026-11-18T10:22:23.090Z
---

Internal RAM on the ESP32-S3 inclinometer is nearly exhausted by the display (strips, bounce buffers, 41 KB display list) and the BT controller. As of 2026-11-18 there were ~15-17 KB free; on 2026-11-24 `m` showed 14 KB internal, 6 KB DMA (serial `m` prints it). Even 8 KB of new static buffers dropped DMA-capable RAM to 0 and made SD writes fail (`esp_dma_capable_malloc ... Not enough heap memory`, `sdmmc_write_blocks failed`).

**Why:** the SDMMC driver needs small DMA-capable internal bounce buffers on every write; the display needs its internal DMA strips.

**How to apply:** put any new buffer >~256 B in PSRAM (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`; static .bss is internal and ext-BSS isn't enabled in this framework), create new tasks with `xTaskCreatePinnedToCoreWithCaps(..., MALLOC_CAP_SPIRAM)`, and after uploading check `m` on serial for DMA free > ~5 KB. Do the `m` check after a replug/power-cycle: right after `pio -t upload` the reset leaves I2C erroring (touch/expander), which is not a firmware fault. Related: [[panel-odd-rows-damage]]
