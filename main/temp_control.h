#pragma once

/**
 * Start the temperature-control task.
 *
 * In auto mode the task reads g_state.temperature every 5 s, computes a
 * target fan speed via linear interpolation between temp_low/temp_high, applies
 * the night-mode cap, and updates g_state.fan_target_pct.
 *
 * In manual mode the task simply copies g_state.manual_pct → fan_target_pct
 * (respecting the night cap).
 */
void temp_control_start_task(void);
