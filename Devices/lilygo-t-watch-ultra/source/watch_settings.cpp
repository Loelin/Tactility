// SPDX-License-Identifier: Apache-2.0

// Board-local haptic feedback setting, stored in the user data partition so it survives upgrades.
// Deliberately kept out of core Tactility's DisplaySettings: only this board builds the file.

#include "watch_settings.h"

#include <tactility/filesystem/fs.h>
#include <tactility/paths.h>
#include <tactility/properties_file.h>

#include <sys/stat.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace {

constexpr auto* SETTINGS_DIRECTORY_FORMAT = "%s/settings";
constexpr auto* SETTINGS_FILE_FORMAT = "%s/settings/watch.properties";
constexpr auto* KEY_VIBRATION_ENABLED = "vibrationEnabled";
constexpr auto* KEY_TT9_ENABLED = "tt9Enabled";
constexpr auto* KEY_DICTATION_ENABLED = "dictationEnabled";
// Written by builds before the "Voice Input" -> "Dictation" rename.
constexpr auto* LEGACY_KEY_VOICE_INPUT_ENABLED = "voiceInputEnabled";
constexpr auto* KEY_WAKE_GPS_SYNC_ENABLED = "wakeGpsSyncEnabled";
constexpr auto* KEY_DICTATION_EXTERNAL_ENABLED = "dictationExternalEnabled";
constexpr auto* KEY_DICTATION_ENDPOINT = "dictationEndpoint";
constexpr auto* KEY_DICTATION_API_KEY = "dictationApiKey";
constexpr auto* KEY_DICTATION_MODEL = "dictationModel";

constexpr auto* DEFAULT_DICTATION_ENDPOINT = "https://api.openai.com";
constexpr auto* DEFAULT_DICTATION_MODEL = "whisper-1";

std::atomic<bool> settings_loaded { false };
std::atomic<bool> vibration_enabled { true };
std::atomic<bool> tt9_enabled { false };
std::atomic<bool> dictation_enabled { true };
std::atomic<bool> dictation_external_enabled { false };
std::atomic<bool> wake_gps_sync_enabled { true };

// Loaded once with the rest of the file, then only touched by the getters/setters. The Dictation
// worker runs with a PSRAM stack, so it must never read the settings file itself; the UI thread
// snapshots these values before starting a recording.
std::mutex string_settings_mutex;
char dictation_endpoint[WATCH_SETTINGS_ENDPOINT_MAX] = {};
char dictation_api_key[WATCH_SETTINGS_API_KEY_MAX] = {};
char dictation_model[WATCH_SETTINGS_MODEL_MAX] = {};

std::mutex load_mutex;

void trim_in_place(char* value) {
    if (value == nullptr) {
        return;
    }
    char* start = value;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        ++start;
    }
    if (start != value) {
        std::memmove(value, start, std::strlen(start) + 1);
    }
    size_t length = std::strlen(value);
    while (length > 0) {
        const char last = value[length - 1];
        if (last != ' ' && last != '\t' && last != '\r' && last != '\n') {
            break;
        }
        value[--length] = '\0';
    }
}

void copy_setting(char* destination, size_t capacity, const char* value) {
    if (destination == nullptr || capacity == 0) {
        return;
    }
    std::strncpy(destination, value != nullptr ? value : "", capacity - 1);
    destination[capacity - 1] = '\0';
}

bool get_settings_path(char* out_path, size_t out_path_size) {
    char data_path[FILE_MAX_PATH_STRING_LENGTH];
    if (paths_get_data_path(data_path, sizeof(data_path)) != ERROR_NONE) {
        return false;
    }
    const int written = std::snprintf(out_path, out_path_size, SETTINGS_FILE_FORMAT, data_path);
    return written > 0 && static_cast<size_t>(written) < out_path_size;
}

bool ensure_settings_directory() {
    char data_path[FILE_MAX_PATH_STRING_LENGTH];
    if (paths_get_data_path(data_path, sizeof(data_path)) != ERROR_NONE) {
        return false;
    }
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    const int written = std::snprintf(directory, sizeof(directory), SETTINGS_DIRECTORY_FORMAT, data_path);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(directory)) {
        return false;
    }
    // Create parents too: a freshly flashed data partition may not contain /data/tactility yet.
    const error_t result = directory_make(directory, true);
    return result == ERROR_NONE || result == ERROR_ALREADY_EXISTS;
}

