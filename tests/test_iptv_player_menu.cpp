/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_picture.h"
#include "iptv_player_menu.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace
{

using iptv::player_menu::Change;
using iptv::player_menu::Info;
using iptv::player_menu::Menu;
using iptv::player_menu::Setting;

const char kAudio[8][48] = {"English 5.1", "French Stereo", "Commentary"};
const char kSubtitles[8][48] = {"English", "English SDH"};

Info FilmInfo()
{
    Info info;
    info.tracks = true;
    info.audio_track = 1;
    info.audio_tracks = 3;
    info.audio_label = kAudio[0];
    info.audio_labels = kAudio;
    info.subtitle_track = 0;
    info.subtitle_tracks = 2;
    info.subtitle_labels = kSubtitles;
    info.listed = 8;
    info.frame_milli = 1778;
    return info;
}

iptv_osd_state_t Filled(const Menu &menu, const Info &info)
{
    iptv_osd_state_t state{};
    menu.Fill(info, &state);
    return state;
}

TEST(PlayerMenu, ClosedMenuFillsNothing)
{
    Menu menu;
    const iptv_osd_state_t state = Filled(menu, FilmInfo());
    EXPECT_EQ(state.menu_rows, 0u);
}

TEST(PlayerMenu, SettingsPageHasSectionsAndValues)
{
    Menu menu;
    menu.Open();
    const iptv_osd_state_t state = Filled(menu, FilmInfo());
    ASSERT_EQ(state.menu_rows, 7u);
    EXPECT_EQ(state.menu_page, static_cast<std::uint32_t>(IPTV_OSD_MENU_SETTINGS));
    EXPECT_STREQ(state.menu_title, "Playback");
    EXPECT_EQ(state.menu[0].kind, static_cast<std::uint32_t>(IPTV_OSD_ROW_HEADER));
    EXPECT_STREQ(state.menu[0].label, "Sound");
    EXPECT_STREQ(state.menu[1].label, "Audio");
    EXPECT_STREQ(state.menu[1].value, "English 5.1");
    EXPECT_STREQ(state.menu[2].value, "Off");
    EXPECT_EQ(state.menu[3].kind, static_cast<std::uint32_t>(IPTV_OSD_ROW_STEPPER));
    EXPECT_STREQ(state.menu[3].value, "0 ms");
    EXPECT_STREQ(state.menu[4].label, "Picture");
    EXPECT_STREQ(state.menu[5].label, "Aspect ratio");
    EXPECT_STREQ(state.menu[5].value, "Auto \xC2\xB7 16:9");
    EXPECT_STREQ(state.menu[6].value, "Fit");
    // It opens on the aspect ratio.
    EXPECT_EQ(state.menu_selected, 5u);
}

TEST(PlayerMenu, LiveChannelsOfferOnlyPictureAndDelay)
{
    Menu menu;
    menu.Open();
    Info info;
    info.aspect = IPTV_ASPECT_4_3;
    const iptv_osd_state_t state = Filled(menu, info);
    ASSERT_EQ(state.menu_rows, 5u);
    EXPECT_STREQ(state.menu[1].label, "Audio delay");
    EXPECT_STREQ(state.menu[3].value, "4:3");
}

TEST(PlayerMenu, UpAndDownSkipHeadersAndWrap)
{
    Menu menu;
    menu.Open();
    const Info info = FilmInfo();
    menu.Input(IPTV_INPUT_DOWN, info);
    EXPECT_EQ(menu.focus(), Setting::zoom);
    menu.Input(IPTV_INPUT_DOWN, info);
    EXPECT_EQ(menu.focus(), Setting::audio);
    menu.Input(IPTV_INPUT_UP, info);
    EXPECT_EQ(menu.focus(), Setting::zoom);
    EXPECT_EQ(Filled(menu, info).menu_selected, 6u);
}

TEST(PlayerMenu, AspectPickerShowsShapesAndPicks)
{
    Menu menu;
    menu.Open();
    Info info = FilmInfo();
    info.frame_milli = 2390;
    EXPECT_EQ(menu.Input(IPTV_INPUT_CROSS, info).kind, Change::Kind::none);
    ASSERT_TRUE(menu.picking());
    iptv_osd_state_t state = Filled(menu, info);
    EXPECT_EQ(state.menu_page, static_cast<std::uint32_t>(IPTV_OSD_MENU_PICKER));
    EXPECT_STREQ(state.menu_title, "Aspect ratio");
    ASSERT_EQ(state.menu_rows, static_cast<std::uint32_t>(IPTV_ASPECT_COUNT));
    EXPECT_EQ(state.menu_selected, 0u); // Auto is in use
    EXPECT_EQ(state.menu[0].checked, 1u);
    EXPECT_STREQ(state.menu[0].value, "2.39:1");
    EXPECT_EQ(state.menu[0].preview_milli, 2390u);
    EXPECT_EQ(state.menu[2].preview, static_cast<std::uint32_t>(IPTV_OSD_PREVIEW_SHAPE));
    EXPECT_EQ(state.menu[2].preview_milli, 1333u);
    EXPECT_EQ(state.menu[5].preview, static_cast<std::uint32_t>(IPTV_OSD_PREVIEW_STRETCH));

    menu.Input(IPTV_INPUT_DOWN, info);
    menu.Input(IPTV_INPUT_DOWN, info);
    const Change change = menu.Input(IPTV_INPUT_CROSS, info);
    EXPECT_EQ(change.kind, Change::Kind::aspect);
    EXPECT_EQ(change.value, IPTV_ASPECT_4_3);
    EXPECT_FALSE(menu.picking());
    EXPECT_EQ(menu.focus(), Setting::aspect);
}

TEST(PlayerMenu, PickerOpensAtTheValueInUseAndCircleGoesBack)
{
    Menu menu;
    menu.Open();
    Info info = FilmInfo();
    info.zoom = IPTV_ZOOM_133;
    menu.Input(IPTV_INPUT_DOWN, info);
    menu.Input(IPTV_INPUT_RIGHT, info);
    ASSERT_TRUE(menu.picking());
    const iptv_osd_state_t state = Filled(menu, info);
    EXPECT_EQ(state.menu_selected, 2u);
    EXPECT_EQ(state.menu[2].preview, static_cast<std::uint32_t>(IPTV_OSD_PREVIEW_ZOOM));
    EXPECT_EQ(state.menu[2].preview_milli, 1333u);
    EXPECT_EQ(menu.Input(IPTV_INPUT_CIRCLE, info).kind, Change::Kind::none);
    EXPECT_FALSE(menu.picking());
    EXPECT_TRUE(menu.is_open());
    EXPECT_EQ(menu.Input(IPTV_INPUT_CIRCLE, info).kind, Change::Kind::close);
    EXPECT_FALSE(menu.is_open());
}

TEST(PlayerMenu, TrackPickersListEveryTrack)
{
    Menu menu;
    menu.Open();
    const Info info = FilmInfo();
    menu.Input(IPTV_INPUT_DOWN, info); // zoom
    menu.Input(IPTV_INPUT_DOWN, info); // audio
    menu.Input(IPTV_INPUT_CROSS, info);
    iptv_osd_state_t state = Filled(menu, info);
    ASSERT_EQ(state.menu_rows, 3u);
    EXPECT_STREQ(state.menu[1].label, "French Stereo");
    menu.Input(IPTV_INPUT_DOWN, info);
    Change change = menu.Input(IPTV_INPUT_CROSS, info);
    EXPECT_EQ(change.kind, Change::Kind::audio);
    EXPECT_EQ(change.value, 2);

    menu.Input(IPTV_INPUT_DOWN, info); // subtitles
    menu.Input(IPTV_INPUT_CROSS, info);
    state = Filled(menu, info);
    ASSERT_EQ(state.menu_rows, 3u);
    EXPECT_STREQ(state.menu[0].label, "Off");
    EXPECT_EQ(state.menu[0].checked, 1u);
    EXPECT_STREQ(state.menu[2].label, "English SDH");
    menu.Input(IPTV_INPUT_UP, info);
    change = menu.Input(IPTV_INPUT_CROSS, info);
    EXPECT_EQ(change.kind, Change::Kind::subtitles);
    EXPECT_EQ(change.value, 2);
}

TEST(PlayerMenu, AudioDelayStepsInPlace)
{
    Menu menu;
    menu.Open();
    Info info = FilmInfo();
    menu.Input(IPTV_INPUT_UP, info);
    ASSERT_EQ(menu.focus(), Setting::audio_delay);
    Change change = menu.Input(IPTV_INPUT_LEFT, info);
    EXPECT_EQ(change.kind, Change::Kind::audio_delay);
    EXPECT_EQ(change.value, -1);
    EXPECT_FALSE(menu.picking());
    info.audio_delay_us = 150000;
    EXPECT_STREQ(Filled(menu, info).menu[3].value, "+150 ms");
    EXPECT_EQ(menu.Input(IPTV_INPUT_CROSS, info).kind, Change::Kind::none);
    EXPECT_FALSE(menu.picking());
}

TEST(PlayerMenu, FocusSurvivesTracksGoingAway)
{
    Menu menu;
    menu.Open();
    Info info = FilmInfo();
    menu.Input(IPTV_INPUT_DOWN, info);
    menu.Input(IPTV_INPUT_DOWN, info);
    ASSERT_EQ(menu.focus(), Setting::audio);
    info.audio_tracks = 1;
    const iptv_osd_state_t state = Filled(menu, info);
    EXPECT_STREQ(state.menu[state.menu_selected].label, "Subtitles");
    menu.Input(IPTV_INPUT_DOWN, info);
    EXPECT_EQ(menu.focus(), Setting::audio_delay);
}

TEST(PictureShape, NamesCommonShapes)
{
    char name[24];
    iptv_picture_shape_name(1778, name, sizeof(name));
    EXPECT_STREQ(name, "16:9");
    iptv_picture_shape_name(1781, name, sizeof(name)); // 1920x1078
    EXPECT_STREQ(name, "16:9");
    iptv_picture_shape_name(1333, name, sizeof(name));
    EXPECT_STREQ(name, "4:3");
    iptv_picture_shape_name(2400, name, sizeof(name));
    EXPECT_STREQ(name, "2.39:1");
    iptv_picture_shape_name(1600, name, sizeof(name));
    EXPECT_STREQ(name, "1.60:1");
    iptv_picture_shape_name(0, name, sizeof(name));
    EXPECT_STREQ(name, "");
}

TEST(PictureShape, FrameShapeCountsPixelShape)
{
    EXPECT_EQ(iptv_picture_frame_milli(1920, 1080, 0, 0), 1778u);
    EXPECT_EQ(iptv_picture_frame_milli(720, 576, 64, 45), 1778u);
    EXPECT_EQ(iptv_picture_frame_milli(0, 1080, 1, 1), 0u);
    EXPECT_EQ(iptv_picture_aspect_milli(IPTV_ASPECT_4_3), 1333u);
    EXPECT_EQ(iptv_picture_aspect_milli(IPTV_ASPECT_AUTO), 0u);
}

} // namespace
