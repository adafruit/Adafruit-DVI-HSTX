// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
//
// Audio over the DVI cable for DVHSTX. Only linked when a sketch enables
// audio (DVHSTX::enable_audio), so video-only sketches keep their RAM: the
// pico_hdmi packet queue alone is 36 KB.
//
// With audio on, every line carries a data island, as pico_hdmi sends them:
// an audio sample packet when one is due, otherwise a null packet. Blanking
// lines also carry the AVI InfoFrame, the audio InfoFrame and the audio clock
// regeneration (ACR) packets. Active lines get a video preamble and guard
// band before the pixels, which sinks expect once they see data islands.
// Layout follows pico_hdmi's build_line_with_di(): the island sits inside the
// hsync pulse when it fits, otherwise in the back porch after it.
#include <string.h>
#include <pico/stdlib.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/sync.h"

#include "dvi.hpp"
#include "dvhstx.hpp"
#include "dvhstx_lines.inl"

extern "C" {
#include "audio_config.h"
#include "pico_hdmi/hstx_packet.h"
#include "pico_hdmi/hstx_data_island_queue.h"
}


using namespace pimoroni;

#define NUM_CHANS DVHSTX::NUM_AUDIO_CHANS

static DVHSTX* audio_display = nullptr;

// Control symbols for active-low sync, lanes 1 and 2 as preambles need them.
#define PREAMBLE_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_01 << 10) | (TMDS_CTRL_01 << 20))
#define PREAMBLE_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_01 << 10) | (TMDS_CTRL_01 << 20))
#define PREAMBLE_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_01 << 10) | (TMDS_CTRL_01 << 20))
#define PREAMBLE_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_01 << 10) | (TMDS_CTRL_01 << 20))
#define VIDEO_PREAMBLE_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_01 << 10) | (TMDS_CTRL_00 << 20))
#define VIDEO_GUARD_BAND (0x2ccu | (0x133u << 10) | (0x2ccu << 20))
#define W_VIDEO_PREAMBLE 8
#define W_VIDEO_GUARD_BAND 2

#define DI_LINE_MAX_WORDS 64

static uint32_t di_acr_vsync_on[DI_LINE_MAX_WORDS], di_acr_vsync_off[DI_LINE_MAX_WORDS];
static uint32_t di_infoframe_vsync_on[DI_LINE_MAX_WORDS], di_avi_infoframe[DI_LINE_MAX_WORDS];
static uint di_acr_vsync_on_len, di_acr_vsync_off_len, di_infoframe_vsync_on_len, di_avi_infoframe_len;
// One blanking line and one active line header per DMA channel, so the
// island can be patched in while the other channels are still sending.
static uint32_t di_blank[NUM_CHANS][DI_LINE_MAX_WORDS];
static uint32_t di_active[NUM_CHANS][DI_LINE_MAX_WORDS];
static uint di_blank_len, di_active_len;
// Offset of the 36 island words in every line built by build_di_line().
static uint di_offset;
static const uint32_t* di_null;

// The island goes inside the hsync pulse when it fits there.
static bool di_in_hsync(const struct dvi_timing* t) {
    return t->h_sync_width >= W_PREAMBLE + W_DATA_ISLAND;
}

static bool di_fits(const struct dvi_timing* t) {
    return di_in_hsync(t) ||
           t->h_back_porch >= W_PREAMBLE + W_DATA_ISLAND + W_VIDEO_PREAMBLE + W_VIDEO_GUARD_BAND;
}

