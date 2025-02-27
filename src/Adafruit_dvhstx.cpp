#include "Adafruit_dvhstx.h"

int16_t dvhstx_width(DVHSTXResolution r) {
  switch (r) {
  default:
  case DVHSTX_RESOLUTION_320x180:
    return 320;
  case DVHSTX_RESOLUTION_640x360:
    return 640;
  case DVHSTX_RESOLUTION_480x270:
    return 480;
  case DVHSTX_RESOLUTION_400x225:
    return 400;
  case DVHSTX_RESOLUTION_320x240:
    return 320;
  case DVHSTX_RESOLUTION_640x480:
    return 640;
  case DVHSTX_RESOLUTION_360x240:
    return 360;
  case DVHSTX_RESOLUTION_360x200:
    return 360;
  case DVHSTX_RESOLUTION_720x400:
    return 720;
  case DVHSTX_RESOLUTION_360x288:
    return 360;
  case DVHSTX_RESOLUTION_400x300:
    return 400;
  case DVHSTX_RESOLUTION_512x384:
    return 512;
  case DVHSTX_RESOLUTION_400x240:
    return 400;
  }
  return 0;
}

int16_t dvhstx_height(DVHSTXResolution r) {
  switch (r) {
  default:
  case DVHSTX_RESOLUTION_320x180:
    return 180;
  case DVHSTX_RESOLUTION_640x360:
    return 360;
  case DVHSTX_RESOLUTION_480x270:
    return 270;
  case DVHSTX_RESOLUTION_400x225:
    return 225;
  case DVHSTX_RESOLUTION_320x240:
    return 240;
  case DVHSTX_RESOLUTION_640x480:
    return 480;
  case DVHSTX_RESOLUTION_360x240:
    return 240;
  case DVHSTX_RESOLUTION_360x200:
    return 200;
  case DVHSTX_RESOLUTION_720x400:
    return 400;
  case DVHSTX_RESOLUTION_360x288:
    return 288;
  case DVHSTX_RESOLUTION_400x300:
    return 300;
  case DVHSTX_RESOLUTION_512x384:
    return 384;
  case DVHSTX_RESOLUTION_400x240:
    return 240;
  }
}

void DVHSTX16::swap(bool copy_framebuffer) {
  if (!double_buffered) {
    return;
  }
  hstx.flip_blocking();
  if (copy_framebuffer) {
    memcpy(hstx.get_front_buffer<uint8_t>(), hstx.get_back_buffer<uint8_t>(),
           sizeof(uint16_t) * _width * _height);
  }
}
void DVHSTX8::swap(bool copy_framebuffer) {
  if (!double_buffered) {
    return;
  }
  hstx.flip_blocking();
  if (copy_framebuffer) {
    memcpy(hstx.get_front_buffer<uint8_t>(), hstx.get_back_buffer<uint8_t>(),
           sizeof(uint8_t) * _width * _height);
  }
}

void DVHSTXText::clear() {
  std::fill(getBuffer(), getBuffer() + WIDTH * HEIGHT, attr << 8);
}

// Character framebuffer is actually a small GFXcanvas16, so...
size_t DVHSTXText::write(uint8_t c) {
  if (!*this)
    return 0;

  if (c == '\r') { // Carriage return
    cursor_x = 0;
  } else if ((c == '\n') ||
             (c >= 32 &&
              cursor_x >= WIDTH)) { // Newline OR right edge and printing
    cursor_x = 0;
    if (cursor_y >= (HEIGHT - 1)) { // Vert scroll?
      memmove(getBuffer(), getBuffer() + WIDTH,
              WIDTH * (HEIGHT - 1) * sizeof(uint16_t));
      drawFastHLine(0, HEIGHT - 1, WIDTH,
                    ' ' | (attr << 8)); // Clear bottom line
      cursor_y = HEIGHT - 1;
    } else {
      cursor_y++;
    }
  }
  if (c >= 32) {
    drawPixel(cursor_x, cursor_y, (attr << 8) | c);
    cursor_x++;
  }
  sync_cursor_with_hstx();
  return 1;
}
