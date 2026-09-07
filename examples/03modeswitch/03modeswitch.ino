// This example combines the previous 01 and 02 examples, 
// alternating between gfx and text modes,
// to demonstrate runtime mode-switching.

// First display: 320x240x8pp graphics,  line-draw, palette setting,
// Next display:  Text display


// 16-bit Adafruit_GFX-compatible framebuffer for RP2350 HSTX

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

void* display = nullptr;

// If you get the message "error: 'DVHSTX_PINOUT_DEFAULTx' was not declared"
// then you need to give the pins numbers explicitly, like the example below.
// The order is: {CKP, D0P, D1P, D2P} DVHSTX8 display({12, 14, 16, 18},
// DVHSTX_RESOLUTION_640x360);


#define gdisplay ((DVHSTX8*)display)
#define tdisplay ((DVHSTXText*)display)

struct moving_point {
    int x, y, dx, dy;
    void step() {
        x += dx;
        if (x < 0) { x = 0; dx = random(3) + 1; }
        if (x >= gdisplay->width()) { x = gdisplay->width() - 1; dx = -random(3) - 1; }

        y += dy;
        if (y < 0) { y = 0; dy = random(3) + 1; }
        if (y >= gdisplay->height()) { y = gdisplay->height() - 1; dy = -random(3) - 1; }
    }
};

moving_point p1, p2;

int random_with_sign(int n) {
    return random(2) ? random(n-1)+1 : -random(n-1)-1;
}

void setGradientColor(uint8_t index, uint8_t offset) {
  static const uint8_t stops[][3] = {
    { 0, 0, 0 },
    { 0, 0, 255 },
    { 255, 0, 0 },
    { 255, 255, 0 },
    { 255, 255, 255 }
  };
  static const uint8_t stopPositions[] = { 0, 64, 128, 192, 255 };

  uint16_t position = (index + offset) & 0xff;
  uint8_t stop = 0;
  while (stop < 3 && position > stopPositions[stop + 1])
    stop++;

  uint16_t start = stopPositions[stop];
  uint16_t range = stopPositions[stop + 1] - start;
  uint16_t distance = position - start;
  uint8_t color[3];
  for (int channel = 0; channel < 3; channel++) {
    color[channel] = stops[stop][channel] +
                     (stops[stop + 1][channel] - stops[stop][channel]) *
                         distance / range;
  }
  gdisplay->setColor(index, color[0], color[1], color[2]);
}

void setup() {
  Serial.begin(115200);
  //while(!Serial);
  display = new DVHSTX8(pinConfig, DVHSTX_RESOLUTION_320x240);

  if (!gdisplay->begin()) { // Blink LED if insufficient RAM
    Serial.println("init gdisplay init fail");
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;)
      digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
  }
  Serial.println(gdisplay->width());
  Serial.println(gdisplay->height());
  p1 = moving_point{random(gdisplay->width()), random(gdisplay->height()), random_with_sign(3), random_with_sign(3)};
  p2 = moving_point{random(gdisplay->width()), random(gdisplay->height()), random_with_sign(3), random_with_sign(3)};


}

void loop() {
  static int j=0;
  for(int q=0;q<1000;q++)
  {
    gdisplay->drawLine(p1.x, p1.y, p2.x, p2.y, 1 + (j + 254) % 255);
    p1.step();
    p2.step();
    for (int i = 0; i < 256; i++)
      setGradientColor(i, j);
    j += 1;
    sleep_ms(5);
  }

  delete gdisplay;
  display = new DVHSTXText(pinConfig);
  if (!tdisplay->begin()) { // Blink LED if insufficient RAM
    Serial.println("tdisplay init fail!");
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;)
      digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
  }
  tdisplay->setColor(TextColor::TEXT_BLACK, TextColor::BG_WHITE);
  tdisplay->clear();
  tdisplay->showCursor();
  tdisplay->print("display initialized (black on white background)\n\n\n\n\n");
  tdisplay->println("line wrap test. one line should be full of 'w's and the "
                  "next line should start 'xy'.");
  for (int i = 0; i < tdisplay->width(); i++)
    tdisplay->write('w');
  tdisplay->println("xy");
  tdisplay->println(tdisplay->width());
  tdisplay->println(tdisplay->height());
  
  sleep_ms(7000);

  delete tdisplay;
  display = new DVHSTX8(pinConfig, DVHSTX_RESOLUTION_320x240);
  if (!gdisplay->begin()) { // Blink LED if insufficient RAM
    Serial.println("gdisplay init fail");
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;)
      digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
  }
  
}
