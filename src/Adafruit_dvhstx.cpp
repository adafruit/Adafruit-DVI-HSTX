#include "Adafruit_dvhstx.h"

extern "C" {
#include "drivers/dvhstx/audio_config.h"
#include "pico_hdmi/hstx_data_island_queue.h"
#include "pico_hdmi/hstx_packet.h"
}

// audioAvailableForWrite() offers room up to this many packets, about 17 ms
// at 48 kHz, of the 256 the queue holds.
#define DVHSTX_AUDIO_QUEUE_TARGET 200
#define DVHSTX_AUDIO_FRAMES_PER_PACKET 4

void dvhstx_enable_audio(pimoroni::DVHSTX &hstx, uint32_t sample_rate) {
  hstx.enable_audio(sample_rate);
}

size_t DVHSTXAudio::audioAvailableForWrite() {
  if (!_audio_running)
    return 0;
  uint32_t level = hstx_di_queue_get_level();
  if (level >= DVHSTX_AUDIO_QUEUE_TARGET)
    return 0;
  return (DVHSTX_AUDIO_QUEUE_TARGET - level) * DVHSTX_AUDIO_FRAMES_PER_PACKET;
}

size_t DVHSTXAudio::audioWrite(const int16_t *lr, size_t frames) {
  if (!_audio_running)
    return 0;
  const bool in_hsync = hstx_di_queue_get_hsync_active();
  size_t done = 0;
  while (frames - done >= DVHSTX_AUDIO_FRAMES_PER_PACKET) {
    audio_sample_t samples[DVHSTX_AUDIO_FRAMES_PER_PACKET];
    for (int i = 0; i < DVHSTX_AUDIO_FRAMES_PER_PACKET; i++) {
      samples[i].left = lr[(done + i) * 2];
      samples[i].right = lr[(done + i) * 2 + 1];
    }
    hstx_packet_t packet;
    int next = hstx_packet_set_audio_samples_cs_rate(
        &packet, samples, DVHSTX_AUDIO_FRAMES_PER_PACKET, _audio_frame,
        _audio_rate);
    hstx_data_island_t island;
    hstx_encode_data_island(&island, &packet, false, in_hsync);
    if (!hstx_di_queue_push(&island))
      break;
    _audio_frame = next;
    done += DVHSTX_AUDIO_FRAMES_PER_PACKET;
  }
  return done;
}

uint32_t DVHSTXAudio::audioUnderruns() {
  return _audio_running ? hstx_di_queue_silence_count : 0;
}

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
    memcpy(hstx.get_back_buffer<uint8_t>(), hstx.get_front_buffer<uint8_t>(),
           sizeof(uint16_t) * _width * _height);
  }
  buffer = hstx.get_back_buffer<uint16_t>();
}
void DVHSTX8::swap(bool copy_framebuffer) {
  if (!double_buffered) {
    return;
  }
  hstx.flip_blocking();
  if (copy_framebuffer) {
    memcpy(hstx.get_back_buffer<uint8_t>(), hstx.get_front_buffer<uint8_t>(),
           sizeof(uint8_t) * _width * _height);
  }
  buffer = hstx.get_back_buffer<uint8_t>();
}
void DVHSTXText::swap(bool copy_framebuffer) {
  if (!double_buffered) {
    return;
  }
  hstx.flip_blocking();
  if (copy_framebuffer) {
    memcpy(hstx.get_back_buffer<uint8_t>(), hstx.get_front_buffer<uint8_t>(),
           sizeof(uint16_t) * _width * _height);
  }
  buffer = hstx.get_back_buffer<uint16_t>();
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
