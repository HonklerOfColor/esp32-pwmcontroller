#pragma once

/**
 * Initialise WiFi in SoftAP mode.
 *
 * – SSID / password come from app_config.h
 * – Static IP  192.168.4.1, DHCP pool starts at .2
 * – Must be called after nvs_flash_init()
 */
void wifi_ap_init(void);
