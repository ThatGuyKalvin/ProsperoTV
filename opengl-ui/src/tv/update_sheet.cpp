// ProsperoTV - The update: a newer version is offered, fetched and installed.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv/update_sheet.hpp"

#include "tv/draw.hpp"
#include "ui/components/overlay.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace ptv
{

namespace
{

constexpr float kTau = 6.2831853f;
constexpr Rect kPanel{(kWidth - 1040.0f) * 0.5f, (kHeight - 548.0f) * 0.5f, 1040.0f, 548.0f};
constexpr float kOrbRadius = 104.0f;
constexpr float kOrbThickness = 12.0f;
constexpr float kButtonHeight = 64.0f;
constexpr float kButtonWidth = 250.0f;
constexpr float kButtonGap = 16.0f;
// How long the closing picture stays before the app closes: long enough to
// read, and for the ring to finish and the tick to draw itself.
constexpr float kClosingSeconds = 2.8f;

std::string megabytes(std::uint64_t bytes)
{
    char text[32];
    const double value = static_cast<double>(bytes) / (1024.0 * 1024.0);
    std::snprintf(text, sizeof(text), value < 10.0 ? "%.1f MB" : "%.0f MB", value);
    return text;
}

// A content version as people say it: "01.000.020" is "1.000.020".
std::string spoken(const std::string &version)
{
    std::size_t start = 0;
    while (start + 1 < version.size() && version[start] == '0' && version[start + 1] != '.')
        ++start;
    return version.substr(start);
}

} // namespace

UpdateSheet::UpdateSheet(Shared &shared) : shared_(shared)
{
}

bool UpdateSheet::visible() const
{
    return stage_ != Stage::closed || fade_.value > 0.004f;
}

int UpdateSheet::button_count() const
{
    switch (stage_)
    {
    case Stage::offer:
    case Stage::failed:
        return 2;
    case Stage::working:
        return 1;
    default:
        break;
    }
    return 0;
}

const char *UpdateSheet::button_label(int index) const
{
    switch (stage_)
    {
    case Stage::offer:
        return index == 0 ? "Update now" : "Later";
    case Stage::failed:
        return index == 0 ? "Try again" : "Close";
    case Stage::working:
        return "Cancel";
    default:
        break;
    }
    return "";
}

void UpdateSheet::open(platform::UpdateOffer offer, ui::Feedback &feedback)
{
    offer_ = std::move(offer);
    progress_ = {};
    failure_.clear();
    stage_ = Stage::offer;
    focus_ = 0;
    quit_ = false;
    age_ = 0.0f;
    closing_ = 0.0f;
    fade_.snap(0.0f);
    pop_.snap(0.0f);
    ring_.snap(0.0f);
    stage_mix_.snap(0.0f);
    focus_x_.snap(0.0f);
    feedback.play(audio::Cue::modal_open);
}

void UpdateSheet::close()
{
    stage_ = Stage::closed;
}

void UpdateSheet::begin(ui::Feedback &feedback)
{
    progress_ = {};
    ring_.snap(0.0f);
    age_ = 0.0f;
    stage_mix_.snap(0.0f);
    focus_ = 0;
    focus_x_.snap(0.0f);
    if (platform::update_begin())
    {
        stage_ = Stage::working;
        progress_.phase = platform::UpdatePhase::starting;
        feedback.play(audio::Cue::launch);
    }
    else
    {
        fail("The update could not start. Is the console online?", feedback);
    }
}

void UpdateSheet::fail(std::string reason, ui::Feedback &feedback)
{
    platform::update_finish();
    failure_ = reason.empty() ? "The update did not finish." : std::move(reason);
    stage_ = Stage::failed;
    focus_ = 0;
    focus_x_.snap(0.0f);
    age_ = 0.0f;
    stage_mix_.snap(0.0f);
    feedback.play(audio::Cue::error);
}

void UpdateSheet::handle(const InputFrame &input, ui::Feedback &feedback)
{
    const int count = button_count();
    if (count == 0)
        return; // cancelling and closing take no answer
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const int next = focus_ + (input.nav == Direction::right ? 1 : -1);
        if (next < 0 || next >= count)
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.5f);
            return;
        }
        focus_ = next;
        feedback.play(audio::Cue::focus);
        return;
    }
    const bool confirm = input.is_pressed(Action::confirm);
    const bool back = input.is_pressed(Action::back);
    if (!confirm && !back)
        return;
    // Back is always the answer that changes nothing.
    const int choice = back ? count - 1 : focus_;
    if (confirm)
        press_.trigger();
    switch (stage_)
    {
    case Stage::offer:
    case Stage::failed:
        if (choice == 0)
        {
            begin(feedback);
        }
        else
        {
            feedback.play(audio::Cue::modal_close);
            close();
        }
        break;
    case Stage::working:
        platform::update_cancel();
        stage_ = Stage::cancelling;
        age_ = 0.0f;
        stage_mix_.snap(0.0f);
        feedback.play(audio::Cue::back);
        break;
    default:
        break;
    }
}

