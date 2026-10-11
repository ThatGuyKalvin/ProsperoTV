// ProsperoTV - TLS uses the installed OpenSSL; SceNet gets a socket BIO adapter.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/share_tls.hpp"
#include "tv/share_bundle.hpp"
#include "tv/platform.hpp"
#include <openssl/ssl.h>
#include <openssl/ec.h>
#include <openssl/rand.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <memory>

#ifdef __PROSPERO__
extern "C"
{
    int sceNetInit(void);
    int sceNetSocket(const char *, int, int, int);
    int sceNetSocketClose(int);
    int sceNetBind(int, const sockaddr *, socklen_t);
    int sceNetListen(int, int);
    int sceNetAccept(int, sockaddr *, socklen_t *);
    int sceNetRecv(int, void *, size_t, int);
    int sceNetSend(int, const void *, size_t, int);
    int sceNetSetsockopt(int, int, int, const void *, socklen_t);
    int sceNetConnect(int, const sockaddr *, socklen_t);
    int sceNetGetsockname(int, sockaddr *, socklen_t *);
    int *sceNetErrnoLoc(void);
}
#define socket(domain, type, protocol) sceNetSocket("ProsperoTVShare", domain, type, protocol)
#define close sceNetSocketClose
#define bind sceNetBind
#define listen sceNetListen
#define accept sceNetAccept
#define recv sceNetRecv
#define send sceNetSend
#define setsockopt sceNetSetsockopt
#define connect sceNetConnect
#define getsockname sceNetGetsockname
#endif

namespace ptv
{
namespace
{
int socket_error()
{
#ifdef __PROSPERO__
    return *sceNetErrnoLoc() & 0xff;
#else
    return errno;
#endif
}
bool retry()
{
    const int e = socket_error();
    return e == EAGAIN || e == EWOULDBLOCK || e == EINTR || e == EINPROGRESS || e == EALREADY;
}
bool nonblocking(int fd)
{
    int enabled = 1;
#ifdef __PROSPERO__
    return setsockopt(fd, SOL_SOCKET, 0x1200, &enabled, sizeof(enabled)) == 0;
#else
    return ioctl(fd, FIONBIO, &enabled) == 0;
#endif
}
struct Socket
{
    int fd = -1;
    ~Socket()
    {
        if (fd >= 0)
            close(fd);
    }
};
std::string fingerprint(X509 *cert)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned count = 0;
    if (!cert || X509_digest(cert, EVP_sha256(), digest, &count) != 1 || count != 32)
        return {};
    std::string result;
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned i = 0; i < count; ++i)
    {
        result += hex[digest[i] >> 4];
        result += hex[digest[i] & 15];
    }
    return result;
}
int bio_read(BIO *bio, char *data, int size)
{
    BIO_clear_retry_flags(bio);
    const int n = recv(*static_cast<int *>(BIO_get_data(bio)), data, size, 0);
    if (n < 0 && retry())
        BIO_set_retry_read(bio);
    return n;
}
int bio_write(BIO *bio, const char *data, int size)
{
    BIO_clear_retry_flags(bio);
#if defined(MSG_NOSIGNAL) && !defined(__PROSPERO__)
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    const int n = send(*static_cast<int *>(BIO_get_data(bio)), data, size, flags);
    if (n < 0 && retry())
        BIO_set_retry_write(bio);
    return n;
}
long bio_control(BIO *, int command, long, void *)
{
    return command == BIO_CTRL_FLUSH ? 1 : 0;
}
bool local_address(const std::string &address, sockaddr_in &peer)
{
    if (inet_pton(AF_INET, address.c_str(), &peer.sin_addr) != 1)
        return false;
    const auto ip = ntohl(peer.sin_addr.s_addr);
    return ip >> 24 == 10 || ip >> 24 == 127 || ip >> 20 == 0xac1 || ip >> 16 == 0xc0a8 ||
           ip >> 16 == 0xa9fe;
}
} // namespace

