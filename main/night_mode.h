#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/**
 * Start the night-mode monitoring task.
 *
 * Checks every 60 s whether the current local hour falls inside the configured
 * night window and writes g_state.night_active accordingly.
 *
 * Time is set via the /api/time endpoint when a browser first loads the page.
 * Until then the clock starts at the Unix epoch and night mode is inactive.
 */
void night_mode_start_task(void);

/**
 * Set the system time from a millisecond epoch timestamp (sent by the browser).
 * Can be called from any task – uses settimeofday() which is thread-safe.
 */
void night_mode_set_time(int64_t epoch_ms);

/**
 * Return the current local time as a formatted ISO-8601 string.
 * The buffer must be at least 20 bytes ("YYYY-MM-DDTHH:MM:SS").
 */
void night_mode_get_time_str(char *buf, size_t len);
