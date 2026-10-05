// ProsperoTV - The update: a newer version is offered, fetched and installed.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tv/platform.hpp"
#include "tv/shared.hpp"

#include <string>

namespace ptv
{

// A modal over the whole menu. It opens with the offer ("Update now" or
// "Later"); accepted, a ring fills while the release downloads and unpacks,
// and when everything is staged the app closes itself so the new files can
// take the old ones' place. Nothing of the installed app changes before that
// last step, so "Cancel" and a failure leave it as it was.
class UpdateSheet
{
  public:
    explicit UpdateSheet(Shared &shared);

    void open(platform::UpdateOffer offer, ui::Feedback &feedback);
    bool is_open() const
    {
        return stage_ != Stage::closed;
    }
    // Still on screen: open, or fading out.
    bool visible() const;

    void handle(const InputFrame &input, ui::Feedback &feedback);
    void update(float dt, ui::Feedback &feedback);
    void draw(ui::Canvas &canvas) const;

    // True once the new version is staged and the closing picture has been
    // shown: the app must close now.
    bool wants_quit() const
    {
        return quit_;
    }

    // ---- where it is, for tests ----
    enum class Stage : std::uint8_t
    {
        closed,
        offer,
        working,    // starting, downloading, unpacking
        cancelling, // told to stop, waiting for it
        closing,    // staged: the app is about to close
        failed,
    };
    Stage stage() const
    {
        return stage_;
    }
    int focus() const
    {
        return focus_;
    }

  private:
    int button_count() const;
    const char *button_label(int index) const;
    void begin(ui::Feedback &feedback);
    void fail(std::string reason, ui::Feedback &feedback);
    void close();
    void draw_orb(ui::Canvas &canvas, float cx, float cy) const;
    void draw_steps(ui::Canvas &canvas, float x, float y, float width) const;

    Shared &shared_;
    platform::UpdateOffer offer_;
    platform::UpdateProgress progress_;
    Stage stage_ = Stage::closed;
    std::string failure_;
    int focus_ = 0;
    bool quit_ = false;

    tween::Spring fade_;
    tween::Bounce pop_;
    tween::Spring ring_;      // how full the ring is drawn
    tween::Spring stage_mix_; // 0 -> 1 after every change of stage: the text settles in
    tween::Spring focus_x_;   // the focused button, as an index that glides
    ui::Pulse press_;
    ui::Pulse ripple_;
    float age_ = 0.0f;       // seconds in this stage
    float closing_ = 0.0f;   // seconds since everything was staged
    float clock_ = 0.0f;     // free-running, for idle motion
};

} // namespace ptv
