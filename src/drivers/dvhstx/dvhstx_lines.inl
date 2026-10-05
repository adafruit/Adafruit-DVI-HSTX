// Line drawing shared by the video-only DMA handlers (dvhstx.cpp) and the
// audio ones (dvhstx_audio.cpp). Included by both; not a public header.
#pragma once

#include "dvhstx.hpp"
#include "font.h"

#ifdef MICROPY_BUILD_TYPE
extern "C" {
void dvhstx_debug(const char *fmt, ...);
}
#elif defined(ARDUINO)
#include <Arduino.h>
#define dvhstx_debug(...) ((void)0)
#else
#include <cstdio>
#define dvhstx_debug printf
#endif

// If changing the font, note this code will not handle glyphs wider than 13 pixels
#define FONT (&intel_one_mono)

extern uint8_t color_lut[8];

static inline __attribute__((always_inline)) uint32_t render_char_line(int c, int y) {
    if (c < 0x20 || c > 0x7e) return 0;
    const lv_font_fmt_txt_glyph_dsc_t* g = &FONT->dsc->glyph_dsc[c - 0x20 + 1];
    const uint8_t *b = FONT->dsc->glyph_bitmap + g->bitmap_index;
    const int ey = y - FONT_HEIGHT + FONT->base_line + g->ofs_y + g->box_h;
    if (ey < 0 || ey >= g->box_h || g->box_w == 0) {
        return 0;
    }
    else {
        int bi = (g->box_w * ey);

        uint32_t bits = (b[bi >> 2] << 24) | (b[(bi >> 2) + 1] << 16) | (b[(bi >> 2) + 2] << 8) | b[(bi >> 2) + 3];
        bits >>= 6 - ((bi & 3) << 1);
        bits &= 0x3ffffff & (0x3ffffff << ((13 - g->box_w) << 1));
        bits >>= g->ofs_x << 1;

        return bits;
    }
}

