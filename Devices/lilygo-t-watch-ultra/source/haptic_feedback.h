// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

// DRV2605 ROM library waveforms used by this board's touch feedback.
constexpr uint8_t WATCH_HAPTIC_CLICK = 18; // Strong Click 2 - 80%: widget presses
constexpr uint8_t WATCH_HAPTIC_KEY = 5;    // Sharp Click - 60%: keyboard/button matrix
constexpr uint8_t WATCH_HAPTIC_LIGHT = 6;  // Sharp Click - 30%: scroll/page settles
// Voice input: a full-strength click when recording starts, sharp clicks when it finishes.
constexpr uint8_t WATCH_HAPTIC_VOICE_START = 1; // Strong Click - 100%
constexpr uint8_t WATCH_HAPTIC_VOICE_DONE = 5;  // Sharp Click - 60%

/**
 * @brief Plays one waveform through the haptic motor.
 * @details Respects the user's haptic feedback setting and silently does nothing when the
 *          setting is off or no haptic device is available.
 * @param[in] waveform DRV2605 ROM library waveform index
 */
void watch_haptic_play(uint8_t waveform);

/**
 * @brief Plays the same waveform several times in one hardware sequence.
 * @param[in] waveform DRV2605 ROM library waveform index
 * @param[in] count number of clicks (clamped to 7)
 */
void watch_haptic_play_sequence(uint8_t waveform, uint8_t count);
