// 16-bit Adafruit_GFX-compatible framebuffer for RP2350 HSTX

#include <Adafruit_dvhstx.h>

// If your board definition has PIN_CKP and related defines,
// DVHSTX_PINOUT_DEFAULT is available
DVHSTX8 display(DVHSTX_PINOUT_DEFAULT, DVHSTX_RESOLUTION_640x360);
// If you get the message "error: 'DVHSTX_PINOUT_DEFAULTx' was not declared"
// then you need to give the pins numbers explicitly, like the example below.
// The order is: {CKP, D0P, D1P, D2P} DVHSTX8 display({12, 14, 16, 18},
// DVHSTX_RESOLUTION_640x360);

struct moving_point {
    int x, y, dx, dy;
    void step() {
        x += dx;
        if (x < 0) { x = 0; dx = random(3) + 1; }
        if (x >= display.width()) { x = display.width() - 1; dx = -random(3) - 1; }

        y += dy;
        if (y < 0) { y = 0; dy = random(3) + 1; }
        if (y >= display.height()) { y = display.height() - 1; dy = -random(3) - 1; }
    }
};

moving_point p1, p2;

int random_with_sign(int n) {
    return random(2) ? random(n-1)+1 : -random(n-1)-1;
}

void setup() {
  Serial.begin(115200);
  // while(!Serial);
  if (!display.begin()) { // Blink LED if insufficient RAM
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;)
      digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
  }
  Serial.println("display initialized");
  p1 = moving_point{random(display.width()), random(display.height()), random_with_sign(3), random_with_sign(3)};
  p2 = moving_point{random(display.width()), random(display.height()), random_with_sign(3), random_with_sign(3)};
}

void loop() {
  static int j;
  display.drawLine(p1.x, p1.y, p2.x, p2.y, 1 + (j + 254) % 255);
Serial.printf("%d %d %d %d\r\n", p1.x, p1.y, p1.dx, p1.dy);
  p1.step();
  p2.step();

  for(int i=1; i<256; i++) {
    display.setColor(i, ((i + j) % 255) * 0x010101);
  }
  j += 1;
  delay(3);
}
