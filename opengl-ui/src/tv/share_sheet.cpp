// ProsperoTV - Credentials leave only after certificate verification and local consent.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/share_sheet.hpp"
#include "tv/draw.hpp"
#include "tv/i18n.hpp"
#include "iptv_ime.h"
#include <utility>

namespace ptv
{
ShareSheet::~ShareSheet()
{
    dismiss();
}
void ShareSheet::dismiss()
{
    if (open_ && (page_ == Page::address || page_ == Page::fingerprint))
        iptv_ime_cancel();
    tls_.cancel();
    library_ = {};
    open_ = false;
}
void ShareSheet::open(std::string own_address)
{
    dismiss();
    if (!shared_.model.require_parent())
        return;
    own_address_ = std::move(own_address);
    if (own_address_.starts_with("http://"))
        own_address_.erase(0, 7);
    own_address_ = own_address_.substr(0, own_address_.find_first_of(":/"));
    open_ = true;
    choice_ = 0;
    page_ = Page::choose;
    message_.clear();
    request_ = {};
}
void ShareSheet::address_result(const char *text, void *context)
{
    auto &self = *static_cast<ShareSheet *>(context);
    if (!self.open_)
        return;
    if (!text)
    {
        self.page_ = Page::send_preview;
        return;
    }
    self.address_ = text;
    self.page_ = Page::fingerprint;
    self.prompt_started_ = false;
}
void ShareSheet::fingerprint_result(const char *text, void *context)
{
    auto &self = *static_cast<ShareSheet *>(context);
    if (!self.open_)
        return;
    if (!text)
    {
        self.page_ = Page::send_preview;
        return;
    }
    std::string bytes;
    if (!self.shared_.model.require_parent() || !encode_shared_library(self.library_, bytes) ||
        !self.tls_.transmit(self.address_, 18127, text, std::move(bytes)))
    {
        self.message_ = tr("Enter a local IPv4 address and the complete 64-digit fingerprint.");
        self.page_ = Page::send_preview;
        return;
    }
    self.receive_ = false;
    self.page_ = Page::transfer;
}
StorageRequest ShareSheet::take_request()
{
    return std::exchange(request_, {});
}
void ShareSheet::handle(const InputFrame &input)
{
    if (!open_)
        return;
    const auto &parental = shared_.model.parental();
    if (parental.enabled() && !parental.unlocked())
    {
        dismiss();
        return;
    }
    if (input.is_pressed(Action::back))
    {
        dismiss();
        return;
    }
    if (page_ == Page::address || page_ == Page::fingerprint)
    {
        if (iptv_ime_busy())
            return;
        if (page_ == Page::fingerprint && !prompt_started_)
        {
            prompt_started_ = true;
            iptv_ime_request_prompt("", tr("Receiver's full SHA-256 fingerprint"),
                                    tr("Copy all 64 hex digits from the receiving TV"), 96,
                                    fingerprint_result, this);
        }
        else
            page_ = Page::send_preview;
        return;
    }
    if (page_ == Page::transfer)
    {
        const auto status = tls_.status();
        if (status.phase == SharePhase::approval && input.is_pressed(Action::confirm))
            tls_.approve();
        if (status.phase == SharePhase::ready)
        {
            if (!receive_)
            {
                message_ = tr("Sent securely. The other TV still needs to approve the import.");
                page_ = Page::result;
            }
            else if (decode_shared_library(tls_.take_payload(), library_))
                page_ = Page::receive_preview;
            else
            {
                message_ = tr("The shared library is invalid. No settings were changed.");
                page_ = Page::result;
            }
        }
        else if (status.phase == SharePhase::failed || status.phase == SharePhase::cancelled)
        {
            message_ = tr(
                "Sharing failed or expired. Check the address and full fingerprint, then retry.");
            page_ = Page::result;
        }
        return;
    }
    if (page_ == Page::choose && input.nav != Direction::none)
        choice_ ^= 1;
    if (!input.is_pressed(Action::confirm))
        return;
    if (page_ == Page::choose)
    {
        if (choice_ == 0)
        {
            if (shared_.model.export_shared_library(library_))
                page_ = Page::send_preview;
            else
            {
                message_ = tr("The library could not be prepared for sharing.");
                page_ = Page::result;
            }
        }
        else
        {
            receive_ = true;
            if (tls_.receive())
                page_ = Page::transfer;
            else
            {
                message_ = tr("Sharing could not start.");
                page_ = Page::result;
            }
        }
    }
    else if (page_ == Page::send_preview)
    {
        page_ = Page::address;
        prompt_started_ = true;
        iptv_ime_request_prompt("", tr("Receiving console's IPv4 address"), "192.168.4.40", 15,
                                address_result, this);
    }
    else if (page_ == Page::receive_preview && shared_.model.require_parent())
    {
        request_ = {
            StorageAction::share_import, {}, std::make_shared<SharedLibrary>(std::move(library_))};
        dismiss();
    }
    else if (page_ == Page::result)
        dismiss();
}
void ShareSheet::draw(ui::Canvas &canvas) const
{
    if (!open_)
        return;
    auto &list = canvas.list;
    const auto &fonts = canvas.fonts;
    const auto &theme = shared_.theme;
    list.push_clip({0, 0, kWidth, kHeight});
    list.rounded_rect({0, 0, kWidth, kHeight}, 0, Color{0, 0, 0, 0.85f});
    draw_glass(canvas, theme, {220, 110, 1480, 860}, 24);
    ui::text(list, fonts.semibold, tr("Share with another console"), 280, 184, 38, theme.text);
    float y = 245;
    const auto line = [&](std::string_view value, float size = 24)
    {
        ui::text(list, fonts.regular, fonts.regular.font->fit(value, size, 1360), 280, y, size,
                 theme.text);
        y += 42;
    };
    if (page_ == Page::choose)
    {
        line(tr("Sources include account passwords and HTTP credentials."));
        line(tr("Only share with a console you trust on your home network."));
        y += 30;
        line(choice_ == 0 ? "> " + std::string(tr("Send sources, favorites and folders"))
                          : tr("Send sources, favorites and folders"));
        line(choice_ == 1 ? "> " + std::string(tr("Receive from another console"))
                          : tr("Receive from another console"));
    }
    else if (page_ == Page::send_preview || page_ == Page::receive_preview)
    {
        line(std::to_string(library_.sources.size()) + " " + tr("network sources") + ", " +
             std::to_string(library_.favorites.size()) + " " + tr("favorites") + ", " +
             std::to_string(library_.folders.size()) + " " + tr("folders"));
        for (unsigned i = 0; i < library_.sources.size() && i < 5; ++i)
            line(library_.sources[i].source.name);
        line(std::to_string(library_.skipped_local) + " " +
             tr("USB playlists skipped (not portable)."));
        line(tr("Existing accounts stay unchanged. Favorites and matching folders merge."), 22);
        line(tr("Parental controls, history, paired phones and settings stay on this TV."), 22);
        if (!message_.empty())
            line(message_, 20);
        line(page_ == Page::send_preview
                 ? tr("Confirm to enter the other TV's address and fingerprint.")
                 : tr("Confirm to import into the current profile."),
             22);
    }
    else if (page_ == Page::transfer)
    {
        const auto status = tls_.status();
        if (receive_)
        {
            line(own_address_.empty() ? tr("Use this console's IPv4 address, port 18127.")
                                      : own_address_ + ":18127");
            line(tr("Full SHA-256 certificate fingerprint (verify on the sending TV):"), 22);
            line(status.fingerprint.substr(0, 32), 28);
            line(status.fingerprint.size() >= 32 ? status.fingerprint.substr(32) : "", 28);
        }
        line(tr("This temporary connection expires after three minutes."), 22);
        if (status.phase == SharePhase::approval)
        {
            line(tr("Connection from: ") + status.peer);
            line(tr("Confirm only if you started sending from that console."), 22);
            line(tr("No library is imported until you review it on this TV."), 22);
        }
        else
            line(receive_ ? tr("Waiting for a secure transfer...")
                          : tr("Connecting securely; approve on the receiving TV..."));
    }
    else if (page_ == Page::result)
        line(message_, 22);
    ui::text(list, fonts.regular, tr("Cross: continue    Circle: cancel"), 280, 920, 22,
             theme.text);
    list.pop_clip();
}
} // namespace ptv