void UpdateSheet::update(float dt, ui::Feedback &feedback)
{
    using platform::UpdatePhase;
    clock_ += dt;
    age_ += dt;
    const bool shown = stage_ != Stage::closed;
    fade_.target = shown ? 1.0f : 0.0f;
    fade_.update(dt, shown ? 14.0f : 24.0f);
    pop_.target = shown ? 1.0f : 0.0f;
    pop_.update(dt, shown ? 13.0f : 24.0f, shown ? 0.62f : 1.0f);
    stage_mix_.target = 1.0f;
    stage_mix_.update(dt, 9.0f);
    focus_x_.target = static_cast<float>(focus_);
    focus_x_.update(dt, 18.0f);
    press_.update(dt, 7.0f);
    ripple_.update(dt, 1.6f);

    if (stage_ == Stage::working || stage_ == Stage::cancelling)
    {
        progress_ = platform::update_poll();
        switch (progress_.phase)
        {
        case UpdatePhase::ready:
            if (stage_ == Stage::cancelling)
            {
                platform::update_cancel();
                break;
            }
            if (!platform::update_apply())
            {
                fail("The new version was downloaded but could not be put in place.", feedback);
                break;
            }
            [[fallthrough]];
        case UpdatePhase::applying:
            stage_ = Stage::closing;
            closing_ = 0.0f;
            age_ = 0.0f;
            stage_mix_.snap(0.0f);
            ripple_.trigger();
            feedback.play(audio::Cue::saved);
            break;
        case UpdatePhase::failed:
            if (stage_ == Stage::cancelling)
            {
                platform::update_finish();
                close();
            }
            else
            {
                fail(progress_.error, feedback);
            }
            break;
        case UpdatePhase::cancelled:
            platform::update_finish();
            if (stage_ == Stage::cancelling)
                shared_.toasts.push(ui::StatusKind::info, "Update cancelled",
                                    "ProsperoTV is as it was.");
            close();
            break;
        default:
            break;
        }
    }
    if (stage_ == Stage::closing)
    {
        closing_ += dt;
        if (closing_ >= kClosingSeconds)
            quit_ = true;
    }

    // The ring: the download fills the first three quarters, unpacking the
    // rest, and it only ever grows.
    float wanted = ring_.target;
    switch (stage_)
    {
    case Stage::working:
    case Stage::cancelling:
    {
        const float part =
            progress_.total > 0
                ? tween::clamp01(static_cast<float>(static_cast<double>(progress_.done) /
                                                    static_cast<double>(progress_.total)))
                : 0.0f;
        if (progress_.phase == UpdatePhase::downloading)
            wanted = std::max(wanted, 0.75f * part);
        else if (progress_.phase == UpdatePhase::unpacking)
            wanted = std::max(wanted, 0.75f + 0.25f * part);
        break;
    }
    case Stage::closing:
        wanted = 1.0f;
        break;
    case Stage::offer:
        wanted = 0.0f;
        break;
    default:
        break;
    }
    ring_.target = wanted;
    ring_.update(dt, 7.0f);
}

