// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
#include "audio_config.h"

#include <stdbool.h>
#include <stdint.h>

#include "pico.h"
#include "pico_hdmi/hstx_data_island_queue.h"

// Called for every audio packet queued; in RAM for the same reason as the
// encoder in audio_packet.c. The per-line tick and pop are already in
// scratch RAM upstream.
bool __not_in_flash_func(hstx_di_queue_push)(const hstx_data_island_t *island);
uint32_t __not_in_flash_func(hstx_di_queue_get_level)(void);

#include "pico_hdmi/hstx_data_island_queue.c.inc"
