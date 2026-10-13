// Text mode can show all 256 code page 437 glyphs, including the line and
// block characters for drawing boxes. print() skips bytes below 32, so
// draw those with drawPixel() as below.

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

DVHSTXText display(pinConfig);

// Put byte c at column x, row y.
void put(int x, int y, uint8_t c, uint8_t attr = TextColor::TEXT_WHITE) {
  display.drawPixel(x, y, (attr << 8) | c);
}

void setup() {
  if (!display.begin()) { // Blink LED if insufficient RAM
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;)
      digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
  }
  display.clear();

  // A 16x16 chart: row is the high hex digit, column the low one.
  const char hex[] = "0123456789ABCDEF";
  for (int i = 0; i < 16; i++) {
    put(4 + i * 3, 1, hex[i], TextColor::TEXT_GREEN);
    put(1, 2 + i, hex[i], TextColor::TEXT_GREEN);
  }
  for (int c = 0; c < 256; c++)
    put(4 + (c & 15) * 3, 2 + (c >> 4), c);

  // A double line box around a message.
  const int x0 = 56, y0 = 6, w = 26, h = 5;
  put(x0, y0, 0xC9);
  put(x0 + w - 1, y0, 0xBB);
  put(x0, y0 + h - 1, 0xC8);
  put(x0 + w - 1, y0 + h - 1, 0xBC);
  for (int x = 1; x < w - 1; x++) {
    put(x0 + x, y0, 0xCD);
    put(x0 + x, y0 + h - 1, 0xCD);
  }
  for (int y = 1; y < h - 1; y++) {
    put(x0, y0 + y, 0xBA);
    put(x0 + w - 1, y0 + y, 0xBA);
  }
  display.setCursor(x0 + 3, y0 + 2);
  display.print("Code page 437");
  put(x0 + 18, y0 + 2, 0x03, TextColor::TEXT_RED);
  put(x0 + 20, y0 + 2, 0x0E, TextColor::TEXT_GREEN);
}

void loop() {}