void UpdateSheet::draw_orb(ui::Canvas &canvas, float cx, float cy) const
{
    using platform::UpdatePhase;
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const bool still = shared_.settings.reduced_motion;
    const float t = still ? 0.0f : clock_;
    const bool failed = stage_ == Stage::failed;
    const Color light = failed ? tone::bad : tone::accent;
    const float breathe = still ? 0.5f : 0.5f + 0.5f * std::sin(t * 1.9f);

    // The light it gives off, and the track.
    list.glow({cx - kOrbRadius, cy - kOrbRadius, kOrbRadius * 2.0f, kOrbRadius * 2.0f}, kOrbRadius,
              70.0f + 26.0f * breathe, light.with_alpha(0.16f + 0.10f * breathe));
    list.circle(cx, cy, kOrbRadius - kOrbThickness - 6.0f, tone::night.with_alpha(0.55f));
    list.ring(cx, cy, kOrbRadius, kOrbThickness, kWhite.with_alpha(0.10f));

    const float filled = tween::clamp01(ring_.value);
    switch (stage_)
    {
    case Stage::offer:
    {
        // Two arcs circle each other, waiting.
        const float turn = t * 0.9f;
        list.arc(cx, cy, kOrbRadius, kOrbThickness, turn, 1.15f, tone::accent);
        list.arc(cx, cy, kOrbRadius, kOrbThickness, turn + kTau * 0.5f, 0.55f,
                 tone::ember.with_alpha(0.85f));
        // An arrow that keeps arriving.
        const float drop = still ? 0.0f : 7.0f * std::sin(t * 2.6f);
        const float tip = cy + 34.0f + drop;
        list.line(cx, cy - 40.0f + drop, cx, tip, 9.0f, tone::cream);
        list.line(cx - 26.0f, tip - 26.0f, cx, tip, 9.0f, tone::cream);
        list.line(cx + 26.0f, tip - 26.0f, cx, tip, 9.0f, tone::cream);
        list.rounded_rect({cx - 36.0f, cy + 56.0f, 72.0f, 8.0f}, 4.0f, tone::cream.with_alpha(0.55f));
        break;
    }
    case Stage::working:
    case Stage::cancelling:
    {
        const bool counting = filled > 0.004f && stage_ == Stage::working;
        if (!counting)
        {
            // Nothing to count yet: an arc that travels and breathes.
            const float sweep = 1.2f + 0.7f * std::sin(t * 2.2f);
            list.arc(cx, cy, kOrbRadius, kOrbThickness, t * 4.2f, sweep,
                     stage_ == Stage::cancelling ? tone::wait : tone::accent);
        }
        else
        {
            const float sweep = kTau * filled;
            list.arc(cx, cy, kOrbRadius + 5.0f, kOrbThickness + 10.0f, 0.0f, sweep,
                     tone::accent.with_alpha(0.18f));
            list.arc(cx, cy, kOrbRadius, kOrbThickness, 0.0f, sweep, tone::accent);
            // The head of the arc burns brighter, and sparks run along it.
            const float head = sweep - 1.5708f;
            const float mid = kOrbRadius - kOrbThickness * 0.5f;
            const float hx = cx + std::cos(head) * mid;
            const float hy = cy + std::sin(head) * mid;
            list.glow({hx - 6.0f, hy - 6.0f, 12.0f, 12.0f}, 6.0f, 22.0f,
                      tone::cream.with_alpha(0.55f));
            list.circle(hx, hy, kOrbThickness * 0.5f + 1.0f, tone::cream);
            if (!still)
            {
                const float run = std::fmod(t * 0.55f, 1.0f);
                list.arc(cx, cy, kOrbRadius, kOrbThickness, sweep * run, std::min(0.35f, sweep),
                         kWhite.with_alpha(0.22f * std::sin(run * 3.14159f)));
            }
        }
        if (counting)
        {
            char percent[8];
            std::snprintf(percent, sizeof(percent), "%d", static_cast<int>(filled * 100.0f + 0.5f));
            const float number = fonts.display.measure(percent, 68.0f);
            ui::text(list, fonts.display, percent, cx - 9.0f, cy + 24.0f, 68.0f, tone::cream,
                     gfx::Align::center);
            ui::text(list, fonts.semibold, "%", cx - 9.0f + number * 0.5f + 5.0f, cy + 22.0f, 26.0f,
                     tone::cream.with_alpha(0.7f));
        }
        else
        {
            // Three dots take turns.
            for (int i = 0; i < 3; ++i)
            {
                const float beat =
                    still ? 0.6f : 0.5f + 0.5f * std::sin(t * 5.0f - static_cast<float>(i) * 0.9f);
                list.circle(cx + (static_cast<float>(i) - 1.0f) * 26.0f, cy, 6.0f + 3.0f * beat,
                            tone::cream.with_alpha(0.45f + 0.5f * beat));
            }
        }
        break;
    }
    case Stage::closing:
    {
        list.arc(cx, cy, kOrbRadius + 5.0f, kOrbThickness + 10.0f, 0.0f, kTau * filled,
                 tone::good.with_alpha(0.18f));
        list.arc(cx, cy, kOrbRadius, kOrbThickness, 0.0f, kTau * filled, tone::good);
        // A ring of light leaves it, and the tick draws itself.
        const float wave = 1.0f - tween::clamp01(ripple_.value);
        if (!still && wave < 1.0f)
            list.ring(cx, cy, kOrbRadius + 90.0f * wave, 4.0f,
                      tone::good.with_alpha(0.5f * (1.0f - wave)));
        const float drawn = still ? 1.0f : tween::cubic_out(tween::clamp01((closing_ - 0.15f) / 0.5f));
        const float ax = cx - 38.0f, ay = cy + 2.0f;
        const float bx = cx - 10.0f, by = cy + 30.0f;
        const float ex = cx + 42.0f, ey = cy - 30.0f;
        const float first = tween::clamp01(drawn / 0.4f);
        const float second = tween::clamp01((drawn - 0.4f) / 0.6f);
        if (first > 0.0f)
            list.line(ax, ay, tween::lerp(ax, bx, first), tween::lerp(ay, by, first), 11.0f,
                      tone::cream);
        if (second > 0.0f)
            list.line(bx, by, tween::lerp(bx, ex, second), tween::lerp(by, ey, second), 11.0f,
                      tone::cream);
        break;
    }
    case Stage::failed:
        list.ring(cx, cy, kOrbRadius, kOrbThickness, tone::bad.with_alpha(0.75f));
        list.rounded_rect({cx - 6.0f, cy - 44.0f, 12.0f, 56.0f}, 6.0f, tone::cream);
        list.circle(cx, cy + 36.0f, 8.0f, tone::cream);
        break;
    default:
        break;
    }
}

