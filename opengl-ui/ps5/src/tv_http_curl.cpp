// ProsperoTV - The catalog's and the player's HTTP calls, on libcurl.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Adapted from ProsperoRadio's radio_http_curl.cpp. A connection owns a
// libcurl multi handle and a request is one transfer on it, driven by the
// thread that reads it; nothing here starts a thread of its own.

#include "tv_http.h"

#include "update_kit/console_curl.h"

#include <curl/curl.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <string>
#include <strings.h>
#include <time.h>
#include <utility>
#include <vector>

extern "C" int sceKernelDebugOutText(int channel, const char *text);

namespace
{

constexpr int kSlots = 64;
constexpr int kTemplateBase = 0x1000;
constexpr int kConnectionBase = 0x2000;
constexpr int kRequestBase = 0x3000;
// What a request holds of a body nobody has read yet before the transfer is
// told to wait: a few seconds of a 4K channel.
constexpr std::size_t kHighWater = 8u * 1024u * 1024u;
constexpr int kErrorBase = 10000;
// The system library's methods, as src/iptv_http.cpp passes them.
constexpr int kMethodPost = 1;

struct Settings
{
    std::string user_agent;
    long resolve_ms = 5000;
    long connect_ms = 5000;
    long receive_ms = 5000;
    bool redirect = false;
};

struct Template
{
    Settings settings;
};

struct Connection
{
    Settings settings;
    CURLM *multi = nullptr;
};

struct Request
{
    Settings settings;
    Connection *connection = nullptr;
    CURL *easy = nullptr;
    curl_slist *header_list = nullptr;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string url;
    int method = 0;
    std::string post_body; // a form POST's body, sent with the request
    std::string response_headers;
    std::string body; // received, not yet read: from `taken` on
    std::size_t taken = 0;
    long status = 0;
    CURLcode result = CURLE_OK;
    bool sent = false;
    bool headers_done = false;
    bool done = false;
    bool paused = false;
    std::atomic<bool> aborted{false};
    char error[CURL_ERROR_SIZE] = {};
};

pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
Template *g_templates[kSlots];
Connection *g_connections[kSlots];
Request *g_requests[kSlots];
bool g_started = false;

struct Guard
{
    Guard()
    {
        pthread_mutex_lock(&g_lock);
    }
    ~Guard()
    {
        pthread_mutex_unlock(&g_lock);
    }
};

template <typename T> int put(T **table, int base, T *item)
{
    Guard guard;
    for (int i = 0; i < kSlots; ++i)
    {
        if (table[i] == nullptr)
        {
            table[i] = item;
            return base + i;
        }
    }
    return -1;
}

template <typename T> T *get(T **table, int base, int id)
{
    Guard guard;
    return id >= base && id < base + kSlots ? table[id - base] : nullptr;
}

template <typename T> T *take(T **table, int base, int id)
{
    Guard guard;
    if (id < base || id >= base + kSlots)
        return nullptr;
    T *item = table[id - base];
    table[id - base] = nullptr;
    return item;
}

// The settings of whatever the id names.
Settings *settings_of(int id)
{
    if (Request *request = get(g_requests, kRequestBase, id))
        return &request->settings;
    if (Connection *connection = get(g_connections, kConnectionBase, id))
        return &connection->settings;
    if (Template *model = get(g_templates, kTemplateBase, id))
        return &model->settings;
    return nullptr;
}

std::int64_t now_ms()
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

void say(const char *line)
{
    std::fputs(line, stdout);
    sceKernelDebugOutText(0, line);
}

std::size_t on_header(char *data, std::size_t size, std::size_t count, void *user)
{
    Request *request = static_cast<Request *>(user);
    const std::size_t total = size * count;
    // A status line opens a new response: what a redirect said is dropped.
    if (total >= 5 && (std::memcmp(data, "HTTP/", 5) == 0 || std::memcmp(data, "ICY ", 4) == 0))
        request->response_headers.clear();
    request->response_headers.append(data, total);
    if (total <= 2) // the empty line that ends a response's headers
    {
        long status = 0;
        curl_easy_getinfo(request->easy, CURLINFO_RESPONSE_CODE, &status);
        const bool passing = status < 200 || (request->settings.redirect && status >= 300 &&
                                              status < 400 && status != 304);
        if (!passing)
        {
            request->status = status;
            request->headers_done = true;
        }
    }
    return total;
}

std::size_t on_body(char *data, std::size_t size, std::size_t count, void *user)
{
    Request *request = static_cast<Request *>(user);
    if (request->body.size() - request->taken >= kHighWater)
    {
        request->paused = true;
        return CURL_WRITEFUNC_PAUSE;
    }
    request->headers_done = true;
    request->body.append(data, size * count);
    return size * count;
}

// Moves the request's transfer along for up to wait_ms.
void drive(Request *request, int wait_ms)
{
    CURLM *multi = request->connection->multi;
    const std::size_t before = request->body.size();
    const bool had_headers = request->headers_done;
    int running = 0;
    curl_multi_perform(multi, &running);
    int queued = 0;
    while (CURLMsg *message = curl_multi_info_read(multi, &queued))
    {
        if (message->msg == CURLMSG_DONE && message->easy_handle == request->easy)
        {
            request->done = true;
            request->result = message->data.result;
            if (request->status == 0)
                curl_easy_getinfo(request->easy, CURLINFO_RESPONSE_CODE, &request->status);
        }
    }
    // Wait only when that pass brought nothing. On the console the wait does
    // not end early when the socket has more to give, so waiting after every
    // pass would hold a download to one buffer per wait.
    const bool progressed =
        request->body.size() != before || request->headers_done != had_headers || request->done;
    if (!progressed && wait_ms > 0)
    {
        int ready = 0;
        curl_multi_poll(multi, nullptr, 0, wait_ms, &ready);
    }
}

int failure(CURLcode code)
{
    return -(kErrorBase + static_cast<int>(code));
}

long to_ms(std::uint32_t usec)
{
    return static_cast<long>(usec / 1000U);
}

} // namespace