bool share_fingerprint(std::string_view input, std::string &normalized)
{
    normalized.clear();
    if (input.size() > 128)
        return false;
    for (char c : input)
    {
        if (c == ':' || c == ' ' || c == '\n' || c == '\r')
            continue;
        if (c >= 'a' && c <= 'f')
            c -= 'a' - 'A';
        if (!((c >= 'A' && c <= 'F') || (c >= '0' && c <= '9')))
            return false;
        normalized += c;
    }
    return normalized.size() == 64;
}
ShareTls::~ShareTls()
{
    cancel();
}
void ShareTls::cancel()
{
    cancel_.store(true);
    if (thread_)
        platform::thread_join(thread_);
    thread_ = nullptr;
    std::lock_guard lock(mutex_);
    payload_.clear();
    if (status_.phase != SharePhase::idle)
        status_.phase = SharePhase::cancelled;
}
ShareStatus ShareTls::status() const
{
    std::lock_guard lock(mutex_);
    return status_;
}
void ShareTls::phase(SharePhase value)
{
    std::lock_guard lock(mutex_);
    status_.phase = value;
}
void ShareTls::approve()
{
    approved_.store(true);
}
bool ShareTls::alive() const
{
    return !cancel_.load() && platform::monotonic_us() < deadline_;
}
std::string ShareTls::take_payload()
{
    std::lock_guard lock(mutex_);
    if (status_.phase != SharePhase::ready || !receiving_)
        return {};
    return std::move(payload_);
}
bool ShareTls::receive(unsigned port)
{
    cancel();
    if (port > 65535)
        return false;
    receiving_ = true;
    port_ = port;
    cancel_.store(false);
    approved_.store(false);
    status_ = {SharePhase::starting, {}, {}, {}, 0};
    deadline_ = platform::monotonic_us() + 180000000;
    thread_ = platform::thread_start(entry, this, 2u * 1024u * 1024u, "ptv-share");
    if (!thread_)
        phase(SharePhase::failed);
    return thread_ != nullptr;
}
bool ShareTls::transmit(std::string address, unsigned port, std::string pin, std::string payload)
{
    cancel();
    sockaddr_in check{};
    if (!port || port > 65535 || !local_address(address, check) ||
        !share_fingerprint(pin, expected_) || payload.empty() || payload.size() > share_byte_limit)
        return false;
    receiving_ = false;
    address_ = std::move(address);
    payload_ = std::move(payload);
    port_ = port;
    cancel_.store(false);
    approved_.store(false);
    status_ = {SharePhase::starting, {}, {}, {}, 0};
    deadline_ = platform::monotonic_us() + 180000000;
    thread_ = platform::thread_start(entry, this, 2u * 1024u * 1024u, "ptv-share");
    if (!thread_)
        phase(SharePhase::failed);
    return thread_ != nullptr;
}
void *ShareTls::entry(void *context)
{
    auto &self = *static_cast<ShareTls *>(context);
    if (!self.run())
    {
        std::lock_guard lock(self.mutex_);
        self.payload_.clear();
        self.status_.phase = self.cancel_.load() ? SharePhase::cancelled : SharePhase::failed;
        self.status_.error =
            "Sharing failed or expired. Check the address and full fingerprint, then retry.";
    }
    return nullptr;
}
bool ShareTls::run()
{
#ifdef __PROSPERO__
    (void)sceNetInit();
#endif
    using Context = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>;
    using Session = std::unique_ptr<SSL, decltype(&SSL_free)>;
    using Certificate = std::unique_ptr<X509, decltype(&X509_free)>;
    using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
    using KeyContext = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
    using Method = std::unique_ptr<BIO_METHOD, decltype(&BIO_meth_free)>;
    Context ctx(SSL_CTX_new(TLS_method()), SSL_CTX_free);
    if (!ctx || !SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION))
        return false;
    SSL_CTX_set_options(ctx.get(), SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION);
    SSL_CTX_set_session_cache_mode(ctx.get(), SSL_SESS_CACHE_OFF);
    if (receiving_)
    {
        KeyContext keyctx(EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr), EVP_PKEY_CTX_free);
        EVP_PKEY *raw = nullptr;
        if (!keyctx || EVP_PKEY_keygen_init(keyctx.get()) <= 0 ||
            EVP_PKEY_CTX_set_ec_paramgen_curve_nid(keyctx.get(), NID_X9_62_prime256v1) <= 0 ||
            EVP_PKEY_keygen(keyctx.get(), &raw) <= 0)
            return false;
        Key key(raw, EVP_PKEY_free);
        Certificate cert(X509_new(), X509_free);
        if (!cert || X509_set_version(cert.get(), 2) != 1 ||
            ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1) != 1 ||
            !X509_gmtime_adj(X509_getm_notBefore(cert.get()), -3600) ||
            !X509_gmtime_adj(X509_getm_notAfter(cert.get()), 3600) ||
            X509_set_pubkey(cert.get(), key.get()) != 1)
            return false;
        auto *name = X509_get_subject_name(cert.get());
        if (!name ||
            X509_NAME_add_entry_by_txt(
                name, "CN", MBSTRING_ASC,
                reinterpret_cast<const unsigned char *>("ProsperoTV temporary sharing"), -1, -1,
                0) != 1 ||
            X509_set_issuer_name(cert.get(), name) != 1 ||
            X509_sign(cert.get(), key.get(), EVP_sha256()) <= 0 ||
            SSL_CTX_use_certificate(ctx.get(), cert.get()) != 1 ||
            SSL_CTX_use_PrivateKey(ctx.get(), key.get()) != 1)
            return false;
        std::lock_guard lock(mutex_);
        status_.fingerprint = fingerprint(cert.get());
    }
    Socket listener, connection;
    sockaddr_in peer{};
#ifdef __PROSPERO__
    peer.sin_len = sizeof(peer);
