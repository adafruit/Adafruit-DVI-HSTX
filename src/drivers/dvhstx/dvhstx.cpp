#include <string.h>
#include <pico/stdlib.h>

#if F_CPU != 150000000
#error "Adafruit_DVHSTX controls overclocking (setting CPU frequency to 264MHz). However, the Tools > CPU Speed selector *MUST* be set to 150MHz"
#endif

extern "C" {
#include <pico/lock_core.h>
}

#include <algorithm>
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "hardware/structs/sio.h"

#include "hardware/structs/ioqspi.h"
#include "hardware/vreg.h"
#include "hardware/structs/qmi.h"
#include "hardware/pll.h"
#include "hardware/clocks.h"

#include "dvi.hpp"
#include "dvhstx.hpp"

using namespace pimoroni;

#ifdef MICROPY_BUILD_TYPE
#define FRAME_BUFFER_SIZE (640*360)
__attribute__((section(".uninitialized_data"))) static uint8_t frame_buffer_a[FRAME_BUFFER_SIZE];
__attribute__((section(".uninitialized_data"))) static uint8_t frame_buffer_b[FRAME_BUFFER_SIZE];
#endif

#include "font.h"

// If changing the font, note this code will not handle glyphs wider than 13 pixels
#define FONT (&intel_one_mono)

#ifdef MICROPY_BUILD_TYPE
extern "C" {
void dvhstx_debug(const char *fmt, ...);
}
#elif defined(ARDUINO)
#include <Arduino.h>
// #define dvhstx_debug Serial.printf
#define dvhstx_debug(...) ((void)0)
#else
#include <cstdio>
#define dvhstx_debug printf
#endif

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

// ----------------------------------------------------------------------------
// HSTX command lists

// Lists are padded with NOPs to be >= HSTX FIFO size, to avoid DMA rapidly
// pingponging and tripping up the IRQs.

static const uint32_t vblank_line_vsync_off_src[] = {
    HSTX_CMD_RAW_REPEAT,
    SYNC_V1_H1,
    HSTX_CMD_RAW_REPEAT,
    SYNC_V1_H0,
    HSTX_CMD_RAW_REPEAT,
    SYNC_V1_H1
};
static uint32_t vblank_line_vsync_off[count_of(vblank_line_vsync_off_src)];

static const uint32_t vblank_line_vsync_on_src[] = {
    HSTX_CMD_RAW_REPEAT,
    SYNC_V0_H1,
    HSTX_CMD_RAW_REPEAT,
    SYNC_V0_H0,
    HSTX_CMD_RAW_REPEAT,
    SYNC_V0_H1
};
static uint32_t vblank_line_vsync_on[count_of(vblank_line_vsync_on_src)];

static const uint32_t vactive_line_header_src[] = {
    HSTX_CMD_RAW_REPEAT,
    SYNC_V1_H1,
    HSTX_CMD_RAW_REPEAT,
    SYNC_V1_H0,
    HSTX_CMD_RAW_REPEAT,
    SYNC_V1_H1,
    HSTX_CMD_TMDS      
};
static uint32_t vactive_line_header[count_of(vactive_line_header_src)];

static const uint32_t vactive_text_line_header_src[] = {
    HSTX_CMD_RAW_REPEAT,
    SYNC_V1_H1,
    HSTX_CMD_RAW_REPEAT,
    SYNC_V1_H0,
    HSTX_CMD_RAW_REPEAT,
    SYNC_V1_H1,
    HSTX_CMD_RAW | 6,
    BLACK_PIXEL_A,
    BLACK_PIXEL_B,
    BLACK_PIXEL_A,
    BLACK_PIXEL_B,
    BLACK_PIXEL_A,
    BLACK_PIXEL_B,
    HSTX_CMD_TMDS
};
static uint32_t vactive_text_line_header[count_of(vactive_text_line_header_src)];

#define NUM_FRAME_LINES 2
#define NUM_CHANS 3

static DVHSTX* display = nullptr;

// ----------------------------------------------------------------------------
// DMA logic

void __scratch_x("display") dma_irq_handler() {
    display->gfx_dma_handler();
}