// Build one line with a data island. text adds the 6 black pixels the text
// mode header sends before its TMDS data. Returns the length in words.
static uint build_di_line(uint32_t* buf, const struct dvi_timing* t, const uint32_t* di,
                          bool vsync, bool active, bool text) {
    uint32_t* p = buf;
    const uint32_t sync_h0 = vsync ? SYNC_V0_H0 : SYNC_V1_H0;
    const uint32_t sync_h1 = vsync ? SYNC_V0_H1 : SYNC_V1_H1;
    const bool in_hsync = di_in_hsync(t);
    int back_porch = t->h_back_porch;

    *p++ = HSTX_CMD_RAW_REPEAT | t->h_front_porch;
    *p++ = sync_h1;
    if (in_hsync) {
        *p++ = HSTX_CMD_RAW_REPEAT | W_PREAMBLE;
        *p++ = vsync ? PREAMBLE_V0_H0 : PREAMBLE_V1_H0;
    } else {
        *p++ = HSTX_CMD_RAW_REPEAT | t->h_sync_width;
        *p++ = sync_h0;
        *p++ = HSTX_CMD_RAW_REPEAT | W_PREAMBLE;
        *p++ = vsync ? PREAMBLE_V0_H1 : PREAMBLE_V1_H1;
    }
    di_offset = p + 1 - buf;
    *p++ = HSTX_CMD_RAW | W_DATA_ISLAND;
    for (int i = 0; i < W_DATA_ISLAND; i++)
        *p++ = di[i];
    if (in_hsync) {
        const int rest = t->h_sync_width - W_PREAMBLE - W_DATA_ISLAND;
        if (rest) {
            *p++ = HSTX_CMD_RAW_REPEAT | rest;
            *p++ = sync_h0;
        }
    } else {
        back_porch -= W_PREAMBLE + W_DATA_ISLAND;
    }

    if (active) {
        *p++ = HSTX_CMD_RAW_REPEAT | (back_porch - W_VIDEO_PREAMBLE - W_VIDEO_GUARD_BAND);
        *p++ = sync_h1;
        *p++ = HSTX_CMD_RAW_REPEAT | W_VIDEO_PREAMBLE;
        *p++ = VIDEO_PREAMBLE_V1_H1;
        *p++ = HSTX_CMD_RAW_REPEAT | W_VIDEO_GUARD_BAND;
        *p++ = VIDEO_GUARD_BAND;
        if (text) {
            *p++ = HSTX_CMD_RAW | 6;
            for (int i = 0; i < 3; i++) {
                *p++ = BLACK_PIXEL_A;
                *p++ = BLACK_PIXEL_B;
            }
            *p++ = HSTX_CMD_TMDS | (t->h_active_pixels - 6);
        } else {
            *p++ = HSTX_CMD_TMDS | t->h_active_pixels;
        }
    } else {
        *p++ = HSTX_CMD_RAW_REPEAT | (back_porch + t->h_active_pixels);
        *p++ = sync_h1;
    }
    return p - buf;
}

// ACR N values as pico_hdmi uses them, 0 for unsupported rates; CTS
// follows from the pixel clock.
static uint32_t acr_n(uint32_t sample_rate) {
    switch (sample_rate) {
    case 32000: return 4096;
    case 44100: return 6272;
    case 48000: return 6144;
    default: return 0;
    }
}

// In main RAM, not scratch: scratch X also holds core 1's stack and is full.
static void __not_in_flash_func(copy_di)(uint32_t* dst, const uint32_t* di) {
    for (int i = 0; i < W_DATA_ISLAND; i++)
        dst[i] = di[i];
}

// Point ch at the next blanking line when audio is on.
static void __no_inline_not_in_flash_func(audio_blank_line)(dma_channel_hw_t* ch, uint ch_num, int v_scanline,
                                                            const struct dvi_timing* t, bool vsync) {
    if (vsync) {
        if (v_scanline == t->v_front_porch) {
            ch->read_addr = (uintptr_t)di_acr_vsync_on;
            ch->transfer_count = di_acr_vsync_on_len;
        } else {
            ch->read_addr = (uintptr_t)di_infoframe_vsync_on;
            ch->transfer_count = di_infoframe_vsync_on_len;
        }
    } else if (v_scanline == 0) {
        ch->read_addr = (uintptr_t)di_avi_infoframe;
        ch->transfer_count = di_avi_infoframe_len;
    } else if (v_scanline >= t->v_front_porch + t->v_sync_width && (v_scanline & 3) == 0) {
        ch->read_addr = (uintptr_t)di_acr_vsync_off;
        ch->transfer_count = di_acr_vsync_off_len;
    } else {
        const uint32_t* di = hstx_di_queue_get_audio_packet();
        copy_di(&di_blank[ch_num][di_offset], di ? di : di_null);
        ch->read_addr = (uintptr_t)di_blank[ch_num];
        ch->transfer_count = di_blank_len;
    }
}

