#pragma once

#include <string.h>

#include "dvi.hpp"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

// DVI HSTX driver for use with Pimoroni PicoGraphics

namespace pimoroni {

  struct DVHSTXPinout {
    uint8_t clk_p, rgb_p[3];
  };

  typedef uint32_t RGB888;

  // Digital Video using HSTX
  // Valid screen modes are:
  //   Pixel doubled: 640x480 (60Hz), 720x480 (60Hz), 720x400 (70Hz), 720x576 (50Hz), 
  //                  800x600 (60Hz), 800x480 (60Hz), 800x450 (60Hz), 960x540 (60Hz), 1024x768 (60Hz)
  //   Pixel doubled or quadrupled: 1280x720 (50Hz)
  //
  // Giving valid resolutions:
  //   320x180, 640x360 (well supported, square pixels on a 16:9 display)
  //   480x270, 400x225 (sometimes supported, square pixels on a 16:9 display)
  //   320x240, 360x240, 360x200, 360x288, 400x300, 512x384 (well supported, but pixels aren't square)
  //   400x240 (sometimes supported, pixels aren't square)
  //
  // Note that the double buffer is in RAM, so 640x360 uses almost all of the available RAM.
  class DVHSTX {
  public:
    static constexpr int PALETTE_SIZE = 256;


    enum Mode {
      MODE_PALETTE = 2,
      MODE_RGB565 = 1,
      MODE_RGB888 = 3,
      MODE_TEXT_MONO = 4,
      MODE_TEXT_RGB111 = 5,
    };

    enum TextColour {
      TEXT_BLACK = 0,
      TEXT_RED,
      TEXT_GREEN,
      TEXT_BLUE,
      TEXT_YELLOW,
      TEXT_MAGENTA,
      TEXT_CYAN,
      TEXT_WHITE,

      BG_BLACK = 0,
      BG_RED = TEXT_RED << 3,
      BG_GREEN = TEXT_GREEN << 3,
      BG_BLUE = TEXT_BLUE << 3,
      BG_YELLOW = TEXT_YELLOW << 3,
      BG_MAGENTA = TEXT_MAGENTA << 3,
      BG_CYAN = TEXT_CYAN << 3,
      BG_WHITE = TEXT_WHITE << 3,

      ATTR_NORMAL_INTEN = 0,
      ATTR_LOW_INTEN = 1 << 6,
      ATTR_V_LOW_INTEN = 1 << 7 | ATTR_LOW_INTEN,
    };    

    //--------------------------------------------------
    // Variables
    //--------------------------------------------------
  protected:
    friend void vsync_callback();

    uint16_t display_width = 320;
    uint16_t display_height = 180;
    uint16_t frame_width = 320;
    uint16_t frame_height = 180;
    uint8_t frame_bytes_per_pixel = 2;
    uint8_t h_repeat = 4;
    uint8_t v_repeat = 4;
    Mode mode = MODE_RGB565;

  public:
    DVHSTX();

    //--------------------------------------------------
    // Methods
    //--------------------------------------------------
    public:
      bool get_single_buffered() { return frame_buffer_display && frame_buffer_display == frame_buffer_back; }
      bool get_double_buffered() { return frame_buffer_display && frame_buffer_display != frame_buffer_back; }

      template<class T>
      T *get_back_buffer() { return (T*)(frame_buffer_back); }
      template<class T>
      T *get_front_buffer() { return (T*)(frame_buffer_display); }

      uint16_t get_width() const { return frame_width; }
      uint16_t get_height() const { return frame_height; }

      RGB888* get_palette();

      bool init(uint16_t width, uint16_t height, Mode mode, bool double_buffered, const DVHSTXPinout &pinout);
      void reset();

      // Wait for vsync and then flip the buffers
      void flip_blocking();

      // Flip immediately without waiting for vsync
      void flip_now();

      void wait_for_vsync();

      // flip_async queues a flip to happen next vsync but returns without blocking.
      // You should call wait_for_flip before doing any more reads or writes, defining sprites, etc.
      void flip_async();
      void wait_for_flip();

      // DMA handlers, should not be called externally
      void gfx_dma_handler();
      void text_dma_handler();

      void set_cursor(int x, int y) { cursor_x = x; cursor_y = y; }
      void cursor_off(void) { cursor_y = -1; }

    private:
      RGB888 palette[PALETTE_SIZE];
      uint8_t* frame_buffer_display;
      uint8_t* frame_buffer_back;
      uint32_t* font_cache = nullptr;

      void display_setup_clock();

      // DMA scanline filling
      uint ch_num = 0;
      int line_num = -1;

      volatile int v_scanline = 2;
      volatile bool flip_next;

      bool inited = false;

      uint32_t* line_buffers;
      const struct dvi_timing* timing_mode;
      int v_inactive_total;
      int v_total_active_lines;

      uint h_repeat_shift;
      uint v_repeat_shift;
      int line_bytes_per_pixel;

      uint32_t* display_palette = nullptr;

      int cursor_x, cursor_y;
  };
}
