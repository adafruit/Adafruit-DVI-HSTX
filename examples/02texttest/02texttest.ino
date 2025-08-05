// Text mode demo
// With apologies to Stanley Kubrick et al
#include <Adafruit_dvhstx.h>

#if defined(ADAFRUIT_FEATHER_RP2350_HSTX)
DVHSTXPinout pinConfig = ADAFRUIT_FEATHER_RP2350_CFG;
#elif defined(ADAFRUIT_METRO_RP2350)
DVHSTXPinout pinConfig = ADAFRUIT_METRO_RP2350_CFG;
#elif defined(ARDUINO_ADAFRUIT_FRUITJAM_RP2350)
DVHSTXPinout pinConfig = ADAFRUIT_FRUIT_JAM_CFG;
#elif (defined(ARDUINO_RASPBERRY_PI_PICO_2) || defined(ARDUINO_RASPBERRY_PI_PICO_2W))
DVHSTXPinout pinConfig = ADAFRUIT_HSTXDVIBELL_CFG;
#else
// If your board definition has PIN_CKP and related defines,
// DVHSTX_PINOUT_DEFAULT is available
DVHSTXPinout pinConfig = DVHSTX_PINOUT_DEFAULT;
#endif


DVHSTXText display(pinConfig);
// If you get the message "error: 'DVHSTX_PINOUT_DEFAULTx' was not declared"
// then you need to give the pins numbers explicitly, like the example below.
// The order is: {CKP, D0P, D1P, D2P}.
//
// DVHSTXText display({12, 14, 16, 18});

const static TextColor colors[] = {
    TextColor::TEXT_BLACK, TextColor::TEXT_RED,    TextColor::TEXT_GREEN,
    TextColor::TEXT_BLUE,  TextColor::TEXT_YELLOW, TextColor::TEXT_MAGENTA,
    TextColor::TEXT_CYAN,  TextColor::TEXT_WHITE,
};

const static TextColor background_colors[] = {
    TextColor::BG_BLACK, TextColor::BG_RED,    TextColor::BG_GREEN,
    TextColor::BG_BLUE,  TextColor::BG_YELLOW, TextColor::BG_MAGENTA,
    TextColor::BG_CYAN,  TextColor::BG_WHITE,
};

const static TextColor intensity[] = {TextColor::ATTR_NORMAL_INTEN,
                                      TextColor::ATTR_LOW_INTEN,
                                      TextColor::ATTR_V_LOW_INTEN};

void setup() {
  Serial.begin(115200);
  if (!display.begin()) { // Blink LED if insufficient RAM
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;)
      digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
  }
  display.setColor(TextColor::TEXT_BLACK, TextColor::BG_WHITE);
  display.clear();
  display.showCursor();
  display.print("display initialized (black on white background)\n\n\n\n\n");
  display.println("line wrap test. one line should be full of 'w's and the "
                  "next line should start 'xy'.");
  for (int i = 0; i < display.width(); i++)
    display.write('w');
  display.println("xy");
  display.println("\n\nAttribute test\n");
  display.print("   ");
  for (int d : background_colors) {
    display.printf(" %d ri vli ", (int)d >> 3);
  }
  display.write('\n');
  for (TextColor c : colors) {
    display.printf(" %d ", (int)c);
    for (TextColor d : background_colors) {
      display.setColor(c, d);
      display.write('*');
      display.write('*');
      display.write('*');
      display.setColor(c, d, TextColor::ATTR_LOW_INTEN);
      display.write('*');
      display.write('*');
      display.write('*');
      display.setColor(c, d, TextColor::ATTR_V_LOW_INTEN);
      display.write('*');
      display.write('*');
      display.write('*');
      display.setColor(TextColor::TEXT_BLACK, TextColor::BG_WHITE);
      display.write(' ');
    }
    display.write('\n');
  }
  display.write('\n');
  display.write('\n');
}

const char message[] = "All work and no play makes Jack a dull boy ";

int cx, cy, i;
void loop() {
  if (i == 0) {
    static_assert(std::size(colors) == std::size(background_colors));
    auto fg_idx = random(std::size(colors));
    auto bg_idx = random(std::size(colors)) - 1;
    auto inten_idx = random(std::size(intensity));
    if (bg_idx == fg_idx)
      bg_idx++; // never bg == fg
    for (int j = random(6); j; j--)
      display.write(' ');
    display.setColor(colors[fg_idx], background_colors[bg_idx],
                     intensity[inten_idx]);
    for (int j = random(6); j; j--)
      display.write(' ');
  }

  int ch = message[i++];
  if (ch) {
    display.write(ch);
  } else
    i = 0;

  sleep_ms(32 + random(32));
}
