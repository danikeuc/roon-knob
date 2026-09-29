#pragma once

#include <esp_http_server.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Setup-mode timeline for diagnosing slow captive-portal pop-ups.
 *
 * Records the first PORTAL_TRACE_MAX events after each station joins the
 * setup AP (join, DHCP lease, DNS queries, HTTP requests, socket open/close)
 * in ms since that join, and logs the whole sequence when the station leaves.
 * Events are stored in RAM rather than logged as they happen so that the
 * logging itself does not add latency to the paths being timed. */

/* Append an event.  kind: J join, L leave, I DHCP lease, D DNS, H HTTP,
 * O socket open, C socket close. */
void portal_trace(char kind, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* Start a new timeline (called on each station join). */
void portal_trace_reset(void);

/* Log the recorded timeline. */
void portal_trace_dump(const char *why);

/* Record an HTTP request as "<Host><URI>". */
void portal_trace_req(httpd_req_t *req);

/* httpd open_fn/close_fn hooks.  close_fn owns closing the socket. */
esp_err_t portal_trace_open(httpd_handle_t hd, int sockfd);
void portal_trace_close(httpd_handle_t hd, int sockfd);

#ifdef __cplusplus
}
#endif
