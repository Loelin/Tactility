// SPDX-License-Identifier: Apache-2.0

// Serializes properties-file disk I/O, working around a core bug this board hits.
//
// properties_file_close() rewrites the whole file through a fixed "<path>.tmp" name (see
// TactilityKernel/source/properties_file.cpp). Two settings accesses that overlap - for example
// the Display app saving while the auto-rotate service saves an orientation change - use the same
// temporary path: the first rename() consumes it, so the second fails with
// "properties_file: Failed to replace ...". Worse, a load that opens while a save is between its
// remove() and rename() can see no file and then write back an empty one.
//
// Every load rewrites the file on close as well, so both open (read) and close (write) are
// serialized. Only the call itself is locked, not the lifetime of the handle: Preferences keeps a
// PropertiesFile open across calls and would deadlock otherwise.
//
// The clean fix is a unique temporary name per save, but that changes core Tactility files; this
// keeps the workaround board-local.

#include <tactility/properties_file.h>

#include <mutex>

namespace {
std::recursive_mutex properties_io_mutex;
}

extern "C" {

PropertiesFile* __real_properties_file_open(const char* path);
error_t __real_properties_file_close(PropertiesFile* file);

PropertiesFile* __wrap_properties_file_open(const char* path) {
    std::lock_guard<std::recursive_mutex> lock(properties_io_mutex);
    return __real_properties_file_open(path);
}

error_t __wrap_properties_file_close(PropertiesFile* file) {
    std::lock_guard<std::recursive_mutex> lock(properties_io_mutex);
    return __real_properties_file_close(file);
}

} // extern "C"