static void __not_in_flash_func(dma_irq_handler_gfx_audio)() {
    audio_display->gfx_audio_dma_handler();
}

static void __not_in_flash_func(dma_irq_handler_text_audio)() {
    audio_display->text_audio_dma_handler();
}

// Each active line is two transfers, the header with its island and then the
// pixels, so repeated lines can share one pixel buffer as without audio.
void __not_in_flash_func(DVHSTX::gfx_audio_dma_handler)() {
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1u << ch_num;
    if (++ch_num == NUM_CHANS) ch_num = 0;

    const uint line_buf_total_len = ((timing_mode->h_active_pixels * line_bytes_per_pixel) >> 2) + line_header_words;

    if (pixels_pending) {
        pixels_pending = false;
        ch->read_addr = (uintptr_t)&line_buffers[line_num * line_buf_total_len + line_header_words];
        ch->transfer_count = line_buf_total_len - line_header_words;
    } else {
        hstx_di_queue_tick();
        if (v_scanline < v_inactive_total) {
            const bool vsync = v_scanline >= timing_mode->v_front_porch &&
                               v_scanline < timing_mode->v_front_porch + timing_mode->v_sync_width;
            audio_blank_line(ch, ch_num, v_scanline, timing_mode, vsync);
        } else {
            const int y = (v_scanline - v_inactive_total) >> v_repeat_shift;
            // Up to three transfers are in flight, from at most two rows
            // before this one, so three pixel buffers never collide.
            const int new_line_num = y % 3;
            if (line_num != new_line_num) {
                line_num = new_line_num;
                fill_gfx_line(&line_buffers[line_num * line_buf_total_len + line_header_words], y);
            }
            const uint32_t* di = hstx_di_queue_get_audio_packet();
            copy_di(&di_active[ch_num][di_offset], di ? di : di_null);
            ch->read_addr = (uintptr_t)di_active[ch_num];
            ch->transfer_count = di_active_len;
            pixels_pending = true;
            return;  // the line ends with its pixel transfer
        }
    }

    if (++v_scanline == v_total_active_lines) {
        v_scanline = 0;
        line_num = -1;
        frame_count++;
        if (flip_next) {
            flip_next = false;
            flip_now();
        }
        __sev();
    }
}

// Text lines are drawn into one buffer per channel already, so the header
// with its island stays in front of the pixels: one transfer per line.
void __not_in_flash_func(DVHSTX::text_audio_dma_handler)() {
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1u << ch_num;
    if (++ch_num == NUM_CHANS) ch_num = 0;

    hstx_di_queue_tick();
    if (v_scanline < v_inactive_total) {
        const bool vsync = v_scanline >= timing_mode->v_front_porch &&
                           v_scanline < timing_mode->v_front_porch + timing_mode->v_sync_width;
        audio_blank_line(ch, ch_num, v_scanline, timing_mode, vsync);
    } else {
        const int y = v_scanline - v_inactive_total;
        const uint line_buf_total_len = (frame_width * line_bytes_per_pixel + 3) / 4 + line_header_words;
        uint32_t* line = &line_buffers[ch_num * line_buf_total_len];
        const uint32_t* di = hstx_di_queue_get_audio_packet();
        copy_di(&line[di_offset], di ? di : di_null);
        ch->read_addr = (uintptr_t)line;
        ch->transfer_count = line_buf_total_len;
        fill_text_line(&line[line_header_words], y);
    }

    if (++v_scanline == v_total_active_lines) {
        v_scanline = 0;
        line_num = -1;
        frame_count++;
        if (flip_next) {
            flip_next = false;
            flip_now();
        }
        __sev();
    }
}

