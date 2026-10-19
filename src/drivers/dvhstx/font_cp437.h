// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
//
// Code page 437 font for text mode, see font_cp437.c.
#pragma once

#include <stdint.h>

#define FONT_CP437_WIDTH 14
#define FONT_CP437_HEIGHT 24

#ifdef __cplusplus
extern "C" {
#endif
// Row y of byte c, bit 13 the leftmost column.
extern const uint16_t font_cp437[256][FONT_CP437_HEIGHT];
#ifdef __cplusplus
}
#endif
