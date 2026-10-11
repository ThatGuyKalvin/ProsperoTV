// ProsperoTV - One connection, explicit approval, certificate-pinned TLS.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace ptv
{
enum class SharePhase
{
    idle,
    starting,
    listening,
    approval,
    transferring,
    ready,
    failed,
    cancelled
};
struct ShareStatus
{
    SharePhase phase = SharePhase::idle;
    std::string fingerprint, peer, error;
    unsigned port = 0;
};
bool share_fingerprint(std::string_view input, std::string &normalized);
class ShareTls
{
  public:
    ~ShareTls();
    bool receive(unsigned port = 18127);
    bool transmit(std::string address, unsigned port, std::string fingerprint, std::string payload);
    void approve();
    void cancel();
    ShareStatus status() const;
    std::string take_payload();

  private:
    static void *entry(void *context);
    bool run();
    bool alive() const;
    void phase(SharePhase phase);
    mutable std::mutex mutex_;
    ShareStatus status_;
    std::string address_, expected_, payload_;
    unsigned port_ = 0;
    bool receiving_ = false;
    std::atomic<bool> cancel_{false}, approved_{false};
    std::uint64_t deadline_ = 0;
    void *thread_ = nullptr;
};
} // namespace ptv