bool DVHSTX::setup_audio(bool text, const uint32_t** header, uint* header_words) {
    const struct dvi_timing* t = timing_mode;
    if (!di_fits(t)) {
        dvhstx_debug("No room for audio in this mode\n");
        return false;
    }
    if (!acr_n(audio_sample_rate)) {
        dvhstx_debug("Unsupported audio sample rate\n");
        return false;
    }
    audio_display = this;
    const bool in_hsync = di_in_hsync(t);
    const uint32_t h_total = t->h_front_porch + t->h_sync_width + t->h_back_porch + t->h_active_pixels;
    // HSTX sends one pixel every 5 clk_hstx cycles (see csr below).
    const uint32_t pixel_clock = clock_get_hz(clk_hstx) / 5;

    hstx_di_queue_init();
    hstx_di_queue_set_v_total(v_total_active_lines);
    hstx_di_queue_set_hsync_active(in_hsync);
    hstx_di_queue_set_sample_rate(audio_sample_rate);
    // Pace packets from the real pixel clock, not a nominal 60 Hz.
    const uint64_t pacing = ((uint64_t)audio_sample_rate * h_total) << 16;
    const uint32_t spl_fp = pacing / pixel_clock;
    hstx_di_queue_set_samples_per_line_exact(spl_fp, pacing - (uint64_t)spl_fp * pixel_clock, pixel_clock);

    di_null = hstx_get_null_data_island(false, in_hsync);
    di_blank_len = build_di_line(di_blank[0], t, di_null, false, false, text);
    for (int i = 1; i < NUM_CHANS; i++)
        memcpy(di_blank[i], di_blank[0], sizeof(di_blank[0]));

    hstx_packet_t packet;
    hstx_data_island_t island;

    const uint32_t n = acr_n(audio_sample_rate);
    const uint64_t div = 128ull * audio_sample_rate;
    const uint32_t cts = ((uint64_t)pixel_clock * n + div / 2) / div;
    hstx_packet_set_acr(&packet, n, cts);
    hstx_encode_data_island(&island, &packet, true, in_hsync);
    di_acr_vsync_on_len = build_di_line(di_acr_vsync_on, t, island.words, true, false, text);
    hstx_encode_data_island(&island, &packet, false, in_hsync);
    di_acr_vsync_off_len = build_di_line(di_acr_vsync_off, t, island.words, false, false, text);

    hstx_packet_set_audio_infoframe(&packet, audio_sample_rate, 2, 16);
    hstx_encode_data_island(&island, &packet, true, in_hsync);
    di_infoframe_vsync_on_len = build_di_line(di_infoframe_vsync_on, t, island.words, true, false, text);

    // CTA-861 VICs for the modes that match one exactly; 0 for the rest.
    uint8_t vic = 0;
    if (t == &dvi_timing_640x480p_60hz) vic = 1;
    else if (t == &dvi_timing_720x480p_60hz) vic = 2;
    else if (t == &dvi_timing_720x576p_50hz) vic = 17;
    const bool wide = t->h_active_pixels * 9 >= t->v_active_lines * 16;
    hstx_packet_set_avi_infoframe_aspect(&packet, vic, 0, wide);
    hstx_encode_data_island(&island, &packet, false, in_hsync);
    di_avi_infoframe_len = build_di_line(di_avi_infoframe, t, island.words, false, false, text);

    di_active_len = build_di_line(di_active[0], t, di_null, false, true, text);
    for (int i = 1; i < NUM_CHANS; i++)
        memcpy(di_active[i], di_active[0], sizeof(di_active[0]));
    if (text) {
        // Text lines are one transfer each, header included.
        *header = di_active[0];
        *header_words = di_active_len;
        audio_irq = dma_irq_handler_text_audio;
    } else {
        audio_irq = dma_irq_handler_gfx_audio;
    }
    return true;
}

void DVHSTX::enable_audio(uint32_t sample_rate) {
    audio_sample_rate = sample_rate;
    audio_setup = &DVHSTX::setup_audio;
}
