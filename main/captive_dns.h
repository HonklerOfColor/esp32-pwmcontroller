#pragma once

/**
 * Start a minimal UDP DNS server on port 53.
 *
 * Every DNS A-query is answered with the AP's own IP (192.168.4.1), regardless
 * of the requested hostname.  This makes every connected client believe that
 * the internet is unreachable through a normal route, which causes iOS, Android,
 * macOS and Windows to display a "Sign in to network" / captive-portal prompt.
 */
void captive_dns_start(void);
