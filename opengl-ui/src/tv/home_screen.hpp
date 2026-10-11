// ProsperoTV - Continue watching, favorites and usual channels.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tv/shared.hpp"
namespace ptv
{
class HomeScreen
{
  public:
    explicit HomeScreen(Shared &shared) : shared_(shared)
    {
    }
    void update();
    void handle(const InputFrame &, ui::Feedback &);
    void draw(ui::Canvas &) const;
    std::vector<std::string> image_urls() const;

  private:
    Shared &shared_;
    std::array<std::vector<Model::HomeItem>, 3> rows_;
    std::array<int, 3> focus_{}, top_{};
    int column_ = 0;
    unsigned revision_ = ~0u;
};
} // namespace ptv
