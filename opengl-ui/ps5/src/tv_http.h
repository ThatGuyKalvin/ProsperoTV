/* ProsperoTV - HTTP for the catalog and the player, on libcurl.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Every request the app makes (channel lists, playlists, streams) goes through
 * these: libcurl with OpenSSL, certificates checked against the console's own
 * list of authorities. The system's HTTP library is not used: with filesystem
 * access the app runs under the system's identity, and that library then
 * refuses every public certificate.
 *
 * The calls take what src/iptv_http.cpp passed to the system's library, so
 * ps5/patch_tree.py only changes the names it calls. A template holds the
 * defaults, a connection one socket and its TLS session, a request one GET or
 * one form POST.
 * Ids are small positive numbers; a negative return is a failure. A failed
 * transfer returns -(10000 + the libcurl code): -10028 is a timeout, -10060 a
 * certificate that could not be verified, -10042 an abort. */

#ifndef TV_HTTP_H
#define TV_HTTP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* The system library's three pools; here they only start libcurl once. */
    int tv_http_tls_init(size_t pool_size);
    int tv_http_tls_term(int tls_id);
    int tv_http_init(int net_pool_id, int tls_id, size_t pool_size);
    int tv_http_term(int http_id);

    int tv_http_create_template(int http_id, const char *user_agent, int version, int proxy);
    int tv_http_delete_template(int template_id);
    int tv_http_create_connection(int template_id, const char *url, int keep_alive);
    int tv_http_delete_connection(int connection_id);
    int tv_http_create_request(int connection_id, int method, const char *url,
                               uint64_t content_length);
    int tv_http_delete_request(int request_id);
    /* A header of the same name is replaced. */
    int tv_http_add_header(int request_id, const char *name, const char *value, int mode);

    /* On a template or a request. Times are in microseconds. */
    int tv_http_set_redirect(int id, int enabled);
    int tv_http_set_resolve_timeout(int id, uint32_t usec);
    int tv_http_set_connect_timeout(int id, uint32_t usec);
    int tv_http_set_send_timeout(int id, uint32_t usec);
    int tv_http_set_receive_timeout(int id, uint32_t usec);
    int tv_http_set_block_size(int id, uint32_t bytes);

    /* Sends the request and waits for the response's headers. */
    int tv_http_send(int request_id, const void *body, size_t size);
    int tv_http_status(int request_id, int *status_code);
    /* The final response's raw headers; valid until the request is deleted. */
    int tv_http_headers(int request_id, char **headers, size_t *size);
    /* Up to `size` bytes of the body: waits for at least one, 0 at its end. */
    int tv_http_read(int request_id, void *data, size_t size);
    /* From any thread: the request's send or read gives up. */
    int tv_http_abort(int request_id);

#ifdef __cplusplus
}
#endif

#endif /* TV_HTTP_H */
