// ProsperoTV - What a provider's group titles and channel names say: the
// place, the language, the kind of channel and the marks on it.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Providers that send no tvg-country or tvg-language put all of it into the
// words: "AR| BEIN SPORTS HD", "|EN| 4K MOVIES", "fr: TF1", "SWEDEN VIAPLAY
// PPV VIP", "GR| ΓΕΝΙΚΆ/GENERAL". The provider's own names are never changed;
// this reads them once and the filters, the kickers and the place lines use
// what it found.

#pragma once

#include "iptv_catalog.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ptv
{

// A country, or a region when the words name no country.
enum class Place : std::uint16_t
{
    none,
    world,
    arab_world,
    latin_america,
    south_asia,
    asia,
    balkans,
    nordics,
    africa,
    caribbean,
    europe,
    north_america,
    // Countries, in the order of their English names.
    afghanistan,
    albania,
    algeria,
    argentina,
    armenia,
    australia,
    austria,
    azerbaijan,
    bahrain,
    bangladesh,
    belarus,
    belgium,
    bhutan,
    bolivia,
    bosnia,
    brazil,
    bulgaria,
    cabo_verde,
    canada,
    chile,
    china,
    colombia,
    costa_rica,
    croatia,
    cuba,
    cyprus,
    czechia,
    denmark,
    dominican_republic,
    ecuador,
    egypt,
    el_salvador,
    estonia,
    ethiopia,
    finland,
    france,
    georgia,
    germany,
    ghana,
    greece,
    guatemala,
    haiti,
    honduras,
    hong_kong,
    hungary,
    iceland,
    india,
    indonesia,
    iran,
    iraq,
    ireland,
    israel,
    italy,
    jamaica,
    japan,
    jordan,
    kazakhstan,
    kenya,
    kosovo,
    kurdistan,
    kuwait,
    latvia,
    lebanon,
    libya,
    lithuania,
    luxembourg,
    malaysia,
    malta,
    mauritania,
    mauritius,
    mexico,
    moldova,
    montenegro,
    morocco,
    nepal,
    netherlands,
    new_zealand,
    nicaragua,
    nigeria,
    north_macedonia,
    norway,
    oman,
    pakistan,
    palestine,
    panama,
    paraguay,
    peru,
    philippines,
    poland,
    portugal,
    puerto_rico,
    qatar,
    quebec,
    romania,
    russia,
    saudi_arabia,
    scotland,
    serbia,
    singapore,
    slovakia,
    slovenia,
    somalia,
    south_africa,
    south_korea,
    spain,
    sri_lanka,
    sudan,
    suriname,
    sweden,
    switzerland,
    syria,
    taiwan,
    tajikistan,
    thailand,
    trinidad,
    tunisia,
    turkey,
    uae,
    uganda,
    ukraine,
    united_kingdom,
    united_states,
    uruguay,
    uzbekistan,
    venezuela,
    vietnam,
    wales,
    yemen,
    count,
};

enum class Language : std::uint8_t
{
    none,
    original, // the sound is left as it was made; the subtitles are the provider's
    multi,
    afrikaans,
    albanian,
    amharic,
    arabic,
    armenian,
    assamese,
    azerbaijani,
    belarusian,
    bengali,
    bhojpuri,
    bosnian,
    bulgarian,
    cantonese,
    catalan,
    chinese,
    croatian,
    czech,
    danish,
    dutch,
    english,
    estonian,
    filipino,
    finnish,
    french,
    georgian,
    german,
    greek,
    gujarati,
    hebrew,
    hindi,
    hungarian,
    icelandic,
    indonesian,
    italian,
    japanese,
    kannada,
    kazakh,
    korean,
    kurdish,
    latvian,
    lithuanian,
    macedonian,
    malay,
    malayalam,
    maltese,
    marathi,
    nepali,
    norwegian,
    odia,
    pashto,
    persian,
    polish,
    portuguese,
    punjabi,
    romanian,
    russian,
    serbian,
    serbo_croatian,
    sinhala,
    slovak,
    slovene,
    somali,
    spanish,
    swahili,
    swedish,
    tamil,
    telugu,
    thai,
    turkish,
    ukrainian,
    urdu,
    uzbek,
    vietnamese,
    count,
};

// The kind of channel a group is, in the app's own words.
enum class Category : std::uint8_t
{
    none,
    general,
    entertainment,
    news,
    sports,
    events, // pay-per-view and live events
    kids,
    movies,
    series,
    documentary,
    music,
    radio,
    religion,
    local,
    lifestyle,
    shopping,
    adult,
    around_the_clock, // "24/7" loops of one show or film
    streaming,        // a platform's catch-up: Netflix, Prime, Disney+…
    count,
};

// Genres of films and series, as bits: a group is often "Horror/Thriller".
enum Genre : std::uint32_t
{
    kGenreAction = 1u << 0,
    kGenreAdventure = 1u << 1,
    kGenreAnimation = 1u << 2,
    kGenreAnime = 1u << 3,
    kGenreComedy = 1u << 4,
    kGenreCrime = 1u << 5,
    kGenreDrama = 1u << 6,
    kGenreFamily = 1u << 7,
    kGenreFantasy = 1u << 8,
    kGenreHistory = 1u << 9,
    kGenreHorror = 1u << 10,
    kGenreMusical = 1u << 11,
    kGenreMystery = 1u << 12,
    kGenreRomance = 1u << 13,
    kGenreSciFi = 1u << 14,
    kGenreThriller = 1u << 15,
    kGenreWar = 1u << 16,
    kGenreWestern = 1u << 17,
    kGenreReality = 1u << 18,
    kGenreTalk = 1u << 19,
    kGenreSoap = 1u << 20,
    kGenreChristmas = 1u << 21,
    kGenreClassic = 1u << 22,
    kGenreBollywood = 1u << 23,
    kGenreStandUp = 1u << 24,
    kGenreCollection = 1u << 25,
    kGenreConcert = 1u << 26,
    kGenreBiography = 1u << 27,
    kGenreSport = 1u << 28,
    kGenreCount = 29,
};

// Marks a provider puts beside a group or a name, as bits.
enum Tag : std::uint32_t
{
    kTagVip = 1u << 0,
    kTagRaw = 1u << 1, // the provider's own capture
    kTagGold = 1u << 2,
    kTagPremium = 1u << 3,
    kTagExclusive = 1u << 4,
    kTagHevc = 1u << 5,
    kTagHd = 1u << 6,
    kTagFullHd = 1u << 7,
    kTagUhd = 1u << 8,
    kTag8k = 1u << 9,
    kTagSd = 1u << 10,
    kTagLowQuality = 1u << 11,
    kTag60fps = 1u << 12,
    kTagDolby = 1u << 13,
    kTagMultiAudio = 1u << 14,
    kTagMultiSub = 1u << 15,
    kTagBackup = 1u << 16,
    kTagTimeshift = 1u << 17,
    kTagBeta = 1u << 18,
    kTagNotGuaranteed = 1u << 19,
    kTagNew = 1u << 20,
    kTagPpv = 1u << 21,
    kTagReplay = 1u << 22,
    kTagEventsOnly = 1u << 23,
    kTagCam = 1u << 24,
    kTag3d = 1u << 25,
    kTagHdr = 1u << 26,
    kTagBluray = 1u << 27,
    kTagYear = 1u << 28, // the group names a year or a range of years
    kTagDubbed = 1u << 29,
    kTagSubtitled = 1u << 30,
};

// What one group title says.
struct GroupFacts
{
    Place place = Place::none;
    Language audio = Language::none;
    Language subtitles = Language::none;
    Category category = Category::none;
    std::uint32_t genres = 0;
    std::uint32_t tags = 0;
    std::uint8_t levels = 0; // "AR| BEIN SPORTS" has two
    bool coded = false;      // the first level is a code such as "AR" or "|EN|"
};

enum class RowKind : std::uint8_t
{
    channel,
    heading,     // "##### SPORTS #####": a line between groups, not a channel
    event_next,  // "NEXT | A vs B | Sat 17 Oct …"
    event_ended, // "ENDED | …"
    no_event,    // "NO EVENT STREAMING NOW"
};

// What one channel name says beyond its group.
struct NameFacts
{
    std::uint8_t prefix_bytes = 0; // "FR| ", "fr: ", "[Kayo] ": bytes to leave out of the name
    Place place = Place::none;     // from the prefix; refines a group that gave none
    Language audio = Language::none;
    RowKind row = RowKind::channel;
    std::uint32_t tags = 0;
};

GroupFacts parse_group(std::string_view title, iptv::MediaKind kind);
NameFacts parse_name(std::string_view name, std::string_view group_title);
// The bytes of a known prefix at the front of a name: what parse_name() finds, alone.
unsigned name_prefix_bytes(std::string_view name, std::string_view group_title);

// English names, as tr() keys.
const char *place_name(Place place);
const char *language_name(Language language);
const char *category_name(Category category);
const char *genre_name(unsigned bit);
// "English" for a level of a provider category that is a code ("|EN|", "AR"),
// else the level as label_text() writes it.
std::string category_level_label(std::string_view level);
// Every level of a provider category path, labelled, with " / " between.
std::string category_path_label(std::string_view path);
// The language a place is mostly heard in.
Language place_language(Place place);
// True when the folded words of a provider category say adult (or, with
// `adult` false, say kids): "NEW XXX", "US / Adult HD", "KIDS/FAMILY".
bool category_words_say(std::string_view category, bool adult);
// For the tests: the words of a group title that stood for nothing.
std::vector<std::string> unknown_words(std::string_view title, iptv::MediaKind kind);

} // namespace ptv
