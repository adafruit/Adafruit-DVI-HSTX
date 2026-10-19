// Video from a scanline callback, with no frame buffer, plus a 1 kHz tone.
// Emulators use this to draw each line as the display needs it. The
// callback runs in the video interrupt, so it must be quick and live in RAM.
#include <Adafruit_dvhstx.h>

#if defined(ADAFRUIT_FEATHER_RP2350_HSTX)
DVHSTXPinout pinConfig = ADAFRUIT_FEATHER_RP2350_CFG;
#elif defined(ADAFRUIT_METRO_RP2350)
DVHSTXPinout pinConfig = ADAFRUIT_METRO_RP2350_CFG;
#elif defined(ARDUINO_ADAFRUIT_FRUITJAM_RP2350)
DVHSTXPinout pinConfig = ADAFRUIT_FRUIT_JAM_CFG;
#elif (defined(ARDUINO_RASPBERRY_PI_PICO_2) ||                                 \
       defined(ARDUINO_RASPBERRY_PI_PICO_2W))
DVHSTXPinout pinConfig = ADAFRUIT_HSTXDVIBELL_CFG;
#else
// If your board definition has PIN_CKP and related defines,
// DVHSTX_PINOUT_DEFAULT is available
DVHSTXPinout pinConfig = DVHSTX_PINOUT_DEFAULT;
#endif

// 320x240 picks 640x480 output; the callback draws all 640 pixels per line.
DVHSTXScanline display(pinConfig, DVHSTX_RESOLUTION_320x240);

// Diagonal stripes: each line is the previous one shifted by a pixel.
static void __not_in_flash_func(draw_line)(uint32_t v_scanline,
                                           uint32_t active_line,
                                           uint32_t *dst) {
  (void)v_scanline;
  static const uint16_t colors[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
  for (int x = 0; x < 640; x += 2) {
    uint16_t a = colors[((x + active_line) >> 5) & 3];
    uint16_t b = colors[((x + 1 + active_line) >> 5) & 3];
    *dst++ = a | ((uint32_t)b << 16); // two RGB565 pixels, left one low
  }
}

static int16_t sine[48]; // one cycle of 1 kHz at 48 kHz
static size_t phase;

void setup() {
  for (size_t i = 0; i < count_of(sine); i++) {
    sine[i] = (int16_t)(sinf(i * 2.0f * PI / count_of(sine)) * 6000);
  }
  display.enableAudio(48000);
  if (!display.begin(draw_line)) {
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;)
      digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
  }
}

void loop() {
  int16_t buf[64 * 2];
  size_t n = display.audioAvailableForWrite();
  if (n > 64)
    n = 64;
  for (size_t i = 0; i < n; i++) {
    buf[i * 2] = buf[i * 2 + 1] = sine[(phase + i) % count_of(sine)];
  }
  phase = (phase + display.audioWrite(buf, n)) % count_of(sine);
}
