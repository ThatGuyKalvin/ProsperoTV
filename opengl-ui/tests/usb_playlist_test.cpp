// ProsperoTV - USB playlist import and offline refresh persistence.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/usb_playlist.hpp"
#include "tv/model.hpp"
#include "tv/sources_screen.hpp"
#include "host_platform.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>
namespace
{
class UsbPlaylistTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        host::reset();
        host::set_network(false, "");
        char pattern[] = "/tmp/prosperotv-usb-list-XXXXXX";
        dir = mkdtemp(pattern);
        mount = dir + "/mnt";
        std::filesystem::create_directories(mount + "/usb0");
        write("Alpha");
    }
    void TearDown() override
    {
        host::reset();
        std::filesystem::remove_all(dir);
    }
    void write(const char *title)
    {
        std::ofstream(mount + "/usb0/list.m3u")
            << "#EXTM3U\n#EXTINF:-1," << title
            << "\nhttps://stream.invalid/live.ts\n#EXTINF:90,Movie\nhttps://stream.invalid/"
               "movie.mp4\n";
    }
    bool settle(ptv::Model &model)
    {
        for (int i = 0; i < 500; ++i)
        {
            model.poll();
            if (!model.refreshing())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }
    std::string dir, mount;
};
TEST_F(UsbPlaylistTest, RejectsTraversalLinksNonRegularFilesAndOversizedInput)
{
    EXPECT_TRUE(ptv::usb_playlist_path("/mnt/usb7/Playlist.M3U8"));
    for (const char *path :
         {"/mnt/usb8/list.m3u", "/mnt/usb0/../list.m3u", "/mnt/usb0/sub/list.m3u", "/data/list.m3u",
          "/mnt/usb0/.hidden.m3u", "/mnt/usb0/list.ts"})
        EXPECT_FALSE(ptv::usb_playlist_path(path));
    std::filesystem::create_symlink("list.m3u", mount + "/usb0/link.m3u");
    std::filesystem::create_directory(mount + "/usb0/folder.m3u");
    std::ofstream(mount + "/usb0/huge.m3u").put('x');
    std::filesystem::resize_file(mount + "/usb0/huge.m3u", iptv::http::kMaxListBytes + 1);
    EXPECT_EQ(ptv::usb_playlists(mount), std::vector<std::string>{"/mnt/usb0/list.m3u"});
    std::string content;
    const iptv::http::ListSink sink{[](void *p, const char *data, std::size_t size)
                                    {
                                        static_cast<std::string *>(p)->append(data, size);
                                        return true;
                                    },
                                    &content};
    EXPECT_EQ(ptv::read_usb_playlist("/mnt/usb0/link.m3u", sink, nullptr, mount).status,
              iptv::http::Status::read_failed);
    EXPECT_EQ(ptv::read_usb_playlist("/mnt/usb0/huge.m3u", sink, nullptr, mount).status,
              iptv::http::Status::read_failed);
    EXPECT_TRUE(content.empty());
    const iptv::http::RequestControl cancel{[](void *) { return true; }, nullptr};
    EXPECT_EQ(ptv::read_usb_playlist("/mnt/usb0/list.m3u", sink, &cancel, mount).status,
              iptv::http::Status::cancelled);
    EXPECT_EQ(ptv::read_usb_playlist("/mnt/usb0/list.m3u", sink, nullptr, mount).status,
              iptv::http::Status::ok);
    EXPECT_NE(content.find("Alpha"), std::string::npos);
}
TEST_F(UsbPlaylistTest, ControllerImportsMixedPlaylistAndRefreshRetainsCacheWithoutDrive)
{
    std::int64_t selected = 0;
    {
        ptv::Model model(dir);
        model.set_usb_root(mount);
        ASSERT_TRUE(model.open());
        ASSERT_TRUE(settle(model));
        const auto previous = model.saved_sources().size();
        hui::ui::Fonts fonts;
        ptv::Settings settings;
        ptv::Shared shared(model, fonts, settings);
        ptv::SourcesScreen screen(shared);
        hui::ui::Feedback feedback;
        hui::InputFrame down, cross;
        down.nav = hui::Direction::down;
        cross.pressed = hui::action_bit(hui::Action::confirm);
        for (int i = 0; i < 30 && screen.focused_id() != -100; ++i)
            screen.handle(down, feedback);
        ASSERT_EQ(screen.focused_id(), -100);
        screen.handle(cross, feedback);
        ASSERT_TRUE(settle(model));
        EXPECT_EQ(model.saved_sources().size(), previous + 1);
        selected = model.selected_source_id();
        EXPECT_EQ(model.saved_source(selected)->url, "/mnt/usb0/list.m3u");
        EXPECT_EQ(model.saved_source(selected)->schedule, ptv::RefreshSchedule::manual);
        EXPECT_EQ(model.channel_count(), 2u);
        write("Changed");
        model.refresh();
        ASSERT_TRUE(settle(model));
        EXPECT_EQ(model.channel(0).name, "Changed");
        std::filesystem::remove_all(mount + "/usb0");
        model.refresh();
        ASSERT_TRUE(settle(model));
        EXPECT_EQ(model.channel_count(), 2u);
        EXPECT_EQ(model.channel(0).name, "Changed");
        model.close();
    }
    ptv::Model reopened(dir);
    reopened.set_usb_root(mount);
    ASSERT_TRUE(reopened.open());
    ASSERT_TRUE(settle(reopened));
    EXPECT_EQ(reopened.selected_source_id(), selected);
    EXPECT_EQ(reopened.channel_count(), 2u);
    EXPECT_EQ(reopened.channel(0).name, "Changed");
    reopened.close();
}
TEST_F(UsbPlaylistTest, CustomHeadersUseInitialStreamOriginForLocalPlaylist)
{
    ptv::Model model(dir);
    model.set_usb_root(mount);
    ASSERT_TRUE(model.open());
    ASSERT_TRUE(settle(model));
    ASSERT_TRUE(model.import_usb_playlist("/mnt/usb0/list.m3u"));
    ASSERT_TRUE(settle(model));
    model.edit_source_http(model.selected_source_id());
    model.poll();
    host::set_keyboard_text("");
    model.poll();
    host::set_keyboard_text("");
    model.poll();
    host::set_keyboard_text("X-Key: local-secret");
    model.poll();
    host::set_keyboard_text("");
    model.poll();
    ASSERT_TRUE(settle(model));
    ASSERT_TRUE(model.play(0));
    ptv::PlayRequest request;
    ASSERT_TRUE(model.take_play_request(&request));
    EXPECT_EQ(request.extra_headers, "X-Key: local-secret");
    EXPECT_TRUE(request.credential_origin.empty());
    ASSERT_FALSE(request.urls.empty());
    iptv::http::RequestHeaders headers;
    headers.extra_headers = request.extra_headers.c_str();
    auto own = iptv::http::HeadersForUrl(request.urls[0].c_str(), request.urls[0].c_str(), headers);
    EXPECT_NE(own.extra_headers, nullptr);
    auto foreign = iptv::http::HeadersForUrl(request.urls[0].c_str(),
                                             "https://foreign.invalid/segment.ts", headers);
    EXPECT_EQ(foreign.extra_headers, nullptr);
    model.close();
}
} // namespace