void store_setting(const char* key, const char* value) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (!get_settings_path(path, sizeof(path))) {
        return;
    }

    ensure_settings_directory();

    PropertiesFile* file = properties_file_open(path);
    if (file == nullptr) {
        return;
    }
    properties_file_set(file, key, value);
    properties_file_close(file);
}

void store_setting(const char* key, bool enabled) {
    store_setting(key, enabled ? "1" : "0");
}

// Returns false while the user-data filesystem isn't available yet, so a later call can retry.
bool load_settings() {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (!get_settings_path(path, sizeof(path))) {
        return false;
    }

    // The data partition image doesn't contain this directory on a fresh flash. Create it before
    // opening, otherwise properties_file_close() can't write the file back and logs an error.
    if (!ensure_settings_directory()) {
        return false;
    }

    PropertiesFile* file = properties_file_open(path);
    if (file == nullptr) {
        return false;
    }

    char value[16] = {};
    if (properties_file_get(file, KEY_VIBRATION_ENABLED, value, sizeof(value)) == ERROR_NONE) {
        vibration_enabled.store(std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0);
    }
    if (properties_file_get(file, KEY_TT9_ENABLED, value, sizeof(value)) == ERROR_NONE) {
        tt9_enabled.store(std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0);
    }
    if (properties_file_get(file, KEY_DICTATION_ENABLED, value, sizeof(value)) == ERROR_NONE ||
        properties_file_get(file, LEGACY_KEY_VOICE_INPUT_ENABLED, value, sizeof(value)) == ERROR_NONE) {
        dictation_enabled.store(std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0);
    }
    if (properties_file_get(file, KEY_DICTATION_EXTERNAL_ENABLED, value, sizeof(value)) == ERROR_NONE) {
        dictation_external_enabled.store(std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0);
    }
    if (properties_file_get(file, KEY_WAKE_GPS_SYNC_ENABLED, value, sizeof(value)) == ERROR_NONE) {
        wake_gps_sync_enabled.store(std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0);
    }

    {
        std::lock_guard<std::mutex> lock(string_settings_mutex);

        char string_value[WATCH_SETTINGS_API_KEY_MAX];
        if (properties_file_get(file, KEY_DICTATION_ENDPOINT, string_value, sizeof(string_value)) == ERROR_NONE) {
            trim_in_place(string_value);
            copy_setting(dictation_endpoint, sizeof(dictation_endpoint),
                string_value[0] != '\0' ? string_value : DEFAULT_DICTATION_ENDPOINT);
        }
        if (properties_file_get(file, KEY_DICTATION_API_KEY, string_value, sizeof(string_value)) == ERROR_NONE) {
            trim_in_place(string_value);
            copy_setting(dictation_api_key, sizeof(dictation_api_key), string_value);
        }
        if (properties_file_get(file, KEY_DICTATION_MODEL, string_value, sizeof(string_value)) == ERROR_NONE) {
            trim_in_place(string_value);
            copy_setting(dictation_model, sizeof(dictation_model),
                string_value[0] != '\0' ? string_value : DEFAULT_DICTATION_MODEL);
        }
    }

    properties_file_close(file);
    return true;
}

} // namespace