#endif
    peer.sin_family = AF_INET;
    peer.sin_port = htons(static_cast<std::uint16_t>(port_));
    if (receiving_)
    {
        listener.fd = socket(AF_INET, SOCK_STREAM, 0);
        const int reuse = 1;
        if (listener.fd < 0 || !nonblocking(listener.fd) ||
            setsockopt(listener.fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0 ||
            bind(listener.fd, reinterpret_cast<sockaddr *>(&peer), sizeof(peer)) != 0 ||
            listen(listener.fd, 1) != 0)
            return false;
        socklen_t size = sizeof(peer);
        if (getsockname(listener.fd, reinterpret_cast<sockaddr *>(&peer), &size) != 0)
            return false;
        {
            std::lock_guard lock(mutex_);
            status_.port = ntohs(peer.sin_port);
        }
        phase(SharePhase::listening);
        while (alive())
        {
            connection.fd = accept(listener.fd, reinterpret_cast<sockaddr *>(&peer), &size);
            if (connection.fd >= 0)
                break;
            if (!retry())
                return false;
            platform::sleep_ms(10);
        }
        close(listener.fd);
        listener.fd = -1;
        if (connection.fd < 0 || !nonblocking(connection.fd))
            return false;
        char address[INET_ADDRSTRLEN]{};
        if (!inet_ntop(AF_INET, &peer.sin_addr, address, sizeof(address)))
            return false;
        {
            std::lock_guard lock(mutex_);
            status_.peer = address;
        }
    }
    else
    {
        if (!local_address(address_, peer))
            return false;
        connection.fd = socket(AF_INET, SOCK_STREAM, 0);
        if (connection.fd < 0 || !nonblocking(connection.fd))
            return false;
        while (alive())
        {
            if (connect(connection.fd, reinterpret_cast<sockaddr *>(&peer), sizeof(peer)) == 0 ||
                socket_error() == EISCONN)
                break;
            if (!retry())
                return false;
            platform::sleep_ms(10);
        }
    }
    Method method(BIO_meth_new(BIO_TYPE_SOURCE_SINK, "ProsperoTV socket"), BIO_meth_free);
    if (!method || !BIO_meth_set_read(method.get(), bio_read) ||
        !BIO_meth_set_write(method.get(), bio_write) ||
        !BIO_meth_set_ctrl(method.get(), bio_control))
        return false;
    Session ssl(SSL_new(ctx.get()), SSL_free);
    BIO *bio = BIO_new(method.get());
    if (!ssl || !bio)
    {
        BIO_free(bio);
        return false;
    }
    BIO_set_data(bio, &connection.fd);
    BIO_set_init(bio, 1);
    SSL_set_bio(ssl.get(), bio, bio);
    if (receiving_)
        SSL_set_accept_state(ssl.get());
    else
        SSL_set_connect_state(ssl.get());
    const auto wait = [&](int result)
    {
        const int error = SSL_get_error(ssl.get(), result);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE)
            return false;
        platform::sleep_ms(10);
        return alive();
    };
    while (alive())
    {
        const int result = SSL_do_handshake(ssl.get());
        if (result == 1)
            break;
        if (!wait(result))
            return false;
    }
    if (!alive())
        return false;
    if (!receiving_)
    {
        Certificate cert(SSL_get_peer_certificate(ssl.get()), X509_free);
        if (fingerprint(cert.get()) != expected_)
            return false;
    }
    const auto transfer = [&](char *data, std::size_t size, bool writing)
    {
        std::size_t offset = 0;
        while (offset < size && alive())
        {
            const int result =
                writing ? SSL_write(ssl.get(), data + offset, static_cast<int>(size - offset))
                        : SSL_read(ssl.get(), data + offset, static_cast<int>(size - offset));
            if (result > 0)
                offset += static_cast<unsigned>(result);
            else if (!wait(result))
                return false;
        }
        return offset == size;
    };
    char ready = 'A';
    if (receiving_)
    {
        phase(SharePhase::approval);
        while (alive() && !approved_.load())
            platform::sleep_ms(10);
        if (!alive() || !transfer(&ready, 1, true))
            return false;
    }
    else if (!transfer(&ready, 1, false) || ready != 'A')
        return false;
    phase(SharePhase::transferring);
    std::uint32_t length = htonl(static_cast<std::uint32_t>(payload_.size()));
    if (!transfer(reinterpret_cast<char *>(&length), sizeof(length), !receiving_))
        return false;
    const auto size = ntohl(length);
    if (!size || size > share_byte_limit)
        return false;
    if (receiving_)
        payload_.resize(size);
    if (!transfer(payload_.data(), size, !receiving_))
        return false;
    ready = 'R';
    if (!transfer(&ready, 1, receiving_) || ready != 'R')
        return false;
    if (!receiving_)
        payload_.clear();
    phase(SharePhase::ready);
    return true;
}
} // namespace ptv
