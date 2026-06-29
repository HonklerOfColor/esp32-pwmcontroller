#pragma once

/**
 * Start the HTTP server.
 *
 * Endpoints:
 *   GET  /             – Serve index.html (embedded at compile time)
 *   GET  /api/status   – JSON snapshot of all state
 *   POST /api/control  – Set mode (auto/manual) and/or manual fan speed
 *   POST /api/config   – Update temperature and night-mode configuration
 *   POST /api/time     – Sync ESP system clock from browser epoch timestamp
 */
void webserver_start(void);
