// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
//
// Not part of pico_hdmi. Stands in for its video_output.h, which is not
// copied here because DVI-HSTX has its own video engine. The vendored
// hstx_packet.c.inc and hstx_data_island_queue.c.inc include it only for
// these three values. DVI-HSTX always sends active-low sync (see dvi.hpp).
#pragma once

#define MODE_H_SYNC_POSITIVE 0
#define MODE_V_SYNC_POSITIVE 0
// Default only; dvhstx.cpp sets it per mode with
// hstx_di_queue_set_hsync_active().
#define DI_HSYNC_ACTIVE true
