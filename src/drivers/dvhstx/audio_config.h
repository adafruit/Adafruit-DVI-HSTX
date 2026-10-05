// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
//
// Build flags for the vendored pico_hdmi audio sources. Arduino does not run
// pico_hdmi's CMake, so its option() defaults are restated here. Include this
// before any pico_hdmi header.
#pragma once

// CMake default ON: exact rational audio pacing, no drift against ACR.
#define PICO_HDMI_EXACT_AUDIO_PACING 1
// CMake defaults OFF.
#define PICO_HDMI_RT_RUNTIME_MODE_ATTRS 0
#define PICO_HDMI_EXPLICIT_AUDIO_CHANNEL_STATUS 0
