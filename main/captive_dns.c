#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "captive_dns.h"

static const char *TAG = "captive_dns";

#define DNS_PORT     53
#define DNS_BUF_SIZE 512

/* ─── DNS response builder ───────────────────────────────────────────────── */
/*
 * Builds a minimal DNS response that resolves ANY A-query to 192.168.4.1.
 * Layout: original header (modified) + original question + one answer RR.
 */
static int build_response(const uint8_t *req, int req_len, uint8_t *resp)
{
    if (req_len < 12 || req_len > DNS_BUF_SIZE - 32) return -1;

    /* Copy header verbatim, then patch flags + counts */
    memcpy(resp, req, 12);
    resp[2]  = 0x81;   /* QR=1 (response), AA=1 (authoritative), RD echo */
    resp[3]  = 0x80;   /* RA=1, RCODE=0 (no error) */
    resp[6]  = 0x00;   /* Answer RR high */
    resp[7]  = 0x01;   /* Answer RR low  = 1 */
    resp[8]  = 0x00;   /* Authority RRs  = 0 */
    resp[9]  = 0x00;
    resp[10] = 0x00;   /* Additional RRs = 0 */
    resp[11] = 0x00;

    /* Copy question section unchanged */
    int qlen = req_len - 12;
    memcpy(resp + 12, req + 12, qlen);
    int pos = 12 + qlen;

    /* Answer record ─────────────────────────────────── */
    resp[pos++] = 0xC0;   /* Name: pointer … */
    resp[pos++] = 0x0C;   /* … to offset 12 (start of question) */

    resp[pos++] = 0x00;   /* Type A  (high) */
    resp[pos++] = 0x01;   /* Type A  (low)  */

    resp[pos++] = 0x00;   /* Class IN (high) */
    resp[pos++] = 0x01;   /* Class IN (low)  */

    resp[pos++] = 0x00;   /* TTL = 60 s */
    resp[pos++] = 0x00;
    resp[pos++] = 0x00;
    resp[pos++] = 0x3C;

    resp[pos++] = 0x00;   /* RDLENGTH = 4 */
    resp[pos++] = 0x04;

    resp[pos++] = 192;    /* 192.168.4.1 */
    resp[pos++] = 168;
    resp[pos++] = 4;
    resp[pos++] = 1;

    return pos;
}

/* ─── Task ───────────────────────────────────────────────────────────────── */

static void dns_task(void *pvParam)
{
    (void)pvParam;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed: %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in bind_addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "bind() on port %d failed: %d", DNS_PORT, errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Captive-portal DNS listening on UDP port %d", DNS_PORT);

    static uint8_t req[DNS_BUF_SIZE];
    static uint8_t resp[DNS_BUF_SIZE];
    struct sockaddr_in src;
    socklen_t src_len = sizeof(src);

    for (;;) {
        int len = recvfrom(sock, req, sizeof(req) - 1, 0,
                           (struct sockaddr *)&src, &src_len);
        if (len < 0) continue;

        int resp_len = build_response(req, len, resp);
        if (resp_len > 0) {
            sendto(sock, resp, resp_len, 0,
                   (struct sockaddr *)&src, src_len);
        }
    }
}

void captive_dns_start(void)
{
    xTaskCreate(dns_task, "captive_dns", 4096, NULL, 5, NULL);
}
