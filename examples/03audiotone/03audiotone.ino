// Colour bars and a 1 kHz tone over one DVI cable. The display plays the
// sound only if it takes audio from the cable, as most TVs do; a plain DVI
// monitor or adapter shows the picture but stays silent.
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

DVHSTX16 display(pinConfig, DVHSTX_RESOLUTION_320x240);

#define SAMPLE_RATE 48000
#define TONE_HZ 1000

static int16_t sine[SAMPLE_RATE / TONE_HZ]; // one cycle of the tone
static size_t phase;

void setup() {
  for (size_t i = 0; i < count_of(sine); i++) {
    sine[i] = (int16_t)(sinf(i * 2.0f * PI / count_of(sine)) * 6000);
  }

  display.enableAudio(SAMPLE_RATE); // before begin()
  if (!display.begin()) {           // Blink LED if insufficient RAM
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;)
      digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
  }

  const uint16_t bars[8] = {0xFFFF, 0xFFE0, 0x07FF, 0x07E0,
                            0xF81F, 0xF800, 0x001F, 0x0000};
  int w = display.width() / 8;
  for (int i = 0; i < 8; i++) {
    display.fillRect(i * w, 0, w, display.height(), bars[i]);
  }
}

void loop() {
  // Keep the audio queue topped up. audioWrite() takes interleaved stereo
  // frames (left, right, left, right ...) and returns how many it queued.
  int16_t buf[64 * 2];
  size_t n = display.audioAvailableForWrite();
  if (n > 64)
    n = 64;
  for (size_t i = 0; i < n; i++) {
    int16_t s = sine[(phase + i) % count_of(sine)];
    buf[i * 2] = s;     // left
    buf[i * 2 + 1] = s; // right
  }
  phase = (phase + display.audioWrite(buf, n)) % count_of(sine);
}
