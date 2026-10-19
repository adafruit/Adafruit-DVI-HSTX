// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
#include "audio_config.h"

#include <stdbool.h>
#include <stdint.h>

#include "pico.h"
#include "pico_hdmi/hstx_packet.h"

// The audio packet encoder runs for every packet queued, about 12,000 times
// a second at 48 kHz. From flash it churns the XIP cache, which on the
// RP2350 also serves PSRAM, so code running from flash or PSRAM slows down.
// Declaring the encoder's functions and tables here first puts the upstream
// definitions in RAM without editing the vendored file.
static const uint16_t ter_c4[16] __attribute__((section(".data.dvhstx_ter_c4")));
static const uint8_t bch_table[256] __attribute__((section(".data.dvhstx_bch")));
static const uint8_t parity_table[32] __attribute__((section(".data.dvhstx_parity")));
static bool __not_in_flash_func(compute_parity)(uint8_t v);
static bool __not_in_flash_func(compute_parity3)(uint8_t a, uint8_t b, uint8_t c);
void __not_in_flash_func(hstx_packet_init)(hstx_packet_t *packet);
static uint8_t __not_in_flash_func(encode_bch_3)(const uint8_t *p);
static uint8_t __not_in_flash_func(encode_bch_7)(const uint8_t *p);
static void __not_in_flash_func(compute_header_parity)(hstx_packet_t *p);
static void __not_in_flash_func(compute_subpacket_parity)(hstx_packet_t *p, int idx);
static void __not_in_flash_func(compute_all_parity)(hstx_packet_t *p);
static uint8_t __not_in_flash_func(channel_status_sample_frequency)(uint32_t sample_rate);
int __not_in_flash_func(hstx_packet_set_audio_samples_cs_rate)(
    hstx_packet_t *packet, const audio_sample_t *samples, int num_samples,
    int frame_count, uint32_t sample_rate);
static void __not_in_flash_func(encode_header_to_lane0)(
    const hstx_packet_t *packet, uint16_t *lane0, int hv, bool first_packet);
static void __not_in_flash_func(encode_subpackets_to_lanes)(
    const hstx_packet_t *packet, uint16_t *lane1, uint16_t *lane2);
void __not_in_flash_func(hstx_encode_data_island)(hstx_data_island_t *out,
                                                  const hstx_packet_t *packet,
                                                  bool vsync_active,
                                                  bool hsync_active);

#include "pico_hdmi/hstx_packet.c.inc"
