// 1-bit Adafruit_GFX-compatible framebuffer for RP2350 HSTX.
// Each pixel is 0 or 1, and setColor() picks the color shown for each,
// so 640x480 needs only 38,400 bytes.

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

DVHSTX1 display(pinConfig, DVHSTX_RESOLUTION_640x480);

const uint32_t colors[][2] = {
    {0x000000, 0xFFFFFF}, // white on black
    {0x000000, 0xFFB000}, // amber
    {0x000000, 0x33FF33}, // green
    {0x0000AA, 0xFFFFFF}, // white on blue
};

void setup() {
  Serial.begin(115200);
  if (!display.begin()) { // Blink LED if insufficient RAM
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;)
      digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
  }
  Serial.println("display initialized");

  display.drawRect(0, 0, display.width(), display.height(), 1);
  display.setTextColor(1);
  display.setTextSize(4);
  display.setCursor(40, 40);
  display.println("1-bit mode");
  for (int r = 10; r < display.height() / 3; r += 12)
    display.drawCircle(display.width() / 2, display.height() * 5 / 8, r, 1);
}

void loop() {
  // Changing a color recolors the whole screen without redrawing it.
  for (auto &c : colors) {
    display.setColor(0, c[0]);
    display.setColor(1, c[1]);
    delay(2000);
  }
}