namespace pimoroni {

// Expand frame buffer row y into the pixel words of a line buffer.
inline __attribute__((always_inline)) void DVHSTX::fill_gfx_line(uint32_t* dst_ptr, int y) {
    if (scanline_cb) {
        scanline_cb(v_scanline, y, dst_ptr);
        return;
    }
    if (line_bytes_per_pixel == 2) {
        uint16_t* src_ptr = (uint16_t*)&frame_buffer_display[y * 2 * (timing_mode->h_active_pixels >> h_repeat_shift)];
        if (h_repeat_shift == 2) {
            for (int i = 0; i < timing_mode->h_active_pixels >> 1; i += 2) {
                uint32_t val = (uint32_t)(*src_ptr++) * 0x10001;
                *dst_ptr++ = val;
                *dst_ptr++ = val;
            }
        }
        else {
            for (int i = 0; i < timing_mode->h_active_pixels >> 1; ++i) {
                uint32_t val = (uint32_t)(*src_ptr++) * 0x10001;
                *dst_ptr++ = val;
            }
        }
    }
    else if (line_bytes_per_pixel == 1) {
        uint8_t* src_ptr = &frame_buffer_display[y * (timing_mode->h_active_pixels >> h_repeat_shift)];
        if (h_repeat_shift == 2) {
            for (int i = 0; i < timing_mode->h_active_pixels >> 2; ++i) {
                uint32_t val = (uint32_t)(*src_ptr++) * 0x01010101;
                *dst_ptr++ = val;
            }
        }
        else {
            for (int i = 0; i < timing_mode->h_active_pixels >> 2; ++i) {
                uint32_t val = ((uint32_t)(*src_ptr++) * 0x0101);
                val |= ((uint32_t)(*src_ptr++) * 0x01010000);
                *dst_ptr++ = val;
            }
        }
    }
    else if (line_bytes_per_pixel == 4) {
        uint8_t* src_ptr = &frame_buffer_display[y * (timing_mode->h_active_pixels >> h_repeat_shift)];
        if (h_repeat_shift == 2) {
            for (int i = 0; i < timing_mode->h_active_pixels; i += 4) {
                uint32_t val = display_palette[*src_ptr++];
                *dst_ptr++ = val;
                *dst_ptr++ = val;
                *dst_ptr++ = val;
                *dst_ptr++ = val;
            }
        } else if (h_repeat_shift == 1) {
            for (int i = 0; i < timing_mode->h_active_pixels; i += 2) {
                uint32_t val = display_palette[*src_ptr++];
                *dst_ptr++ = val;
                *dst_ptr++ = val;
            }
        } else {
            for (int i = 0; i < timing_mode->h_active_pixels; i += 1) {
                uint32_t val = display_palette[*src_ptr++];
                *dst_ptr++ = val;
            }
        }
    }
}

// Render text row y (in scanlines) into the pixel words at line.
inline __attribute__((always_inline)) void DVHSTX::fill_text_line(uint32_t* line, int y) {
    int char_y = y % 24;
    if (line_bytes_per_pixel == 4) {
        uint32_t* dst_ptr = line;
        uint8_t* src_ptr = &frame_buffer_display[(y / 24) * frame_width];
        for (int i = 0; i < frame_width; ++i) {
            *dst_ptr++ = render_char_line(*src_ptr++, char_y);
        }
    }
    else {
        uint8_t* src_ptr = &frame_buffer_display[(y / 24) * frame_width * 2];
        uint32_t* dst_ptr = line;
        for (int i = 0; i < frame_width; i += 2) {
            uint32_t tmp_h, tmp_l;

            uint8_t c = (*src_ptr++ - 0x20);
            uint32_t bits = (c < 95) ? font_cache[c * 24 + char_y] : 0;
            uint8_t attr = *src_ptr++;
            uint32_t bg = color_lut[(attr >> 3) & 7];
            uint32_t colour = color_lut[attr & 7] ^ bg;
            uint32_t bg_xor = bg * 0x3030303;
            if (attr & ATTR_LOW_INTEN) bits = bits & 0xaaaaaaaa;
            if ((attr & ATTR_V_LOW_INTEN) == ATTR_V_LOW_INTEN) bits >>= 1;

            *dst_ptr++ = colour * ((bits >> 6) & 0x3030303) ^ bg_xor;
            *dst_ptr++ = colour * ((bits >> 4) & 0x3030303) ^ bg_xor;
            *dst_ptr++ = colour * ((bits >> 2) & 0x3030303) ^ bg_xor;
            tmp_l = colour * ((bits >> 0) & 0x3030303) ^ bg_xor;

            if (i == frame_width - 1) {
                *dst_ptr++ = tmp_l;
                break;
            }

            c = (*src_ptr++ - 0x20);
            bits = (c < 95) ? font_cache[c * 24 + char_y] : 0;
            attr = *src_ptr++;
            if (attr & ATTR_LOW_INTEN) bits = bits & 0xaaaaaaaa;
            if ((attr & ATTR_V_LOW_INTEN) == ATTR_V_LOW_INTEN) bits >>= 1;
            bg = color_lut[(attr >> 3) & 7] ;
            colour = color_lut[attr & 7] ^ bg;
            bg_xor = bg * 0x3030303;

            tmp_h = colour * ((bits >> 6) & 0x3030303) ^ bg_xor;
            *dst_ptr++ = (tmp_l & 0xffff) | (tmp_h << 16);
            tmp_l = tmp_h >> 16;
            tmp_h = colour * ((bits >> 4) & 0x3030303) ^ bg_xor;
            *dst_ptr++ = (tmp_l & 0xffff) | (tmp_h << 16);
            tmp_l = tmp_h >> 16;
            tmp_h = colour * ((bits >> 2) & 0x3030303) ^ bg_xor;
            *dst_ptr++ = (tmp_l & 0xffff) | (tmp_h << 16);
            tmp_l = tmp_h >> 16;
            tmp_h = colour * ((bits >> 0) & 0x3030303) ^ bg_xor;
            *dst_ptr++ = (tmp_l & 0xffff) | (tmp_h << 16);
        }
        if (y / 24 == cursor_y) {
            uint8_t* dst_ptr = (uint8_t*)line + 14 * cursor_x;
            *dst_ptr++ ^= 0xff;
            *dst_ptr++ ^= 0xff;
            *dst_ptr++ ^= 0xff;
            *dst_ptr++ ^= 0xff;
            *dst_ptr++ ^= 0xff;
            *dst_ptr++ ^= 0xff;
            *dst_ptr++ ^= 0xff;
            *dst_ptr++ ^= 0xff;
            *dst_ptr++ ^= 0xff;
            *dst_ptr++ ^= 0xff;
            *dst_ptr++ ^= 0xff;
            *dst_ptr++ ^= 0xff;
            *dst_ptr++ ^= 0xff;
        }
    }
}

}
