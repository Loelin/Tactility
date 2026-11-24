// SPDX-License-Identifier: Apache-2.0

// Board-local guard for file access from externally-stacked tasks.
//
// The LVGL task runs on a PSRAM stack. Core UI callbacks can end up reading /data synchronously
// (e.g. the Display app's orientation chip triggers a SystemBars refresh that reloads launcher
// settings); a flash read disables the cache and asserts in ESP-IDF when the caller's stack is
// not in internal RAM. Queue such calls on the file operation worker, which has an internal
// stack, instead. Calls from normal internal-stack tasks pass straight through.

#include <tactility/file_ops.h>
#include <tactility/filesystem/file.h>

#include <esp_memory_utils.h>

#include <freertos/FreeRTOS.h>

extern "C" bool __real_file_exists(const char* path);

namespace {

struct ExistsJob {
    const char* path;
    bool exists;
};

void run_exists(void* context) {
    auto* job = static_cast<ExistsJob*>(context);
    job->exists = __real_file_exists(job->path);
}

bool caller_stack_is_external() {
    uint8_t probe = 0;
    return esp_ptr_external_ram(&probe);
}

} // namespace

extern "C" {

bool __wrap_file_exists(const char* path) {
    if (path == nullptr || file_ops_is_worker() || !caller_stack_is_external()) {
        return __real_file_exists(path);
    }

    ExistsJob job { path, false };
    TaskEventGroup group {};
    task_event_group_construct(&group);

    bool completed = false;
    FileOpsSubscription subscription {};
    if (file_ops_subscribe(&subscription, &group) == ERROR_NONE) {
        if (file_ops_queue(run_exists, &job, &subscription) == ERROR_NONE) {
            completed = file_ops_await(&subscription, portMAX_DELAY) == ERROR_NONE;
        }
        file_ops_unsubscribe(&subscription);
    }

    task_event_group_destruct(&group);
    return completed ? job.exists : __real_file_exists(path);
}

}