// Download, unpack, restart: where the update is.
void UpdateSheet::draw_steps(ui::Canvas &canvas, float x, float y, float width) const
{
    using platform::UpdatePhase;
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    static constexpr const char *kNames[] = {"Download", "Unpack", "Restart"};
    int at = 0;
    if (stage_ == Stage::closing)
        at = 3;
    else if (progress_.phase == UpdatePhase::unpacking || progress_.phase == UpdatePhase::ready)
        at = 1;
    const bool still = shared_.settings.reduced_motion;
    const float step = width / 3.0f;
    for (int i = 0; i < 3; ++i)
    {
        const float left = x + step * static_cast<float>(i);
        const bool done = i < at;
        const bool current = i == at && stage_ == Stage::working;
        const Color color =
            done ? tone::good : current ? tone::accent : kWhite.with_alpha(0.26f);
        const float beat = current && !still ? 0.5f + 0.5f * std::sin(clock_ * 4.0f) : 0.0f;
        list.rounded_rect({left, y, step - 12.0f, 5.0f}, 2.5f, kWhite.with_alpha(0.10f));
        float filled = done ? 1.0f : 0.0f;
        if (current && progress_.total > 0)
            filled = tween::clamp01(static_cast<float>(static_cast<double>(progress_.done) /
                                                       static_cast<double>(progress_.total)));
        if (filled > 0.0f)
            list.rounded_rect({left, y, std::max(5.0f, (step - 12.0f) * filled), 5.0f}, 2.5f, color);
        if (current)
            list.glow({left, y, 10.0f, 5.0f}, 2.5f, 8.0f + 6.0f * beat, tone::accent.with_alpha(0.5f));
        ui::text(list, fonts.semibold, kNames[i], left, y + 34.0f, 18.0f,
                 done || current ? shared_.theme.text : shared_.theme.text_muted, gfx::Align::left,
                 1.5f);
    }
}

