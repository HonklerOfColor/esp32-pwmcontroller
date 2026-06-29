#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "lwip/ip4_addr.h"

#include "app_config.h"
#include "wifi_ap.h"

static const char *TAG = "wifi_ap";

/* ─── Event handler ──────────────────────────────────────────────────────── */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base != WIFI_EVENT) return;

    switch (id) {
    case WIFI_EVENT_AP_START:
        ESP_LOGI(TAG, "AP started");
        break;

    case WIFI_EVENT_AP_STACONNECTED: {
        wifi_event_ap_staconnected_t *ev = data;
        ESP_LOGI(TAG, "Client connected  MAC=%02X:%02X:%02X:%02X:%02X:%02X  AID=%d",
                 ev->mac[0], ev->mac[1], ev->mac[2],
                 ev->mac[3], ev->mac[4], ev->mac[5],
                 ev->aid);
        break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED: {
        wifi_event_ap_stadisconnected_t *ev = data;
        ESP_LOGI(TAG, "Client disconnected  MAC=%02X:%02X:%02X:%02X:%02X:%02X  AID=%d",
                 ev->mac[0], ev->mac[1], ev->mac[2],
                 ev->mac[3], ev->mac[4], ev->mac[5],
                 ev->aid);
        break;
    }
    default:
        break;
    }
}

/* ─── Public init ────────────────────────────────────────────────────────── */

void wifi_ap_init(void)
{
    /* netif + event loop (idempotent – safe to call multiple times) */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Create the SoftAP network interface */
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

    /* Override default IP/GW/mask and restart DHCP server */
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    ip4addr_aton(WIFI_AP_IP,        (ip4_addr_t *)&ip_info.ip);
    ip4addr_aton(WIFI_AP_IP,        (ip4_addr_t *)&ip_info.gw);
    ip4addr_aton("255.255.255.0",   (ip4_addr_t *)&ip_info.netmask);

    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));

    /* WiFi driver init */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));

    /* AP configuration */
    wifi_config_t wifi_cfg = {
        .ap = {
            .ssid           = WIFI_SSID,
            .ssid_len       = (uint8_t)strlen(WIFI_SSID),
            .channel        = WIFI_CHANNEL,
            .password       = WIFI_PASS,
            .max_connection = WIFI_MAX_CLIENTS,
            .authmode       = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg        = {
                .required = false,
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}