extern "C"
{

int tv_http_tls_init(std::size_t)
{
    return 1;
}

int tv_http_tls_term(int)
{
    return 0;
}

int tv_http_init(int, int, std::size_t)
{
    Guard guard;
    if (!g_started)
    {
        const CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (code != CURLE_OK)
            return failure(code);
        g_started = true;
    }
    return 1; // libcurl stays set up for the life of the process
}

int tv_http_term(int)
{
    return 0;
}

int tv_http_create_template(int, const char *user_agent, int, int)
{
    Template *model = new Template;
    model->settings.user_agent = user_agent != nullptr ? user_agent : "";
    const int id = put(g_templates, kTemplateBase, model);
    if (id < 0)
        delete model;
    return id;
}

int tv_http_delete_template(int id)
{
    delete take(g_templates, kTemplateBase, id);
    return 0;
}

int tv_http_create_connection(int template_id, const char *, int)
{
    Template *model = get(g_templates, kTemplateBase, template_id);
    if (model == nullptr)
        return -1;
    Connection *connection = new Connection;
    connection->settings = model->settings;
    connection->multi = curl_multi_init();
    const int id =
        connection->multi != nullptr ? put(g_connections, kConnectionBase, connection) : -1;
    if (id < 0)
    {
        if (connection->multi != nullptr)
            curl_multi_cleanup(connection->multi);
        delete connection;
    }
    return id;
}

int tv_http_delete_connection(int id)
{
    Connection *connection = take(g_connections, kConnectionBase, id);
    if (connection == nullptr)
        return -1;
    curl_multi_cleanup(connection->multi);
    delete connection;
    return 0;
}

int tv_http_create_request(int connection_id, int method, const char *url, std::uint64_t)
{
    Connection *connection = get(g_connections, kConnectionBase, connection_id);
    if (connection == nullptr || url == nullptr)
        return -1;
    Request *request = new Request;
    request->settings = connection->settings;
    request->connection = connection;
    request->url = url;
    request->method = method;
    const int id = put(g_requests, kRequestBase, request);
    if (id < 0)
        delete request;
    return id;
}

int tv_http_delete_request(int id)
{
    // Out of the table first: an abort from another thread no longer finds it.
    Request *request = take(g_requests, kRequestBase, id);
    if (request == nullptr)
        return -1;
    if (request->easy != nullptr)
    {
        curl_multi_remove_handle(request->connection->multi, request->easy);
        curl_easy_cleanup(request->easy);
    }
    curl_slist_free_all(request->header_list);
    delete request;
    return 0;
}

int tv_http_add_header(int id, const char *name, const char *value, int)
{
    Request *request = get(g_requests, kRequestBase, id);
    if (request == nullptr || name == nullptr || value == nullptr || request->sent)
        return -1;
    for (auto &header : request->headers)
    {
        if (strcasecmp(header.first.c_str(), name) == 0)
        {
            header.second = value;
            return 0;
        }
    }
    request->headers.emplace_back(name, value);
    return 0;
}

int tv_http_set_redirect(int id, int enabled)
{
    Settings *settings = settings_of(id);
    if (settings == nullptr)
        return -1;
    settings->redirect = enabled != 0;
    return 0;
}

int tv_http_set_resolve_timeout(int id, std::uint32_t usec)
{
    Settings *settings = settings_of(id);
    if (settings == nullptr)
        return -1;
    settings->resolve_ms = to_ms(usec);
    return 0;
}

int tv_http_set_connect_timeout(int id, std::uint32_t usec)
{
    Settings *settings = settings_of(id);
    if (settings == nullptr)
        return -1;
    settings->connect_ms = to_ms(usec);
    return 0;
}

int tv_http_set_send_timeout(int id, std::uint32_t)
{
    // A GET has nothing to send after its headers, and a form is a few hundred bytes.
    return settings_of(id) != nullptr ? 0 : -1;
}

int tv_http_set_receive_timeout(int id, std::uint32_t usec)
{
    Settings *settings = settings_of(id);
    if (settings == nullptr)
        return -1;
    settings->receive_ms = to_ms(usec);
    return 0;
}

int tv_http_set_block_size(int id, std::uint32_t)
{
    return settings_of(id) != nullptr ? 0 : -1;
}

int tv_http_send(int id, const void *body, std::size_t size)
{
    Request *request = get(g_requests, kRequestBase, id);
    if (request == nullptr || request->sent || (size != 0 && body == nullptr))
        return -1;
    if (request->method == kMethodPost)
        request->post_body.assign(static_cast<const char *>(body), size);
    request->sent = true;
    request->easy = curl_easy_init();
    if (request->easy == nullptr)
        return failure(CURLE_FAILED_INIT);
    CURL *easy = request->easy;
    // No signals, the console's list of authorities, non-blocking sockets.
    console_curl_setup(easy);
    for (const auto &header : request->headers)
        request->header_list =
            curl_slist_append(request->header_list, (header.first + ": " + header.second).c_str());
    // The name lookup counts toward libcurl's connection limit.
    const long connect_ms = request->settings.resolve_ms + request->settings.connect_ms;
    curl_easy_setopt(easy, CURLOPT_URL, request->url.c_str());
    curl_easy_setopt(easy, CURLOPT_USERAGENT, request->settings.user_agent.c_str());
    curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, request->settings.redirect ? 1L : 0L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(easy, CURLOPT_BUFFERSIZE, 256L * 1024L);
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, connect_ms);
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, request->header_list);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, on_header);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, request);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, on_body);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, request);
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, request->error);
    if (request->method == kMethodPost)
    {
        // A sign-in form (OneStream panels); the body is ours until the request goes.
        curl_easy_setopt(easy, CURLOPT_POST, 1L);
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, request->post_body.data());
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, static_cast<long>(request->post_body.size()));
    }
    if (curl_multi_add_handle(request->connection->multi, easy) != CURLM_OK)
        return failure(CURLE_FAILED_INIT);

    // Until the response's headers are in: the connection, then one receive.
    const std::int64_t deadline = now_ms() + connect_ms + request->settings.receive_ms;
    while (!request->headers_done && !request->done)
    {
        if (request->aborted.load())
            return failure(CURLE_ABORTED_BY_CALLBACK);
        if (now_ms() > deadline)
            return failure(CURLE_OPERATION_TIMEDOUT);
        drive(request, 10);
    }
    if (request->done && request->result != CURLE_OK)
    {
        char line[400];
        std::snprintf(line, sizeof(line), "[ProsperoTV][http] %s: %s (%s)\n",
                      request->url.substr(0, 96).c_str(), curl_easy_strerror(request->result),
                      request->error);
        say(line);
        return failure(request->result);
    }
    return 0;
}