extern "C" {

bool watch_settings_ensure_directory(void) {
    return ensure_settings_directory();
}

bool watch_settings_load(void) {
    if (settings_loaded.load(std::memory_order_acquire)) {
        return true;
    }

    std::lock_guard<std::mutex> lock(load_mutex);
    if (settings_loaded.load(std::memory_order_acquire)) {
        return true;
    }
    if (!load_settings()) {
        return false;
    }
    settings_loaded.store(true, std::memory_order_release);
    return true;
}

bool watch_settings_get_vibration_enabled(void) {
    watch_settings_load();
    return vibration_enabled.load(std::memory_order_acquire);
}

void watch_settings_set_vibration_enabled(bool enabled) {
    settings_loaded.store(true, std::memory_order_release);
    vibration_enabled.store(enabled, std::memory_order_release);
    store_setting(KEY_VIBRATION_ENABLED, enabled);
}

bool watch_settings_get_tt9_enabled(void) {
    watch_settings_load();
    return tt9_enabled.load(std::memory_order_acquire);
}

void watch_settings_set_tt9_enabled(bool enabled) {
    settings_loaded.store(true, std::memory_order_release);
    tt9_enabled.store(enabled, std::memory_order_release);
    store_setting(KEY_TT9_ENABLED, enabled);
}

bool watch_settings_get_dictation_enabled(void) {
    watch_settings_load();
    return dictation_enabled.load(std::memory_order_acquire);
}

void watch_settings_set_dictation_enabled(bool enabled) {
    settings_loaded.store(true, std::memory_order_release);
    dictation_enabled.store(enabled, std::memory_order_release);
    store_setting(KEY_DICTATION_ENABLED, enabled);
}

bool watch_settings_get_wake_gps_sync_enabled(void) {
    watch_settings_load();
    return wake_gps_sync_enabled.load(std::memory_order_acquire);
}

void watch_settings_set_wake_gps_sync_enabled(bool enabled) {
    settings_loaded.store(true, std::memory_order_release);
    wake_gps_sync_enabled.store(enabled, std::memory_order_release);
    store_setting(KEY_WAKE_GPS_SYNC_ENABLED, enabled);
}

bool watch_settings_get_dictation_external_enabled(void) {
    watch_settings_load();
    return dictation_external_enabled.load(std::memory_order_acquire);
}

void watch_settings_set_dictation_external_enabled(bool enabled) {
    settings_loaded.store(true, std::memory_order_release);
    dictation_external_enabled.store(enabled, std::memory_order_release);
    store_setting(KEY_DICTATION_EXTERNAL_ENABLED, enabled);
}

void watch_settings_get_dictation_endpoint(char* out, size_t capacity) {
    if (out == nullptr || capacity == 0) {
        return;
    }
    watch_settings_load();
    std::lock_guard<std::mutex> lock(string_settings_mutex);
    copy_setting(out, capacity,
        dictation_endpoint[0] != '\0' ? dictation_endpoint : DEFAULT_DICTATION_ENDPOINT);
}

void watch_settings_set_dictation_endpoint(const char* value) {
    char trimmed[WATCH_SETTINGS_ENDPOINT_MAX];
    copy_setting(trimmed, sizeof(trimmed), value);
    trim_in_place(trimmed);

    settings_loaded.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(string_settings_mutex);
        copy_setting(dictation_endpoint, sizeof(dictation_endpoint),
            trimmed[0] != '\0' ? trimmed : DEFAULT_DICTATION_ENDPOINT);
    }
    store_setting(KEY_DICTATION_ENDPOINT, trimmed[0] != '\0' ? trimmed : DEFAULT_DICTATION_ENDPOINT);
}

void watch_settings_get_dictation_api_key(char* out, size_t capacity) {
    if (out == nullptr || capacity == 0) {
        return;
    }
    watch_settings_load();
    std::lock_guard<std::mutex> lock(string_settings_mutex);
    copy_setting(out, capacity, dictation_api_key);
}

void watch_settings_set_dictation_api_key(const char* value) {
    char trimmed[WATCH_SETTINGS_API_KEY_MAX];
    copy_setting(trimmed, sizeof(trimmed), value);
    trim_in_place(trimmed);

    settings_loaded.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(string_settings_mutex);
        copy_setting(dictation_api_key, sizeof(dictation_api_key), trimmed);
    }
    store_setting(KEY_DICTATION_API_KEY, trimmed);
}

void watch_settings_get_dictation_model(char* out, size_t capacity) {
    if (out == nullptr || capacity == 0) {
        return;
    }
    watch_settings_load();
    std::lock_guard<std::mutex> lock(string_settings_mutex);
    copy_setting(out, capacity,
        dictation_model[0] != '\0' ? dictation_model : DEFAULT_DICTATION_MODEL);
}

void watch_settings_set_dictation_model(const char* value) {
    char trimmed[WATCH_SETTINGS_MODEL_MAX];
    copy_setting(trimmed, sizeof(trimmed), value);
    trim_in_place(trimmed);

    settings_loaded.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(string_settings_mutex);
        copy_setting(dictation_model, sizeof(dictation_model),
            trimmed[0] != '\0' ? trimmed : DEFAULT_DICTATION_MODEL);
    }
    store_setting(KEY_DICTATION_MODEL, trimmed[0] != '\0' ? trimmed : DEFAULT_DICTATION_MODEL);
}

} // extern "C"
