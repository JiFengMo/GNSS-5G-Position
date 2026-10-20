#pragma once
#include <stddef.h>

#ifndef NR_DEFAULT_LOG_DIR
#define NR_DEFAULT_LOG_DIR "log"
#endif

/* Relative paths live under log/. Absolute overrides must also stay there.
 * Parent directories are created automatically; '..' components are rejected. */
int nr_log_file_path(char *path, size_t capacity, const char *requested, const char *fallback);
int nr_log_directory(char *path, size_t capacity, const char *requested, const char *fallback);