void UpdateSheet::draw(ui::Canvas &canvas) const
{
    using platform::UpdatePhase;
    if (!visible())
        return;
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = shared_.theme;
    const bool still = shared_.settings.reduced_motion;
    const float fade = tween::clamp01(fade_.value);
    const float pop = pop_.value;

    list.rounded_rect({0.0f, 0.0f, kWidth, kHeight}, 0.0f, tone::night.with_alpha(0.66f * fade));
    list.push_opacity(tween::clamp01(fade * 1.5f));
    list.push_transform(still ? 1.0f : tween::lerp(0.9f, 1.0f, pop), kPanel.cx(), kPanel.cy(), 0.0f,
                        still ? 0.0f : 30.0f * (1.0f - pop));

    list.shadow({kPanel.x, kPanel.y + 30.0f, kPanel.w, kPanel.h}, theme.radius_card, 80.0f,
                Color::rgb(0x000000, 0.5f));
    ui::draw_overlay_panel(canvas, theme, kPanel, true, 0.6f);
    ui::Painter paint(list, fonts, theme, canvas.glass);

    // ---- left: the orb ----
    const float orb_x = kPanel.x + 76.0f + kOrbRadius;
    const float orb_y = kPanel.y + 66.0f + kOrbRadius + 20.0f;
    const float grow = still ? 1.0f : tween::lerp(0.6f, 1.0f, pop);
    list.push_transform(grow, orb_x, orb_y, 0.0f, 0.0f);
    draw_orb(canvas, orb_x, orb_y);
    list.pop_transform();

    // ---- right: what is happening ----
    const float x = kPanel.x + 76.0f + kOrbRadius * 2.0f + 64.0f;
    const float right = kPanel.x + kPanel.w - 64.0f;
    const float width = right - x;
    const float mix = tween::clamp01(stage_mix_.value);
    const float slide = still ? 0.0f : 16.0f * (1.0f - mix);
    list.push_opacity(mix);
    list.push_transform(1.0f, 0.0f, 0.0f, slide, 0.0f);

    const char *kicker = "UPDATE AVAILABLE";
    std::string title = "ProsperoTV " + spoken(offer_.version);
    std::string body;
    std::string detail;
    switch (stage_)
    {
    case Stage::offer:
        body = "A newer version is on homebrew.page. It downloads and installs here; your "
               "sources, favorites and settings stay as they are.";
        if (offer_.size > 0)
            detail = megabytes(offer_.size) + " download";
        break;
    case Stage::working:
        kicker = "UPDATING";
        if (progress_.phase == UpdatePhase::unpacking || progress_.phase == UpdatePhase::ready)
        {
            title = "Unpacking";
            body = "The new version is being put together beside the one you are using.";
        }
        else if (progress_.phase == UpdatePhase::downloading)
        {
            title = "Downloading";
            body = "Version " + spoken(offer_.version) + " is on its way to this console.";
        }
        else
        {
            title = "Getting ready";
            body = "Asking the console to make room for the new version.";
        }
        if (progress_.total > 0 && progress_.phase == UpdatePhase::downloading)
            detail = megabytes(progress_.done) + " of " + megabytes(progress_.total);
        if (!progress_.time_left.empty())
            detail += (detail.empty() ? "" : "  \xC2\xB7  ") + progress_.time_left;
        break;
    case Stage::cancelling:
        kicker = "UPDATING";
        title = "Stopping";
        body = "Nothing was changed. ProsperoTV stays as it is.";
        break;
    case Stage::closing:
        kicker = "READY";
        title = "Version " + spoken(offer_.version) + " is ready";
        body = "ProsperoTV closes now and the new version takes its place. Open it again from "
               "the home screen.";
        break;
    case Stage::failed:
        kicker = "UPDATE";
        title = "It didn't work this time";
        body = failure_ + " Nothing was changed.";
        break;
    default:
        break;
    }

    float y = kPanel.y + 96.0f;
    ui::text(list, fonts.semibold, kicker, x, y, 17.0f,
             stage_ == Stage::failed ? tone::bad
                                     : stage_ == Stage::closing ? tone::good : tone::accent,
             gfx::Align::left, 4.0f);
    y += 62.0f;
    const float title_size = paint.heading_width(title, 50.0f) <= width ? 50.0f : 40.0f;
    paint.heading(title, x - 2.0f, y, title_size);
    y += 30.0f;

    if (stage_ == Stage::offer || stage_ == Stage::closing)
    {
        // From this version to that one.
        const std::string from = spoken(offer_.installed.empty() ? "?" : offer_.installed);
        const std::string to = spoken(offer_.available.empty() ? offer_.version : offer_.available);
        const float from_width = fonts.mono.measure(from, 21.0f) + 32.0f;
        const float to_width = fonts.mono.measure(to, 21.0f) + 32.0f;
        const Rect old_pill{x, y, from_width, 38.0f};
        list.rounded_rect(old_pill, 19.0f, kWhite.with_alpha(0.08f));
        ui::text(list, fonts.mono, from, old_pill.cx(), baseline_for(old_pill.cy(), 21.0f), 21.0f,
                 theme.text_muted, gfx::Align::center);
        // Chevrons run from the old to the new.
        const float lane = x + from_width + 14.0f;
        for (int i = 0; i < 3; ++i)
        {
            const float phase =
                still ? 0.5f : std::fmod(clock_ * 1.3f - static_cast<float>(i) * 0.22f + 4.0f, 1.0f);
            const float cx = lane + 10.0f + static_cast<float>(i) * 16.0f;
            const Color color = tone::accent.with_alpha(0.25f + 0.75f * std::sin(phase * 3.14159f));
            list.line(cx - 4.0f, y + 11.0f, cx + 4.0f, y + 19.0f, 3.0f, color);
            list.line(cx + 4.0f, y + 19.0f, cx - 4.0f, y + 27.0f, 3.0f, color);
        }
        const Rect new_pill{lane + 62.0f, y, to_width, 38.0f};
        list.glow(new_pill, 19.0f, 16.0f, tone::accent.with_alpha(0.3f));
        list.rounded_rect(new_pill, 19.0f, tone::accent);
        ui::text(list, fonts.mono, to, new_pill.cx(), baseline_for(new_pill.cy(), 21.0f), 21.0f,
                 tone::ink, gfx::Align::center);
        y += 38.0f;
    }
    y += 44.0f;
    y = ui::paragraph(list, fonts.regular, body, x, y, 24.0f, width, 35.0f, theme.text_muted, 3);
    if (!detail.empty())
        ui::text(list, fonts.semibold, fonts.semibold.font->fit(detail, 22.0f, width), x, y + 14.0f,
                 22.0f, theme.text);
    list.pop_transform();
    list.pop_opacity();

    if (stage_ == Stage::working || stage_ == Stage::closing)
        draw_steps(canvas, x, kPanel.y + kPanel.h - 196.0f, width);

    // ---- the answers ----
    const int count = button_count();
    const float row_y = kPanel.y + kPanel.h - 58.0f - kButtonHeight;
    const float total = static_cast<float>(count) * kButtonWidth +
                        static_cast<float>(std::max(0, count - 1)) * kButtonGap;
    const float first = right - total;
    for (int i = 0; i < count; ++i)
    {
        const Rect r{first + static_cast<float>(i) * (kButtonWidth + kButtonGap), row_y,
                     kButtonWidth, kButtonHeight};
        ui::Look look;
        look.focus = tween::clamp01(1.0f - std::fabs(focus_x_.value - static_cast<float>(i)));
        look.press = i == focus_ ? tween::clamp01(press_.value) : 0.0f;
        const bool main = i == 0 && stage_ != Stage::working;
        paint.button(r, button_label(i), main ? ui::ButtonKind::primary : ui::ButtonKind::secondary,
                     look);
    }
    if (count > 0)
    {
        const float at = first + focus_x_.value * (kButtonWidth + kButtonGap);
        paint.focus_ring({at, row_y, kButtonWidth, kButtonHeight},
                         paint.control_radius({at, row_y, kButtonWidth, kButtonHeight}), 1.0f);
    }
    else if (stage_ == Stage::closing)
    {
        // Time runs out along the bottom of the panel.
        const float left = 1.0f - tween::clamp01(closing_ / kClosingSeconds);
        list.rounded_rect({x, row_y + kButtonHeight - 6.0f, width, 4.0f}, 2.0f,
                          kWhite.with_alpha(0.10f));
        list.rounded_rect({x, row_y + kButtonHeight - 6.0f, std::max(4.0f, width * left), 4.0f}, 2.0f,
                          tone::good);
    }

    list.pop_transform();
    list.pop_opacity();
}

} // namespace ptv
