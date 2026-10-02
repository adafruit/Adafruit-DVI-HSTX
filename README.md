# DVI for HSTX (Adafruit Arduino library) <!-- omit in toc -->

This repository is home to the Adafruit GFX compatible DVI driver for RP2 chips with HSTX (e.g. Adafruit Metro RP2350, Adafruit Fruit Jam).

**Important note on overclocking:** This library overclocks your RP2 chip to 264MHz. Simply including the `<Adafruit_dvhstx.h>` header enables this overclocking, separate from the option in the Arduino Tools menu.
Just like PC overclocking, there’s some risk of reduced component lifespan, though the extent (if any) can’t be precisely quantified and could vary from one chip to another.
Proceed at your own discretion.

- [Introduction](#introduction)
- [Documentation](#documentation)
- [Audio](#audio)
- [Scanline callback](#scanline-callback)

## Introduction

DV HSTX will enable you to create big, bold audio visual projects using Arduino and an HDMI display of your choice.

![Text mode display](hstx-textmode.png)
![Graphics](hstx-graphicsmode.png)

## Documentation

See the examples in the `examples` folder. These examples should all work without changes on the Adafruit Feather RP2350, Adafruit Metro RP2350, and Adafruit Fruit Jam, as well as any other boards that define the HSTX pinout with preprocessor macros `PIN_CKP`, PIN_D0P`, PIN_D1P`, and `PIN_D2P`. If these are defined, then you can simply use `DVHSTX_PINOUT_DEFAULT`. 

If your board does not define the HSTX pin mapping, it can be written as 4 numbers inside curly braces: `{ckp, d0p, d1p, d2p}` where e.g., `ckp` is the GPIO# of the positive pin in the clock pair, d0p is the positive pin in the D0 or red pin pair, and so on.

## Audio

Any display class can also send 16-bit stereo audio over the same cable.
Call `enableAudio()` before `begin()`, then keep the queue fed from
`loop()`:

```cpp
DVHSTX16 display(pinConfig, DVHSTX_RESOLUTION_320x240);

void setup() {
  display.enableAudio(48000);  // 32000, 44100 or 48000 Hz
  display.begin();
}

void loop() {
  int16_t buf[64 * 2];  // left, right, left, right ...
  size_t n = min(display.audioAvailableForWrite(), (size_t)64);
  // fill buf with n frames ...
  display.audioWrite(buf, n);  // returns the frames it queued
}
```

`audioWrite()` queues frames in groups of 4 and can take fewer than you
offer when the queue is full; send the rest next time. If the queue runs
dry the display gets silence, and `audioUnderruns()` counts it.

- Audio needs a display that plays sound from the cable, as most TVs do. A
  plain DVI monitor or adapter shows the picture without sound, and some
  may not accept the picture at all with audio on, so audio is off unless
  you enable it.
- Sketches that never call `enableAudio()` use no extra RAM. With audio,
  the packet queue takes 36 KB and one more DMA channel is used.
- Audio works in every mode except `DVHSTX_RESOLUTION_480x270`, where the
  blanking is too short to carry it; `begin()` returns false there.

See `examples/03audiotone`.

## Scanline callback

`DVHSTXScanline` has no frame buffer. Instead your function draws each
line of the output just before it is sent, which suits emulators. The
callback gets the full output width (640 pixels for
`DVHSTX_RESOLUTION_320x240`) as RGB565, two pixels per 32-bit word. It
runs in the video interrupt, so keep it short and in RAM:

```cpp
static void __not_in_flash_func(draw_line)(uint32_t v_scanline,
                                           uint32_t active_line,
                                           uint32_t *dst) {
  // write display.width() pixels for line active_line into dst
}

DVHSTXScanline display(pinConfig, DVHSTX_RESOLUTION_320x240);
display.begin(draw_line);
```

Audio works the same way as with the other classes. See
`examples/04scanline`.

The audio packet encoder and queue come from
[fliperama86/pico_hdmi](https://github.com/fliperama86/pico_hdmi), copied
unmodified into `src/pico_hdmi` (The Unlicense); `tools/revendor.sh`
updates them.
