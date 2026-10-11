// ProsperoTV - Sharing trust boundaries and profile preservation.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/share_bundle.hpp"
#include "tv/share_tls.hpp"
#include "tv/platform.hpp"
#include "iptv_user_state.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace
{
ptv::SharedLibrary sample()
{
    ptv::SharedLibrary out;
    ptv::SharedSource source;
    source.source = {99,       2,        "Family TV", "https://example.invalid",
                     "viewer", "secret", {},          ptv::RefreshSchedule::weekly};
    source.source.user_agent = "Family player";
    source.source.extra_headers = "X-Test: value";
    source.guide = "https://example.invalid/guide.xml";
    source.mapping["channel-id"] = "guide-id";
    out.sources.push_back(source);
    out.favorites = {"channel-id"};
    out.folders["Family"] = {"channel-id"};
    out.skipped_local = 1;
    return out;
}
bool wait_for(ptv::ShareTls &tls, ptv::SharePhase phase)
{
    for (unsigned i = 0; i < 500; ++i)
    {
        if (tls.status().phase == phase)
            return true;
        if (tls.status().phase == ptv::SharePhase::failed)
            return false;
        ptv::platform::sleep_ms(10);
    }
    return false;
}
TEST(Sharing, RoundtripExcludesLocalIdsAndRejectsTruncationTrailingAndLocalSources)
{
    auto input = sample();
    std::string bytes;
    ASSERT_TRUE(ptv::encode_shared_library(input, bytes));
    ptv::SharedLibrary output;
    ASSERT_TRUE(ptv::decode_shared_library(bytes, output));
    ASSERT_EQ(output.sources.size(), 1u);
    EXPECT_EQ(output.sources[0].source.id, 0);
    EXPECT_EQ(output.sources[0].source.password, "secret");
    EXPECT_EQ(output.sources[0].source.extra_headers, "X-Test: value");
    EXPECT_EQ(output.sources[0].mapping, input.sources[0].mapping);
    EXPECT_EQ(output.skipped_local, 1u);
    EXPECT_EQ(output.folders, input.folders);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        EXPECT_FALSE(ptv::decode_shared_library(std::string_view(bytes).substr(0, i), output));
    EXPECT_FALSE(ptv::decode_shared_library(bytes + "x", output));
    input.sources[0].source.kind = 1;
    input.sources[0].source.url = "/mnt/usb0/private.m3u";
    EXPECT_FALSE(ptv::encode_shared_library(input, bytes));
}
TEST(Sharing, RequiresFullFingerprintAndRejectsPublicOrInvalidDestination)
{
    std::string normalized;
    EXPECT_FALSE(ptv::share_fingerprint("123456", normalized));
    EXPECT_TRUE(ptv::share_fingerprint(std::string(64, 'a'), normalized));
    EXPECT_EQ(normalized, std::string(64, 'A'));
    ptv::ShareTls sender;
    EXPECT_FALSE(sender.transmit("8.8.8.8", 18127, std::string(64, 'A'), "data"));
    EXPECT_FALSE(sender.transmit("127.0.0.1", 0, std::string(64, 'A'), "data"));
}
TEST(Sharing, PinnedTlsRequiresReceiverApprovalAndTransfersExactBytes)
{
    ptv::ShareTls receiver, sender;
    ASSERT_TRUE(receiver.receive(0));
    ASSERT_TRUE(wait_for(receiver, ptv::SharePhase::listening));
    const auto ready = receiver.status();
    const std::string payload("sensitive\0account", 17);
    ASSERT_TRUE(sender.transmit("127.0.0.1", ready.port, ready.fingerprint, payload));
    ASSERT_TRUE(wait_for(receiver, ptv::SharePhase::approval));
    EXPECT_TRUE(receiver.take_payload().empty());
    EXPECT_NE(sender.status().phase, ptv::SharePhase::ready);
    receiver.approve();
    ASSERT_TRUE(wait_for(receiver, ptv::SharePhase::ready));
    ASSERT_TRUE(wait_for(sender, ptv::SharePhase::ready));
    EXPECT_EQ(receiver.take_payload(), payload);
    EXPECT_TRUE(receiver.take_payload().empty());
}
TEST(Sharing, WrongPinSendsNoPayloadAndCancelEndsPendingReceiver)
{
    ptv::ShareTls receiver, sender;
    ASSERT_TRUE(receiver.receive(0));
    ASSERT_TRUE(wait_for(receiver, ptv::SharePhase::listening));
    const auto ready = receiver.status();
    ASSERT_TRUE(sender.transmit("127.0.0.1", ready.port, std::string(64, '0'), "secret"));
    ASSERT_TRUE(wait_for(sender, ptv::SharePhase::failed));
    EXPECT_TRUE(receiver.take_payload().empty());
    receiver.cancel();
    EXPECT_EQ(receiver.status().phase, ptv::SharePhase::cancelled);
}
TEST(Sharing, ImportRemapsIdsMergesFavoritesAndPreservesDestinationProfile)
{
    char pattern[] = "/tmp/ptv-share-XXXXXX";
    const std::string root = mkdtemp(pattern);
    struct Cleanup
    {
        std::string path;
        ~Cleanup()
        {
            std::filesystem::remove_all(path);
        }
    } cleanup{root};
    const auto database = root + "/prosperotv-library.sqlite3";
    {
        ptv::Library library;
        ASSERT_TRUE(library.open(database));
        ptv::SavedSource reserved{4, 3, "Portal", {}, {}, {}, {}, ptv::RefreshSchedule::daily};
        ASSERT_TRUE(library.save_source(&reserved));
        ASSERT_TRUE(library.select_source(4));
        ASSERT_TRUE(library.add_folder("Existing"));
        ASSERT_TRUE(library.put_in_folder("Existing", "old-id", true));
    }
    for (auto file :
         {"prosperotv-parental-v1.txt", "prosperotv-interface-v1.txt", "remote-pairing.txt"})
        std::ofstream(root + "/" + file) << "destination-private-data";
    ASSERT_EQ(iptv::SaveFavorites(root + "/iptv-favorites-v1.bin", {"old-id"}),
              iptv::UserStateStatus::ok);
    std::string error;
    ASSERT_TRUE(ptv::import_shared_library(root, sample(), error)) << error;
    ASSERT_TRUE(ptv::import_shared_library(root, sample(), error)) << error;
    ptv::Library library;
    ASSERT_TRUE(library.open(database));
    auto sources = library.sources();
    ASSERT_EQ(sources.size(), 2u);
    EXPECT_EQ(sources[1].id, 5);
    EXPECT_EQ(library.selected_source(), 4);
    EXPECT_EQ(library.guide_mapping(5).at("channel-id"), "guide-id");
    EXPECT_TRUE(library.folder_channels("Existing").contains("old-id"));
    EXPECT_TRUE(library.folder_channels("Family").contains("channel-id"));
    std::vector<std::string> favorites;
    ASSERT_EQ(iptv::LoadFavorites(root + "/iptv-favorites-v1.bin", &favorites),
              iptv::UserStateStatus::ok);
    EXPECT_EQ(favorites, (std::vector<std::string>{"old-id", "channel-id"}));
    for (auto file :
         {"prosperotv-parental-v1.txt", "prosperotv-interface-v1.txt", "remote-pairing.txt"})
    {
        std::ifstream in(root + "/" + file);
        std::string content;
        in >> content;
        EXPECT_EQ(content, "destination-private-data");
    }
    EXPECT_FALSE(std::filesystem::exists(root + "/prosperotv-restore-journal.sqlite3"));
}
TEST(Sharing, ImportRollsBackNewSourcesWhenFavoritesCannotBeMerged)
{
    char pattern[] = "/tmp/ptv-share-rollback-XXXXXX";
    const std::string root = mkdtemp(pattern);
    struct Cleanup { std::string path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{root};
    const auto database = root + "/prosperotv-library.sqlite3";
    {
        ptv::Library library;
        ASSERT_TRUE(library.open(database));
        ptv::SavedSource reserved{4, 3, "Portal", {}, {}, {}, {}, ptv::RefreshSchedule::daily};
        ASSERT_TRUE(library.save_source(&reserved));
    }
    std::ofstream(root + "/iptv-favorites-v1.bin") << "invalid-existing-favorites";
    std::string error;
    EXPECT_FALSE(ptv::import_shared_library(root, sample(), error));
    ptv::Library library;
    ASSERT_TRUE(library.open(database));
    ASSERT_EQ(library.sources().size(), 1u);
    EXPECT_TRUE(library.folders().empty());
    std::ifstream original(root + "/iptv-favorites-v1.bin");
    std::string contents;
    original >> contents;
    EXPECT_EQ(contents, "invalid-existing-favorites");
    EXPECT_FALSE(std::filesystem::exists(root + "/prosperotv-restore-journal.sqlite3"));
}
} // namespace