void __scratch_x("display") DVHSTX::gfx_dma_handler() {
    // ch_num indicates the channel that just finished, which is the one
    // we're about to reload.
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1u << ch_num;
    if (++ch_num == NUM_CHANS) ch_num = 0;

    if (v_scanline >= timing_mode->v_front_porch && v_scanline < (timing_mode->v_front_porch + timing_mode->v_sync_width)) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if (v_scanline < v_inactive_total) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else {
        const int y = (v_scanline - v_inactive_total) >> v_repeat_shift;
        const int new_line_num = (v_repeat_shift == 0) ? ch_num : (y & (NUM_FRAME_LINES - 1));
        const uint line_buf_total_len = ((timing_mode->h_active_pixels * line_bytes_per_pixel) >> 2) + count_of(vactive_line_header);

        ch->read_addr = (uintptr_t)&line_buffers[new_line_num * line_buf_total_len];
        ch->transfer_count = line_buf_total_len;

        // Fill line buffer
        if (line_num != new_line_num)
        {
            line_num = new_line_num;
            uint32_t* dst_ptr = &line_buffers[line_num * line_buf_total_len + count_of(vactive_line_header)];

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
    }

    if (++v_scanline == v_total_active_lines) {
        v_scanline = 0;
        line_num = -1;
        if (flip_next) {
            flip_next = false;
            display->flip_now();
        }
        __sev();
    }
}

void __scratch_x("display") dma_irq_handler_text() {
    display->text_dma_handler();
}

uint8_t color_lut[8] = {
#define CLUT_ENTRY(i) (i)
#define CLUT_R CLUT_ENTRY(1 << 6)
#define CLUT_G CLUT_ENTRY(1 << 3)
#define CLUT_B CLUT_ENTRY(1 << 0)
    0,
    CLUT_R,
    CLUT_G,
    CLUT_R | CLUT_G,
    CLUT_B,
    CLUT_R | CLUT_B,
    CLUT_G | CLUT_B,
    CLUT_R | CLUT_G | CLUT_B,
#undef CLUT_R
#undef CLUT_G
#undef CLUT_B
#undef CLUT_ENTRY
};

void __scratch_x("display") DVHSTX::text_dma_handler() {
    // ch_num indicates the channel that just finished, which is the one
    // we're about to reload.
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1u << ch_num;
    if (++ch_num == NUM_CHANS) ch_num = 0;

    if (v_scanline >= timing_mode->v_front_porch && v_scanline < (timing_mode->v_front_porch + timing_mode->v_sync_width)) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if (v_scanline < v_inactive_total) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else {
        const int y = (v_scanline - v_inactive_total);
        const uint line_buf_total_len = (frame_width * line_bytes_per_pixel + 3) / 4 + count_of(vactive_text_line_header);

        ch->read_addr = (uintptr_t)&line_buffers[ch_num * line_buf_total_len];
        ch->transfer_count = line_buf_total_len;

        // Fill line buffer
        int char_y = y % 24;
        if (line_bytes_per_pixel == 4) {
            uint32_t* dst_ptr = &line_buffers[ch_num * line_buf_total_len + count_of(vactive_text_line_header)];
            uint8_t* src_ptr = &frame_buffer_display[(y / 24) * frame_width];
            for (int i = 0; i < frame_width; ++i) {
                *dst_ptr++ = render_char_line(*src_ptr++, char_y);
            }
        }
        else {
            uint8_t* src_ptr = &frame_buffer_display[(y / 24) * frame_width * 2];
            uint32_t* dst_ptr = &line_buffers[ch_num * line_buf_total_len + count_of(vactive_text_line_header)];
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
                uint8_t* dst_ptr = (uint8_t*)&line_buffers[ch_num * line_buf_total_len + count_of(vactive_text_line_header)] + 14 * cursor_x;
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

    if (++v_scanline == v_total_active_lines) {
        v_scanline = 0;
        line_num = -1;
        if (flip_next) {
            flip_next = false;
            display->flip_now();
        }
        __sev();
    }
}

// ----------------------------------------------------------------------------
// Experimental clock config

#ifndef MICROPY_BUILD_TYPE
static void __no_inline_not_in_flash_func(set_qmi_timing)() {
    // Make sure flash is deselected - QMI doesn't appear to have a busy flag(!)
    while ((ioqspi_hw->io[1].status & IO_QSPI_GPIO_QSPI_SS_STATUS_OUTTOPAD_BITS) != IO_QSPI_GPIO_QSPI_SS_STATUS_OUTTOPAD_BITS)
        ;

    qmi_hw->m[0].timing = 0x40000202;
    //qmi_hw->m[0].timing = 0x40000101;
    // Force a read through XIP to ensure the timing is applied
    volatile uint32_t* ptr = (volatile uint32_t*)0x14000000;
    (void) *ptr;
}
#endif

extern "C" void __no_inline_not_in_flash_func(display_setup_clock_preinit)() {
    uint32_t intr_stash = save_and_disable_interrupts();

    // Before messing with clock speeds ensure QSPI clock is nice and slow
    hw_write_masked(&qmi_hw->m[0].timing, 6, QMI_M0_TIMING_CLKDIV_BITS);

    // We're going to go fast, boost the voltage a little
    vreg_set_voltage(VREG_VOLTAGE_1_15);

    // Force a read through XIP to ensure the timing is applied before raising the clock rate
    volatile uint32_t* ptr = (volatile uint32_t*)0x14000000;
    (void) *ptr;

    // Before we touch PLLs, switch sys and ref cleanly away from their aux sources.
    hw_clear_bits(&clocks_hw->clk[clk_sys].ctrl, CLOCKS_CLK_SYS_CTRL_SRC_BITS);
    while (clocks_hw->clk[clk_sys].selected != 0x1)
        tight_loop_contents();
    hw_write_masked(&clocks_hw->clk[clk_ref].ctrl, CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC, CLOCKS_CLK_REF_CTRL_SRC_BITS);
    while (clocks_hw->clk[clk_ref].selected != 0x4)
        tight_loop_contents();

    // Stop the other clocks so we don't worry about overspeed
    clock_stop(clk_usb);
    clock_stop(clk_adc);
    clock_stop(clk_peri);
    clock_stop(clk_hstx);

    // Set USB PLL to 528MHz
    pll_init(pll_usb, PLL_COMMON_REFDIV, 1584 * MHZ, 3, 1);

    const uint32_t usb_pll_freq = 528 * MHZ;

    // CLK SYS = PLL USB 528MHz / 2 = 264MHz
    clock_configure(clk_sys,
                    CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    usb_pll_freq, usb_pll_freq / 2);

    // CLK PERI = PLL USB 528MHz / 4 = 132MHz
    clock_configure(clk_peri,
                    0, // Only AUX mux on ADC
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    usb_pll_freq, usb_pll_freq / 4);

    // CLK USB = PLL USB 528MHz / 11 = 48MHz
    clock_configure(clk_usb,
                    0, // No GLMUX
                    CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    usb_pll_freq,
                    USB_CLK_KHZ * KHZ);

    // CLK ADC = PLL USB 528MHz / 11 = 48MHz
    clock_configure(clk_adc,
                    0, // No GLMUX
                    CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    usb_pll_freq,
                    USB_CLK_KHZ * KHZ);

    // Now we are running fast set fast QSPI clock and read delay
    // On MicroPython this is setup by main.
#ifndef MICROPY_BUILD_TYPE
    set_qmi_timing();
#endif

    restore_interrupts(intr_stash);
}

#ifndef MICROPY_BUILD_TYPE
// Trigger clock setup early - on MicroPython this is done by a hook in main.
namespace {
    class DV_preinit {
        public:
        DV_preinit() {
            display_setup_clock_preinit();
        }
    };
    DV_preinit dv_preinit __attribute__ ((init_priority (101))) ;
}
#endif

void DVHSTX::display_setup_clock() {
    const uint32_t dvi_clock_khz = timing_mode->bit_clk_khz >> 1;
    uint vco_freq, post_div1, post_div2;
    if (!check_sys_clock_khz(dvi_clock_khz, &vco_freq, &post_div1, &post_div2))
        panic("System clock of %u kHz cannot be exactly achieved", dvi_clock_khz);
    const uint32_t freq = vco_freq / (post_div1 * post_div2);

    // Set the sys PLL to the requested freq
    pll_init(pll_sys, PLL_COMMON_REFDIV, vco_freq, post_div1, post_div2);

    // CLK HSTX = Requested freq
    clock_configure(clk_hstx,
                    0,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    freq, freq);
}

RGB888* DVHSTX::get_palette()
{
    return palette;
}

DVHSTX::DVHSTX()
{
    // Always use the bottom channels
    dma_claim_mask((1 << NUM_CHANS) - 1);
}

bool DVHSTX::init(uint16_t width, uint16_t height, Mode mode_, bool double_buffered, const DVHSTXPinout &pinout)
{
    if (inited) reset();

    cursor_y = -1;
    ch_num = 0;
    line_num = -1;
    v_scanline = 2;
    flip_next = false;

    display_width = width;
    display_height = height;
    frame_width = width;
    frame_height = height;
    mode = mode_;

    timing_mode = nullptr;
    if (mode == MODE_TEXT_MONO || mode == MODE_TEXT_RGB111) {
        width = 1280;
        height = 720;
        display_width = 91;
        frame_width = 91;
        display_height = 30;
        frame_height = 30;
        h_repeat_shift = 0;
        v_repeat_shift = 0;
        timing_mode = &dvi_timing_1280x720p_rb_50hz;
    }
    else if (width == 320 && height == 180) {
        h_repeat_shift = 2;
        v_repeat_shift = 2;
        timing_mode = &dvi_timing_1280x720p_rb_50hz;
    }
    else if (width == 640 && height == 360) {
        h_repeat_shift = 1;
        v_repeat_shift = 1;
        timing_mode = &dvi_timing_1280x720p_rb_50hz;
    }
    else if (width == 480 && height == 270) {
        h_repeat_shift = 2;
        v_repeat_shift = 2;
        timing_mode = &dvi_timing_1920x1080p_rb2_30hz;
    }
    else
    {
        uint16_t full_width = display_width;
        uint16_t full_height = display_height;
        h_repeat_shift = 0;
        v_repeat_shift = 0;

        if (display_width < 640) {
            h_repeat_shift = 1;
            full_width *= 2;
        }

        if (display_height < 400) {
            v_repeat_shift = 1;
            full_height *= 2;
        }

        if (full_width == 640) {
            if (full_height == 480) timing_mode = &dvi_timing_640x480p_60hz;
        }
        else if (full_width == 720) {
            if (full_height == 480) timing_mode = &dvi_timing_720x480p_60hz;
            else if (full_height == 400) timing_mode = &dvi_timing_720x400p_70hz;
            else if (full_height == 576) timing_mode = &dvi_timing_720x576p_50hz;
        }
        else if (full_width == 800) {
            if (full_height == 600) timing_mode = &dvi_timing_800x600p_60hz;
            else if (full_height == 480) timing_mode = &dvi_timing_800x480p_60hz;
            else if (full_height == 450) timing_mode = &dvi_timing_800x450p_60hz;
        }
        else if (full_width == 960) {
            if (full_height == 540) timing_mode = &dvi_timing_960x540p_60hz;
        }
        else if (full_width == 1024) {
            if (full_height == 768) timing_mode = &dvi_timing_1024x768_rb_60hz;
        }
    }

    if (!timing_mode) {
        dvhstx_debug("Unsupported resolution %dx%d", width, height);
        return false;
    }

    display = this;
    display_palette = get_palette();
    
    dvhstx_debug("Setup clock\n");
    display_setup_clock();

#if !defined(MICROPY_BUILD_TYPE) && !defined(ARDUINO)
    stdio_init_all();
#endif
    dvhstx_debug("Clock setup done\n");

    v_inactive_total = timing_mode->v_front_porch + timing_mode->v_sync_width + timing_mode->v_back_porch;
    v_total_active_lines = v_inactive_total + timing_mode->v_active_lines;
    v_repeat = 1 << v_repeat_shift;
    h_repeat = 1 << h_repeat_shift;

    memcpy(vblank_line_vsync_off, vblank_line_vsync_off_src, sizeof(vblank_line_vsync_off_src));
    vblank_line_vsync_off[0] |= timing_mode->h_front_porch;
    vblank_line_vsync_off[2] |= timing_mode->h_sync_width;
    vblank_line_vsync_off[4] |= timing_mode->h_back_porch + timing_mode->h_active_pixels;

    memcpy(vblank_line_vsync_on, vblank_line_vsync_on_src, sizeof(vblank_line_vsync_on_src));
    vblank_line_vsync_on[0] |= timing_mode->h_front_porch;
    vblank_line_vsync_on[2] |= timing_mode->h_sync_width;
    vblank_line_vsync_on[4] |= timing_mode->h_back_porch + timing_mode->h_active_pixels;

    memcpy(vactive_line_header, vactive_line_header_src, sizeof(vactive_line_header_src));
    vactive_line_header[0] |= timing_mode->h_front_porch;
    vactive_line_header[2] |= timing_mode->h_sync_width;
    vactive_line_header[4] |= timing_mode->h_back_porch;
    vactive_line_header[6] |= timing_mode->h_active_pixels;

    memcpy(vactive_text_line_header, vactive_text_line_header_src, sizeof(vactive_text_line_header_src));
    vactive_text_line_header[0] |= timing_mode->h_front_porch;
    vactive_text_line_header[2] |= timing_mode->h_sync_width;
    vactive_text_line_header[4] |= timing_mode->h_back_porch;
    vactive_text_line_header[7+6] |= timing_mode->h_active_pixels - 6;

    switch (mode) {
    case MODE_RGB565:
        frame_bytes_per_pixel = 2;
        line_bytes_per_pixel = 2;
        break;
    case MODE_PALETTE:
        frame_bytes_per_pixel = 1;
        line_bytes_per_pixel = 4;
        break;
    case MODE_RGB888:
        frame_bytes_per_pixel = 4;
        line_bytes_per_pixel = 4;
        break;
    case MODE_TEXT_MONO:
        frame_bytes_per_pixel = 1;
        line_bytes_per_pixel = 4;
        break;
    case MODE_TEXT_RGB111:
        frame_bytes_per_pixel = 2;
        line_bytes_per_pixel = 14;
        break;
    default:
        dvhstx_debug("Unsupported mode %d", (int)mode);
        return false;
    }

#ifdef MICROPY_BUILD_TYPE
    if (frame_width * frame_height * frame_bytes_per_pixel > sizeof(frame_buffer_a)) {
        panic("Frame buffer too large");
    }

    frame_buffer_display = frame_buffer_a;
    frame_buffer_back = double_buffered ? frame_buffer_b : frame_buffer_a;
#else
    frame_buffer_display = (uint8_t*)malloc(frame_width * frame_height * frame_bytes_per_pixel);
    frame_buffer_back = double_buffered ? (uint8_t*)malloc(frame_width * frame_height * frame_bytes_per_pixel) : frame_buffer_display;
#endif
    memset(frame_buffer_display, 0, frame_width * frame_height * frame_bytes_per_pixel);
    memset(frame_buffer_back, 0, frame_width * frame_height * frame_bytes_per_pixel);

    memset(palette, 0, PALETTE_SIZE * sizeof(palette[0]));

    frame_buffer_display = frame_buffer_display;
    dvhstx_debug("Frame buffers inited\n");

    const bool is_text_mode = (mode == MODE_TEXT_MONO || mode == MODE_TEXT_RGB111);
    const int frame_pixel_words = (frame_width * h_repeat * line_bytes_per_pixel + 3) >> 2;
    const int frame_line_words = frame_pixel_words + (is_text_mode ? count_of(vactive_text_line_header) : count_of(vactive_line_header));
    const int frame_lines = (v_repeat == 1) ? NUM_CHANS : NUM_FRAME_LINES;
    line_buffers = (uint32_t*)malloc(frame_line_words * 4 * frame_lines);

    for (int i = 0; i < frame_lines; ++i)
    {
        if (is_text_mode) memcpy(&line_buffers[i * frame_line_words], vactive_text_line_header, count_of(vactive_text_line_header) * sizeof(uint32_t));
        else memcpy(&line_buffers[i * frame_line_words], vactive_line_header, count_of(vactive_line_header) * sizeof(uint32_t));
    }

    if (mode == MODE_TEXT_RGB111) {
        auto scramble = [](uint32_t b) {
            auto take = [b](int shift1, int shift2) {
                return ((b >> shift1) & 3) << shift2;
            };
            return
                take( 0,  0) |
                take( 2, 26) |
                take( 4, 18) |
                take( 6, 10) |
                take( 8,  2) |
                take(10, 28) |
                take(12, 20) |
                take(14, 12) |
                take(16,  4) |
                take(18, 30) |
                take(20, 22) |
                take(22, 14) |
                take(24,  6) |
                take(26, 28);

        };
        // Need to pre-render the font to RAM to be fast enough.
        font_cache = (uint32_t*)malloc(4 * FONT->line_height * 96);
        uint32_t* font_cache_ptr = font_cache;
        for (int c = 0x20; c < 128; ++c) {
            for (int y = 0; y < FONT->line_height; ++y) {
                *font_cache_ptr++ = scramble(render_char_line(c, y));
            }
        }
    }

    // Ensure HSTX FIFO is clear
    reset_block_num(RESET_HSTX);
    sleep_us(10);
    unreset_block_num_wait_blocking(RESET_HSTX);
    sleep_us(10);

    switch (mode) {
    case MODE_RGB565:
        // Configure HSTX's TMDS encoder for RGB565
        hstx_ctrl_hw->expand_tmds =
            4  << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |
            8 << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB   |
            5  << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
            3  << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB   |
            4  << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
            29 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;

        // Pixels (TMDS) come in 2 16-bit chunks. Control symbols (RAW) are an
        // entire 32-bit word.
        hstx_ctrl_hw->expand_shift =
            2 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
            16 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
            1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
            0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;
        break;

    case MODE_PALETTE:
        // Configure HSTX's TMDS encoder for RGB888
        hstx_ctrl_hw->expand_tmds =
            7  << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |
            16 << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB   |
            7  << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
            8  << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB   |
            7  << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
            0  << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;

        // Pixels and control symbols (RAW) are an
        // entire 32-bit word.
        hstx_ctrl_hw->expand_shift =
            1 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
            0 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
            1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
            0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;
        break;

    case MODE_TEXT_MONO:
        // Configure HSTX's TMDS encoder for 2bpp
        hstx_ctrl_hw->expand_tmds =
            1  << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |
            18 << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB   |
            1  << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
            18  << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB   |
            1  << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
            18  << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;

        // Pixels and control symbols (RAW) are an
        // entire 32-bit word.
        hstx_ctrl_hw->expand_shift =
            14 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
            30 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
            1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
            0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;
        break;

    case MODE_TEXT_RGB111:
        // Configure HSTX's TMDS encoder for RGB222
        hstx_ctrl_hw->expand_tmds =
            1  << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |
            0 << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB   |
            1  << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
            29  << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB   |
            1  << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
            26 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;

        // Pixels (TMDS) come in 4 8-bit chunks. Control symbols (RAW) are an
        // entire 32-bit word.
        hstx_ctrl_hw->expand_shift =
            4 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
            8 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
            1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
            0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;
        break;

    default:
        dvhstx_debug("Unsupported mode %d", (int)mode);
        return false;
    }

    // Serial output config: clock period of 5 cycles, pop from command
    // expander every 5 cycles, shift the output shiftreg by 2 every cycle.
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_EXPAND_EN_BITS |
        5u << HSTX_CTRL_CSR_CLKDIV_LSB |
        5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
        2u << HSTX_CTRL_CSR_SHIFT_LSB |
        HSTX_CTRL_CSR_EN_BITS; 

    // HSTX outputs 0 through 7 appear on GPIO 12 through 19.
    constexpr int HSTX_FIRST_PIN = 12;
    // Assign clock pair to two neighbouring pins:
    hstx_ctrl_hw->bit[(pinout.clk_p    ) - HSTX_FIRST_PIN] = HSTX_CTRL_BIT0_CLK_BITS;
    hstx_ctrl_hw->bit[(pinout.clk_p ^ 1) - HSTX_FIRST_PIN] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    for (uint lane = 0; lane < 3; ++lane) {
        // For each TMDS lane, assign it to the correct GPIO pair based on the
        // desired pinout:
        int bit = pinout.rgb_p[lane];
        // Output even bits during first half of each HSTX cycle, and odd bits
        // during second half. The shifter advances by two bits each cycle.
        uint32_t lane_data_sel_bits =
            (lane * 10    ) << HSTX_CTRL_BIT0_SEL_P_LSB |
            (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
        // The two halves of each pair get identical data, but one pin is inverted.
        hstx_ctrl_hw->bit[(bit    ) - HSTX_FIRST_PIN] = lane_data_sel_bits;
        hstx_ctrl_hw->bit[(bit ^ 1) - HSTX_FIRST_PIN] = lane_data_sel_bits | HSTX_CTRL_BIT0_INV_BITS;
    }

    for (int i = 12; i <= 19; ++i) {
        gpio_set_function(i, GPIO_FUNC_HSTX);
        gpio_set_drive_strength(i, GPIO_DRIVE_STRENGTH_4MA);
    }

    dvhstx_debug("GPIO configured\n");

    // The channels are set up identically, to transfer a whole scanline and
    // then chain to the next channel. Each time a channel finishes, we
    // reconfigure the one that just finished, meanwhile the other channel(s)
    // are already making progress.
    // Using just 2 channels was insufficient to avoid issues with the IRQ.
    dma_channel_config c;
    c = dma_channel_get_default_config(0);
    channel_config_set_chain_to(&c, 1);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(
        0,
        &c,
        &hstx_fifo_hw->fifo,
        vblank_line_vsync_off,
        count_of(vblank_line_vsync_off),
        false
    );
    c = dma_channel_get_default_config(1);
    channel_config_set_chain_to(&c, 2);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(
        1,
        &c,
        &hstx_fifo_hw->fifo,
        vblank_line_vsync_off,
        count_of(vblank_line_vsync_off),
        false
    );
    for (int i = 2; i < NUM_CHANS; ++i) {
        c = dma_channel_get_default_config(i);
        channel_config_set_chain_to(&c, (i+1) % NUM_CHANS);
        channel_config_set_dreq(&c, DREQ_HSTX);
        dma_channel_configure(
            i,
            &c,
            &hstx_fifo_hw->fifo,
            vblank_line_vsync_off,
            count_of(vblank_line_vsync_off),
            false
        );
    }

    dvhstx_debug("DMA channels claimed\n");

    dma_hw->intr = (1 << NUM_CHANS) - 1;
    dma_hw->ints2 = (1 << NUM_CHANS) - 1;
    dma_hw->inte2 = (1 << NUM_CHANS) - 1;
    if (is_text_mode) irq_set_exclusive_handler(DMA_IRQ_2, dma_irq_handler_text);
    else irq_set_exclusive_handler(DMA_IRQ_2, dma_irq_handler);
    irq_set_priority(DMA_IRQ_2, PICO_HIGHEST_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_2, true);

    dma_channel_start(0);

    dvhstx_debug("DVHSTX started\n");

    inited = true;
    return true;
}

void DVHSTX::reset() {
    if (!inited) return;
    inited = false;

    hstx_ctrl_hw->csr = 0;

    irq_set_enabled(DMA_IRQ_2, false);
    irq_remove_handler(DMA_IRQ_2, irq_get_exclusive_handler(DMA_IRQ_2));

    for (int i = 0; i < NUM_CHANS; ++i)
        dma_channel_abort(i);

    if (font_cache) {
        free(font_cache);
        font_cache = nullptr;
    }
    free(line_buffers);
    line_buffers = nullptr;

#ifndef MICROPY_BUILD_TYPE
    free(frame_buffer_display);
    if (frame_buffer_display != frame_buffer_back) {
        free(frame_buffer_back);
}
    frame_buffer_display = frame_buffer_back = nullptr;
#endif
}

void DVHSTX::flip_blocking() {
    if (get_single_buffered())
        return;
    wait_for_vsync();
    flip_now();
}

void DVHSTX::flip_now() {
    if (get_single_buffered())
        return;
    std::swap(frame_buffer_display, frame_buffer_back);
}

void DVHSTX::wait_for_vsync() {
    while (v_scanline >= timing_mode->v_front_porch) __wfe();
}

void DVHSTX::flip_async() {
    if (get_single_buffered())
        return;
    flip_next = true;
}

void DVHSTX::wait_for_flip() {
    if (get_single_buffered())
        return;
    while (flip_next) __wfe();
}