int tv_http_status(int id, int *status)
{
    Request *request = get(g_requests, kRequestBase, id);
    if (request == nullptr || status == nullptr || !request->sent)
        return -1;
    *status = static_cast<int>(request->status);
    return 0;
}

int tv_http_headers(int id, char **headers, std::size_t *size)
{
    Request *request = get(g_requests, kRequestBase, id);
    if (request == nullptr || headers == nullptr || size == nullptr || !request->sent)
        return -1;
    *headers = request->response_headers.data();
    *size = request->response_headers.size();
    return 0;
}

int tv_http_read(int id, void *data, std::size_t size)
{
    Request *request = get(g_requests, kRequestBase, id);
    if (request == nullptr || data == nullptr || !request->sent)
        return -1;
    if (size == 0)
        return 0;
    std::int64_t quiet_since = now_ms();
    for (;;)
    {
        if (request->aborted.load())
            return failure(CURLE_ABORTED_BY_CALLBACK);
        const std::size_t available = request->body.size() - request->taken;
        if (available != 0)
        {
            const std::size_t count = available < size ? available : size;
            std::memcpy(data, request->body.data() + request->taken, count);
            request->taken += count;
            if (request->taken == request->body.size())
            {
                request->body.clear();
                request->taken = 0;
            }
            else if (request->taken >= 1024u * 1024u)
            {
                request->body.erase(0, request->taken);
                request->taken = 0;
            }
            if (request->paused && request->body.size() - request->taken < kHighWater / 2)
            {
                request->paused = false;
                curl_easy_pause(request->easy, CURLPAUSE_CONT);
            }
            return static_cast<int>(count);
        }
        if (request->done)
            return request->result == CURLE_OK ? 0 : failure(request->result);
        if (now_ms() - quiet_since > request->settings.receive_ms)
            return failure(CURLE_OPERATION_TIMEDOUT);
        drive(request, 10);
    }
}

int tv_http_abort(int id)
{
    // Under the lock, so the request cannot be deleted while it is woken.
    Guard guard;
    if (id < kRequestBase || id >= kRequestBase + kSlots ||
        g_requests[id - kRequestBase] == nullptr)
        return -1;
    Request *request = g_requests[id - kRequestBase];
    request->aborted.store(true);
    if (request->connection != nullptr && request->connection->multi != nullptr)
        curl_multi_wakeup(request->connection->multi);
    return 0;
}

} // extern "C"
