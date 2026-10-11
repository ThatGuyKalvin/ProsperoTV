// ProsperoTV - Explicit send/receive flow for a temporary encrypted connection.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tv/shared.hpp"
#include "tv/share_tls.hpp"
#include "tv/backup.hpp"

namespace ptv
{
class ShareSheet
{
  public:
    explicit ShareSheet(Shared &shared) : shared_(shared)
    {
    }
    ~ShareSheet();
    void open(std::string own_address);
    void dismiss();
    bool is_open() const
    {
        return open_;
    }
    void handle(const InputFrame &input);
    void draw(ui::Canvas &canvas) const;
    StorageRequest take_request();

  private:
    enum class Page
    {
        choose,
        send_preview,
        address,
        fingerprint,
        transfer,
        receive_preview,
        result
    };
    static void address_result(const char *text, void *context);
    static void fingerprint_result(const char *text, void *context);
    Shared &shared_;
    ShareTls tls_;
    SharedLibrary library_;
    StorageRequest request_;
    std::string own_address_, address_, message_;
    Page page_ = Page::choose;
    bool open_ = false, receive_ = false, prompt_started_ = false;
    unsigned choice_ = 0;
};
} // namespace ptv
