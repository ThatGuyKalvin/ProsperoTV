// ProsperoTV - What the provider's words are read as.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/catalog_facts.hpp"
#include "tv/channel_text.hpp"
#include "iptv_store.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace
{

using ptv::Category;
using ptv::Language;
using ptv::Place;

ptv::GroupFacts live(std::string_view title)
{
    return ptv::parse_group(title, iptv::MediaKind::live);
}
ptv::GroupFacts film(std::string_view title)
{
    return ptv::parse_group(title, iptv::MediaKind::movie);
}
ptv::GroupFacts show(std::string_view title)
{
    return ptv::parse_group(title, iptv::MediaKind::episode);
}

TEST(CatalogFacts, ACodeBeforeTheBarIsThePlaceAndTheLeafTheKind)
{
    auto facts = live("AR| BEIN SPORTS HD");
    EXPECT_EQ(facts.place, Place::arab_world);
    EXPECT_EQ(facts.audio, Language::arabic);
    EXPECT_EQ(facts.category, Category::sports);
    EXPECT_TRUE(facts.tags & ptv::kTagHd);
    EXPECT_TRUE(facts.coded);
    EXPECT_EQ(facts.levels, 2);

    facts = live("UK | Sky Sports");
    EXPECT_EQ(facts.place, Place::united_kingdom);
    EXPECT_EQ(facts.audio, Language::english);
    EXPECT_EQ(facts.category, Category::sports);

    facts = live("Uk | +1 Channels");
    EXPECT_EQ(facts.place, Place::united_kingdom);
    EXPECT_TRUE(facts.tags & ptv::kTagTimeshift);

    facts = live("USA | LOCALS");
    EXPECT_EQ(facts.place, Place::united_states);
    EXPECT_EQ(facts.category, Category::local);

    facts = live("IE | Irish Tv");
    EXPECT_EQ(facts.place, Place::ireland);

    facts = live("GR| ΓΕΝΙΚΆ/GENERAL");
    EXPECT_EQ(facts.place, Place::greece);
    EXPECT_EQ(facts.audio, Language::greek);
    EXPECT_EQ(facts.category, Category::general);

    facts = live("CY| ΑΘΛΗΤΙΣΜΟΣ/SPORTS");
    EXPECT_EQ(facts.place, Place::cyprus);
    EXPECT_EQ(facts.category, Category::sports);

    facts = live("TR| HABER KANALLARI");
    EXPECT_EQ(facts.place, Place::turkey);
    EXPECT_EQ(facts.category, Category::news);

    facts = live("PL| OGÓLNOTEMATYCZNE");
    EXPECT_EQ(facts.category, Category::general);
    EXPECT_EQ(facts.audio, Language::polish);

    facts = live("CZ| DĚTI");
    EXPECT_EQ(facts.category, Category::kids);

    facts = live("RO| CANALE PENTRU COPII");
    EXPECT_EQ(facts.category, Category::kids);

    facts = live("EXYU| SPORTSKI KANALI");
    EXPECT_EQ(facts.place, Place::balkans);
    EXPECT_EQ(facts.category, Category::sports);

    facts = live("ES| DAZN EXCLUSIVE ᴴᴰ/ᴿᴬᵂ");
    EXPECT_EQ(facts.place, Place::spain);
    EXPECT_EQ(facts.category, Category::sports);
    EXPECT_TRUE(facts.tags & ptv::kTagRaw);
    EXPECT_TRUE(facts.tags & ptv::kTagHd);
    EXPECT_TRUE(facts.tags & ptv::kTagExclusive);

    facts = live("US| PRIME ᴿᴬᵂ ⁶⁰ᶠᵖˢ");
    EXPECT_EQ(facts.category, Category::streaming);
    EXPECT_TRUE(facts.tags & ptv::kTag60fps);

    facts = live("NL| RADIO LIVE");
    EXPECT_EQ(facts.category, Category::radio);

    facts = live("AR| ISLAMIC");
    EXPECT_EQ(facts.category, Category::religion);

    facts = live("UK| DAZN PPV");
    EXPECT_EQ(facts.category, Category::sports);
    EXPECT_TRUE(facts.tags & ptv::kTagPpv);

    facts = live("PPV | Live Events");
    EXPECT_EQ(facts.category, Category::events);
    EXPECT_EQ(facts.place, Place::none);

    facts = live("AR| SYRIA AM +6H");
    EXPECT_EQ(facts.place, Place::syria);
    EXPECT_TRUE(facts.tags & ptv::kTagTimeshift);
}

TEST(CatalogFacts, ACountryInTheLeafBeatsTheCode)
{
    EXPECT_EQ(live("IR| IRELAND").place, Place::ireland);
    EXPECT_EQ(live("IR| IRELAND").audio, Language::english);
    EXPECT_EQ(live("IR| LOI PPV").place, Place::ireland);
    EXPECT_EQ(live("IR| IRAN").place, Place::iran);
    EXPECT_EQ(live("IR| IRAN").audio, Language::persian);
    EXPECT_EQ(live("CH| SWITZERLAND").place, Place::switzerland);
    EXPECT_EQ(live("CH| CHINA / HONGKONG").place, Place::china);
    EXPECT_EQ(live("LA| LATVIA").place, Place::latvia);
    EXPECT_EQ(show("|LA| LATINO SERIES").place, Place::latin_america);
    EXPECT_EQ(live("LA / GENERAL").place, Place::latin_america);
    EXPECT_EQ(live("LA / GENERAL").category, Category::general);
    EXPECT_EQ(live("IS| ISRAEL HD").place, Place::israel);
    EXPECT_EQ(live("BH| BOSNIA").place, Place::bosnia);
    EXPECT_EQ(live("SK| SOUTH KOREA").place, Place::south_korea);
    EXPECT_EQ(live("AR| EGYPT").place, Place::egypt);
    EXPECT_EQ(live("AR| EGYPT").audio, Language::arabic);
    EXPECT_EQ(live("LAT| MEXICO VIP").place, Place::mexico);
    EXPECT_TRUE(live("LAT| MEXICO VIP").tags & ptv::kTagVip);
    EXPECT_EQ(live("LAT| R. DOMINICANA").place, Place::dominican_republic);
    EXPECT_EQ(live("LAT| EL SALVADOR").place, Place::el_salvador);
    EXPECT_EQ(live("ASIA | PAKISTAN").place, Place::pakistan);
    EXPECT_EQ(live("ASIA | PAKISTAN").audio, Language::urdu);
    EXPECT_EQ(live("AFR| ETHOPIA").place, Place::ethiopia);
    EXPECT_EQ(live("AR| SAUDIA ARABIA").place, Place::saudi_arabia);
    EXPECT_EQ(live("AR| PALASTINA").place, Place::palestine);
    EXPECT_EQ(live("KA| KAZACHSTAN").place, Place::kazakhstan);
    EXPECT_EQ(live("PT| CABO VERDE").place, Place::cabo_verde);
    EXPECT_EQ(live("EU| LUXEMBOURG").place, Place::luxembourg);
    EXPECT_EQ(live("TR| AVRUPA & YEREL").place, Place::turkey);
    EXPECT_EQ(live("TR| AVRUPA & YEREL").category, Category::local);
    // A country in round brackets.
    EXPECT_EQ(live("SuperSport (ZA)").place, Place::south_africa);
    EXPECT_EQ(live("SuperSport (ZA)").category, Category::sports);
    EXPECT_EQ(live("CLUBBER TV (IE) - Events only").place, Place::ireland);
    EXPECT_TRUE(live("CLUBBER TV (IE) - Events only").tags & ptv::kTagEventsOnly);
    EXPECT_EQ(live("⚽EPL | STAN Sports (AU)").place, Place::australia);
    EXPECT_EQ(live("PL| KANAŁY (NA)").place, Place::poland);
}

TEST(CatalogFacts, BareNamesAndNordicFirstWords)
{
    EXPECT_EQ(live("France").place, Place::france);
    EXPECT_EQ(live("France").audio, Language::french);
    EXPECT_EQ(live("France").category, Category::general);
    EXPECT_EQ(live("FR| FRANCE HD").category, Category::general);
    EXPECT_EQ(live("LAT| MEXICO VIP").category, Category::general);
    EXPECT_EQ(live("US| NBC NETWORK").category, Category::general);
    EXPECT_EQ(live("GAA (Events only)").category, Category::events);
    EXPECT_EQ(live("UK | Betting").category, Category::sports);
    EXPECT_EQ(live("Adult Swim").category, Category::entertainment);
    EXPECT_FALSE(live("France").coded);
    EXPECT_EQ(live("Germany").audio, Language::german);
    EXPECT_EQ(live("Asian (IND/PK/AFG/BANG)").place, Place::asia);
    EXPECT_EQ(live("Asian  24/7").category, Category::around_the_clock);
    EXPECT_EQ(live("SWEDEN VIAPLAY PPV VIP").place, Place::sweden);
    EXPECT_EQ(live("SWEDEN VIAPLAY PPV VIP").audio, Language::swedish);
    EXPECT_EQ(live("SWEDEN VIAPLAY PPV VIP").category, Category::events);
    EXPECT_EQ(live("DENMARK HD & HEVC").place, Place::denmark);
    EXPECT_TRUE(live("DENMARK HD & HEVC").tags & ptv::kTagHevc);
    EXPECT_EQ(live("NORWAY ᴳᴼᴸᴰ ᴿᴬᵂ").place, Place::norway);
    EXPECT_TRUE(live("NORWAY ᴳᴼᴸᴰ ᴿᴬᵂ").tags & ptv::kTagGold);
    EXPECT_EQ(live("24/7 COMEDY VIP").category, Category::around_the_clock);
    EXPECT_TRUE(live("24/7 COMEDY VIP").genres & ptv::kGenreComedy);
    EXPECT_EQ(live("24/7 KIDS").category, Category::kids);
    EXPECT_EQ(live("WORLD LIVE SPORTS").category, Category::sports);
    EXPECT_EQ(live("WORLD LIVE SPORTS").place, Place::world);
    EXPECT_EQ(live("Live TV").category, Category::general);
    EXPECT_EQ(live("4K UHD 3840P").category, Category::none);
    EXPECT_TRUE(live("4K UHD 3840P").tags & ptv::kTagUhd);
    EXPECT_EQ(live("DAB RADIO").category, Category::radio);
    EXPECT_EQ(live("NEW XXX").category, Category::adult);
    EXPECT_EQ(live("ONE PLAY XXX ( NEW)").category, Category::adult);
    EXPECT_EQ(live("Shopping Channels").category, Category::shopping);
    EXPECT_EQ(live("⚽EFL | Championship").category, Category::sports);
    EXPECT_EQ(live("⚽️EPL - FUBO SPORTS").category, Category::sports);
    EXPECT_EQ(live("⚽Scottish | SPFL").place, Place::scotland);
    EXPECT_EQ(live("⚽Scottish | SPFL").category, Category::sports);
    EXPECT_EQ(live("MLB").category, Category::sports);
    EXPECT_EQ(live("NBA & NHL REPLAYS").category, Category::sports);
    EXPECT_TRUE(live("NBA & NHL REPLAYS").tags & ptv::kTagReplay);
    EXPECT_EQ(live("GAA (Events only)").place, Place::ireland);
    EXPECT_EQ(live("MOLA TV (BETA)").category, Category::sports);
    EXPECT_TRUE(live("MOLA TV (BETA)").tags & ptv::kTagBeta);
    EXPECT_TRUE(live("⚽Scottish Champ (Not Guaranteed)").tags & ptv::kTagNotGuaranteed);
    EXPECT_EQ(live("").category, Category::none);
    EXPECT_EQ(live("   ").place, Place::none);
}

TEST(CatalogFacts, FilmsAndSeriesReadTheirLanguageAndGenres)
{
    auto facts = film("|EN| 4K MOVIES");
    EXPECT_EQ(facts.place, Place::none);
    EXPECT_EQ(facts.audio, Language::english);
    EXPECT_EQ(facts.category, Category::movies);
    EXPECT_TRUE(facts.tags & ptv::kTagUhd);
    EXPECT_TRUE(facts.coded);

    facts = film("|AR| أفلام أجنبية");
    EXPECT_EQ(facts.place, Place::arab_world);
    EXPECT_EQ(facts.category, Category::movies);
    EXPECT_EQ(facts.audio, Language::english); // foreign films, as the Arabic says

    facts = film("|AR| افلام اجنبي رعب");
    EXPECT_EQ(facts.category, Category::movies);
    EXPECT_TRUE(facts.genres & ptv::kGenreHorror);

    facts = show("|AR| مسلسلات تركيه مدبلج");
    EXPECT_EQ(facts.category, Category::series);
    EXPECT_EQ(facts.audio, Language::arabic);
    EXPECT_TRUE(facts.tags & ptv::kTagDubbed);

    facts = show("|AR| مسلسلات تركيه مترجم");
    EXPECT_EQ(facts.audio, Language::turkish);
    EXPECT_EQ(facts.subtitles, Language::arabic);

    facts = film("|GR| GREECE MOVIES SUB");
    EXPECT_EQ(facts.place, Place::greece);
    EXPECT_EQ(facts.audio, Language::original);
    EXPECT_EQ(facts.subtitles, Language::greek);

    facts = film("|IN| HINDI DUBBED SOUTH");
    EXPECT_EQ(facts.place, Place::india);
    EXPECT_EQ(facts.audio, Language::hindi);

    facts = film("|IN| HINDI ENGLISH DUBBED");
    EXPECT_EQ(facts.audio, Language::hindi);

    facts = film("|IN| HINDI SUBS");
    EXPECT_EQ(facts.audio, Language::original);
    EXPECT_EQ(facts.subtitles, Language::hindi);

    facts = film("|IN| TELEGU MOVIES");
    EXPECT_EQ(facts.audio, Language::telugu);

    facts = show("|IR| TURKISH زیرنویس");
    EXPECT_EQ(facts.audio, Language::turkish);
    EXPECT_EQ(facts.subtitles, Language::persian);

    facts = show("|IR| TURKISH دوبله");
    EXPECT_EQ(facts.audio, Language::persian);

    facts = film("|EN| FRENCH MOVIES / ENG SUB");
    EXPECT_EQ(facts.audio, Language::french);
    EXPECT_EQ(facts.subtitles, Language::english);

    facts = film("|TR| DUBLAJ SINEMA");
    EXPECT_EQ(facts.audio, Language::turkish);
    EXPECT_EQ(facts.category, Category::movies);

    facts = film("|HU| SZINKRONIZALT FILMEK");
    EXPECT_EQ(facts.audio, Language::hungarian);

    facts = show("|SCA| MULTISUB SERIES");
    EXPECT_EQ(facts.place, Place::nordics);
    EXPECT_EQ(facts.subtitles, Language::multi);
    EXPECT_EQ(facts.audio, Language::original);

    facts = show("|MULTI| NETFLIX SERIES");
    EXPECT_EQ(facts.audio, Language::multi);
    EXPECT_EQ(facts.category, Category::series);

    facts = film("|DE| FILME 1990-2023");
    EXPECT_EQ(facts.audio, Language::german);
    EXPECT_EQ(facts.category, Category::movies);
    EXPECT_TRUE(facts.tags & ptv::kTagYear);

    facts = film("|PL| AKCJA/SENSACJA/KRYMINAŁ");
    EXPECT_TRUE(facts.genres & ptv::kGenreAction);
    EXPECT_TRUE(facts.genres & ptv::kGenreThriller);
    EXPECT_TRUE(facts.genres & ptv::kGenreCrime);

    facts = film("|FR| HORREUR/EPOUVANTE");
    EXPECT_TRUE(facts.genres & ptv::kGenreHorror);
    EXPECT_EQ(facts.audio, Language::french);

    facts = film("|ES| CIENCIA FICCION/FANTASIA");
    EXPECT_TRUE(facts.genres & ptv::kGenreSciFi);
    EXPECT_TRUE(facts.genres & ptv::kGenreFantasy);

    facts = film("|DK| DANSKE BØRNEFILM");
    EXPECT_EQ(facts.category, Category::kids);
    EXPECT_EQ(facts.audio, Language::danish);

    facts = film("|TR| DIZI/SERIE");
    EXPECT_EQ(facts.category, Category::series); // episodes filed as films

    facts = film("2025 Movies");
    EXPECT_EQ(facts.category, Category::movies);
    EXPECT_EQ(facts.audio, Language::none);

    facts = film("Comedy");
    EXPECT_EQ(facts.category, Category::movies);
    EXPECT_TRUE(facts.genres & ptv::kGenreComedy);

    facts = show("Sci-Fi & Fantasy");
    EXPECT_EQ(facts.category, Category::series);
    EXPECT_TRUE(facts.genres & ptv::kGenreSciFi);

    facts = film("NETFLIX MOVIES");
    EXPECT_EQ(facts.category, Category::movies);

    facts = show("|QC| DOCUMENTAIRE");
    EXPECT_EQ(facts.place, Place::quebec);
    EXPECT_EQ(facts.audio, Language::french);
    EXPECT_EQ(facts.category, Category::documentary);
}

TEST(CatalogFacts, NamesCarryTheirOwnCodeNoteOrState)
{
    const auto prefix = [](std::string_view name, std::string_view group = {})
    { return std::string(name.substr(ptv::parse_name(name, group).prefix_bytes)); };
    EXPECT_EQ(prefix("FR| TFX HD"), "TFX HD");
    EXPECT_EQ(prefix("fr: TVR"), "TVR");
    EXPECT_EQ(prefix("Pl: Eleven Sports 3 HD"), "Eleven Sports 3 HD");
    EXPECT_EQ(prefix("rom: DIGI Sport 1 HD RO"), "DIGI Sport 1 HD RO");
    EXPECT_EQ(prefix("IND-Hindi: B4U_Movies"), "B4U_Movies");
    EXPECT_EQ(prefix("Ind-Kids: Chutti Tv"), "Chutti Tv");
    EXPECT_EQ(prefix("IND-Tamil : Colors HD"), "Colors HD");
    EXPECT_EQ(prefix("BD-loc:  boishakhi"), "boishakhi");
    EXPECT_EQ(prefix("US HD: MSG"), "MSG");
    EXPECT_EQ(prefix("MXC| ESPN HD"), "ESPN HD");
    EXPECT_EQ(prefix("HINDI| AAJ TAK TEZ"), "AAJ TAK TEZ");
    EXPECT_EQ(prefix("PRIME| FOX 2 DETROIT ᴿᴬᵂ"), "FOX 2 DETROIT ᴿᴬᵂ");
    EXPECT_EQ(prefix("IN-PREM| HOTSTAR HD"), "HOTSTAR HD");
    EXPECT_EQ(prefix("SC-4K - M.I.A. (2026) (US)"), "M.I.A. (2026) (US)");
    EXPECT_EQ(prefix("4K-DE - Avatar (2009)"), "Avatar (2009)");
    EXPECT_EQ(prefix("EN - Pretty Thing 4K (2025)"), "Pretty Thing 4K (2025)");
    EXPECT_EQ(prefix("NF - Vivarium (2019)"), "Vivarium (2019)");
    EXPECT_EQ(prefix("[SE] STAR TREK"), "STAR TREK");
    EXPECT_EQ(prefix("[Kayo] Fox Footy 504"), "Fox Footy 504");
    EXPECT_EQ(prefix("[UK FTA] Channel 4 FHD"), "Channel 4 FHD");
    EXPECT_EQ(prefix("[DirecTVWebos] QVC2"), "QVC2");
    EXPECT_EQ(prefix("SWE| [Radio][ES] 06AM Ibiza Underground"), "06AM Ibiza Underground");
    EXPECT_EQ(prefix("UK◈ Disney+ Series 7 ᴴᴰ"), "Disney+ Series 7 ᴴᴰ");
    // The group's own first level counts as a code too.
    EXPECT_EQ(prefix("T2| GOUWESTAD RADIO", "NL| TELE2 RADIO"), "GOUWESTAD RADIO");
    // Not codes: a network, a state, a count, a sport body, a platform with more words.
    EXPECT_EQ(prefix("NBC: CA SACRAMENTO KCRA"), "NBC: CA SACRAMENTO KCRA");
    EXPECT_EQ(prefix("WV | CHARLESTON | WCHS"), "WV | CHARLESTON | WCHS");
    EXPECT_EQ(prefix("PPV 01: Chaos Joshua v Ngannou"), "PPV 01: Chaos Joshua v Ngannou");
    EXPECT_EQ(prefix("LOCAL SS | beIN Sports 4 /720P"), "LOCAL SS | beIN Sports 4 /720P");
    EXPECT_EQ(prefix("DAZN ES | Event 130"), "DAZN ES | Event 130");
    EXPECT_EQ(prefix("HBO Max UK | Event 1"), "HBO Max UK | Event 1");
    EXPECT_EQ(prefix("T&T - RADIO 96.7 FM"), "T&T - RADIO 96.7 FM");
    EXPECT_EQ(prefix("Kısmet 4. Bölüm"), "Kısmet 4. Bölüm");
    EXPECT_EQ(prefix("UK:"), "UK:"); // nothing but a prefix keeps it
    EXPECT_EQ(prefix(""), "");

    auto facts = ptv::parse_name("FR| Addict Star", "Live TV");
    EXPECT_EQ(facts.place, Place::france);
    EXPECT_EQ(facts.audio, Language::french);
    EXPECT_EQ(facts.row, ptv::RowKind::channel);
    facts = ptv::parse_name("SWE| [Radio][ES] 06AM Ibiza Underground", "Live TV");
    EXPECT_EQ(facts.place, Place::sweden);
    EXPECT_EQ(facts.audio, Language::swedish);
    facts = ptv::parse_name("DNK| SKY SHOWTIME 1 HD MULTI", "DENMARK HD & HEVC");
    EXPECT_EQ(facts.place, Place::denmark);
    EXPECT_TRUE(facts.tags & ptv::kTagHd);
    facts = ptv::parse_name("AR| SYRIAN TV HD +6H", "AR| SYRIA");
    EXPECT_TRUE(facts.tags & ptv::kTagTimeshift);

    EXPECT_EQ(ptv::parse_name("##### RU NEWS #####", "RU| RUSSIA").row, ptv::RowKind::heading);
    EXPECT_EQ(ptv::parse_name("####### ESPN SPORT #######", "").row, ptv::RowKind::heading);
    EXPECT_EQ(
        ptv::parse_name(
            "NEXT | A vs B | Sat 17 Oct 02:30 EDT (US) | 8K EXCLUSIVE | US: ESPN+ PPV 157", "")
            .row,
        ptv::RowKind::event_next);
    EXPECT_EQ(ptv::parse_name("Next | A - B | 2026-10-17 | 18:00 (GMT) | DE: DAZN PPV 32", "").row,
              ptv::RowKind::event_next);
    EXPECT_EQ(ptv::parse_name("ENDED | A VS B | Fri 16 Oct | SE: SVENSK HOCKEY PPV 73", "").row,
              ptv::RowKind::event_ended);
    EXPECT_EQ(ptv::parse_name("NO EVENT STREAMING NOW - | 8K EXCLUSIVE", "").row,
              ptv::RowKind::no_event);
    EXPECT_EQ(ptv::parse_name("Next Level Chef", "").row, ptv::RowKind::channel);
    EXPECT_EQ(ptv::parse_name("Ended Up Here (2021)", "").row, ptv::RowKind::channel);
}

TEST(CatalogFacts, NamesLabelsAndAdultWords)
{
    EXPECT_STREQ(ptv::place_name(Place::france), "France");
    EXPECT_STREQ(ptv::place_name(Place::latin_america), "Latin America");
    EXPECT_STREQ(ptv::place_name(Place::none), "");
    EXPECT_STREQ(ptv::language_name(Language::serbo_croatian), "Serbo-Croatian");
    EXPECT_STREQ(ptv::category_name(Category::around_the_clock), "24/7");
    EXPECT_STREQ(ptv::genre_name(14), "Science fiction");
    EXPECT_EQ(ptv::place_language(Place::brazil), Language::portuguese);
    EXPECT_EQ(ptv::place_language(Place::belgium), Language::none);

    EXPECT_EQ(ptv::category_level_label("|EN"), "English");
    EXPECT_EQ(ptv::category_level_label("EN"), "English");
    EXPECT_EQ(ptv::category_level_label("AR"), "Arab world");
    EXPECT_EQ(ptv::category_level_label("|LA|"), "Latin America");
    EXPECT_EQ(ptv::category_level_label("MULTI"), "Multiple languages");
    EXPECT_EQ(ptv::category_level_label("4K MOVIES"), "4K MOVIES");
    EXPECT_EQ(ptv::category_level_label("US| NETFLIX ᴿᴬᵂ"), "US| NETFLIX RAW");

    EXPECT_TRUE(ptv::category_words_say("NEW XXX", true));
    EXPECT_TRUE(ptv::category_words_say("ONE PLAY XXX ( NEW)", true));
    EXPECT_TRUE(ptv::category_words_say("US / Adult HD", true));
    EXPECT_TRUE(ptv::category_words_say("Adult Movies", true));
    EXPECT_TRUE(ptv::category_words_say("|AR| +18", true));
    EXPECT_FALSE(ptv::category_words_say("US / Kids / Cartoons", true));
    EXPECT_FALSE(ptv::category_words_say("Adulting 101", true));
    EXPECT_TRUE(ptv::category_words_say("US / Kids / Cartoons", false));
    EXPECT_TRUE(ptv::category_words_say("24/7 KIDS/FAMILY VIP", false));
    EXPECT_TRUE(ptv::category_words_say("Children's", false));
    EXPECT_TRUE(ptv::category_words_say("BE| KINDEREN", false));
    EXPECT_FALSE(ptv::category_words_say("General", false));
}

// Set PTV_FACTS_CATALOG to one or more saved catalogs (":" between them) to
// see how much of a real provider the words cover, and which words are left.
TEST(CatalogFacts, CoverageOfSavedCatalogs)
{
    const char *paths = std::getenv("PTV_FACTS_CATALOG");
    if (!paths || !*paths)
        GTEST_SKIP() << "PTV_FACTS_CATALOG is not set";
    std::string list(paths);
    for (std::size_t at = 0; at < list.size();)
    {
        const std::size_t end = std::min(list.find(':', at), list.size());
        const std::string path = list.substr(at, end - at);
        at = end + 1;
        if (path.empty())
            continue;
        iptv::Catalog catalog;
        iptv::StoreLimits limits;
        limits.max_channels = 400000;
        limits.max_file_bytes = 512u << 20;
        const auto status = iptv::LoadCatalog(path, &catalog, limits);
        ASSERT_EQ(status, iptv::StoreStatus::ok) << path;
        std::map<std::string, unsigned> unknown, places, languages, categories;
        std::map<std::string, ptv::GroupFacts> groups;
        unsigned with_place = 0, with_language = 0, with_category = 0, headings = 0, prefixed = 0,
                 events = 0;
        for (std::size_t i = 0; i < catalog.size(); ++i)
        {
            const iptv::ChannelView channel = catalog[i];
            const std::string title(channel.group_title);
            auto found = groups.find(title);
            if (found == groups.end())
            {
                found = groups.emplace(title, ptv::parse_group(title, channel.media_kind)).first;
                for (const std::string &word : ptv::unknown_words(title, channel.media_kind))
                    unknown[word] += 0;
            }
            const ptv::GroupFacts &facts = found->second;
            for (const std::string &word : ptv::unknown_words(title, channel.media_kind))
                ++unknown[word];
            const ptv::NameFacts name = ptv::parse_name(channel.name, title);
            Place place = facts.place;
            Language audio = facts.audio;
            if ((place == Place::none || place < Place::afghanistan) &&
                name.place >= Place::afghanistan)
                place = name.place;
            if (place == Place::none)
                place = name.place;
            if (audio == Language::none)
                audio = name.audio;
            if (place != Place::none)
                ++with_place;
            if (audio != Language::none)
                ++with_language;
            if (facts.category != Category::none)
                ++with_category;
            if (name.row == ptv::RowKind::heading)
                ++headings;
            if (name.row != ptv::RowKind::channel && name.row != ptv::RowKind::heading)
                ++events;
            if (name.prefix_bytes)
                ++prefixed;
            ++places[ptv::place_name(place)];
            ++languages[ptv::language_name(audio)];
            ++categories[ptv::category_name(facts.category)];
        }
        const auto percent = [&](unsigned count)
        { return catalog.empty() ? 0.0 : 100.0 * count / static_cast<double>(catalog.size()); };
        std::printf("\n== %s: %zu channels, %zu groups\n", path.c_str(), catalog.size(),
                    groups.size());
        std::printf("place %.1f%%  language %.1f%%  category %.1f%%  prefixed names %.1f%%  "
                    "headings %u  events %u\n",
                    percent(with_place), percent(with_language), percent(with_category),
                    percent(prefixed), headings, events);
        const auto top =
            [&](const char *what, const std::map<std::string, unsigned> &counts, std::size_t limit)
        {
            std::vector<std::pair<unsigned, std::string>> rows;
            for (const auto &[key, count] : counts)
                rows.emplace_back(count, key.empty() ? "(none)" : key);
            std::sort(rows.rbegin(), rows.rend());
            std::printf("%s:", what);
            for (std::size_t i = 0; i < rows.size() && i < limit; ++i)
                std::printf(" %s=%u", rows[i].second.c_str(), rows[i].first);
            std::printf("\n");
        };
        top("places", places, 40);
        top("languages", languages, 30);
        top("categories", categories, 20);
        top("unknown words (channels)", unknown, 80);
        // Groups whose kind was not read, the largest first.
        std::map<std::string, unsigned> sizes;
        for (std::size_t i = 0; i < catalog.size(); ++i)
            ++sizes[std::string(catalog[i].group_title)];
        std::vector<std::pair<unsigned, std::string>> uncategorized, unplaced;
        for (const auto &[title, facts] : groups)
        {
            if (facts.category == Category::none)
                uncategorized.emplace_back(sizes[title], title);
            if (facts.place == Place::none)
                unplaced.emplace_back(sizes[title], title);
        }
        std::sort(uncategorized.rbegin(), uncategorized.rend());
        std::sort(unplaced.rbegin(), unplaced.rend());
        std::printf("groups without a kind (%zu):", uncategorized.size());
        for (std::size_t i = 0; i < uncategorized.size() && i < 60; ++i)
            std::printf(" [%s]=%u", uncategorized[i].second.c_str(), uncategorized[i].first);
        std::printf("\ngroups without a place (%zu):", unplaced.size());
        for (std::size_t i = 0; i < unplaced.size() && i < 60; ++i)
            std::printf(" [%s]=%u", unplaced[i].second.c_str(), unplaced[i].first);
        std::printf("\n");
    }
}

} // namespace
