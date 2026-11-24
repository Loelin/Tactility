// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest endpoint URL accepted by the Dictation app, including the terminator. */
#define WATCH_SETTINGS_ENDPOINT_MAX 192
/** Longest API key accepted by the Dictation app, including the terminator. */
#define WATCH_SETTINGS_API_KEY_MAX 256
/** Longest model name accepted by the Dictation app, including the terminator. */
#define WATCH_SETTINGS_MODEL_MAX 64

/** Whether the DRV2605 touch-click feedback is enabled. Defaults to true. */
bool watch_settings_get_vibration_enabled(void);

/** Persists the touch-click feedback preference. */
void watch_settings_set_vibration_enabled(bool enabled);

/** Whether the TT9 (multi-tap) on-screen keypad is enabled instead of the QWERTY keyboard. Defaults to false. */
bool watch_settings_get_tt9_enabled(void);

/** Persists the TT9 keypad preference. */
void watch_settings_set_tt9_enabled(bool enabled);

/** Whether push-to-talk dictation is enabled. Defaults to true. */
bool watch_settings_get_dictation_enabled(void);

/** Persists the dictation preference. */
void watch_settings_set_dictation_enabled(bool enabled);

/** Ensures the board settings directory exists. Safe to call repeatedly. */
bool watch_settings_ensure_directory(void);

/** Whether a wake from deep sleep should resync the RTC from GPS/location. Defaults to true. */
bool watch_settings_get_wake_gps_sync_enabled(void);

/** Persists the wake GPS-sync preference. */
void watch_settings_set_wake_gps_sync_enabled(bool enabled);

/** Whether dictation sends audio to an external OpenAI-compatible endpoint. Defaults to false. */
bool watch_settings_get_dictation_external_enabled(void);

/** Persists the external-transcription preference. */
void watch_settings_set_dictation_external_enabled(bool enabled);

/**
 * @brief Copies the external endpoint base URL.
 *
 * The value may be a base URL ("https://api.openai.com") or a complete
 * ".../v1/audio/transcriptions" URL.
 */
void watch_settings_get_dictation_endpoint(char* out, size_t capacity);

/** Persists the external endpoint URL. */
void watch_settings_set_dictation_endpoint(const char* value);

/** Copies the external API key (empty when none is configured). */
void watch_settings_get_dictation_api_key(char* out, size_t capacity);

/** Persists the external API key. */
void watch_settings_set_dictation_api_key(const char* value);

/** Copies the external model name. Defaults to "whisper-1". */
void watch_settings_get_dictation_model(char* out, size_t capacity);

/** Persists the external model name. */
void watch_settings_set_dictation_model(const char* value);

/**
 * @brief Reads the settings file if it hasn't been read yet.
 *
 * Must not be called from the LVGL task: that task's stack can be in PSRAM, which is unavailable
 * while the flash cache is off during file I/O.
 *
 * @return true once the file has been read (or is known to be absent), false while the user-data
 * filesystem isn't available yet, so the caller can retry.
 */
bool watch_settings_load(void);

/** Registers this board's "Miscellaneous" app with the app manager. */
void watch_settings_register_app(void);

#ifdef __cplusplus
}
#endif
