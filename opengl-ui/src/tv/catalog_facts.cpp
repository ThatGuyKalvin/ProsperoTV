// ProsperoTV - What a provider's group titles and channel names say.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv/catalog_facts.hpp"

#include "tv/category_path.hpp"
#include "tv/channel_text.hpp"
#include "tv/i18n.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ptv
{

namespace
{

// ---- the tables ----

struct PlaceInfo
{
    const char *name;
    Language language;
};

// In the order of the Place enum.
constexpr PlaceInfo kPlaces[] = {
    {"", Language::none},
    {"World", Language::none},
    {"Arab world", Language::arabic},
    {"Latin America", Language::spanish},
    {"South Asia", Language::none},
    {"Asia", Language::none},
    {"Balkans", Language::serbo_croatian},
    {"Nordics", Language::multi},
    {"Africa", Language::none},
    {"Caribbean", Language::none},
    {"Europe", Language::none},
    {"North America", Language::english},
    {"Afghanistan", Language::pashto},
    {"Albania", Language::albanian},
    {"Algeria", Language::arabic},
    {"Argentina", Language::spanish},
    {"Armenia", Language::armenian},
    {"Australia", Language::english},
    {"Austria", Language::german},
    {"Azerbaijan", Language::azerbaijani},
    {"Bahrain", Language::arabic},
    {"Bangladesh", Language::bengali},
    {"Belarus", Language::belarusian},
    {"Belgium", Language::none},
    {"Bhutan", Language::none},
    {"Bolivia", Language::spanish},
    {"Bosnia and Herzegovina", Language::bosnian},
    {"Brazil", Language::portuguese},
    {"Bulgaria", Language::bulgarian},
    {"Cabo Verde", Language::portuguese},
    {"Canada", Language::none},
    {"Chile", Language::spanish},
    {"China", Language::chinese},
    {"Colombia", Language::spanish},
    {"Costa Rica", Language::spanish},
    {"Croatia", Language::croatian},
    {"Cuba", Language::spanish},
    {"Cyprus", Language::greek},
    {"Czechia", Language::czech},
    {"Denmark", Language::danish},
    {"Dominican Republic", Language::spanish},
    {"Ecuador", Language::spanish},
    {"Egypt", Language::arabic},
    {"El Salvador", Language::spanish},
    {"Estonia", Language::estonian},
    {"Ethiopia", Language::amharic},
    {"Finland", Language::finnish},
    {"France", Language::french},
    {"Georgia", Language::georgian},
    {"Germany", Language::german},
    {"Ghana", Language::english},
    {"Greece", Language::greek},
    {"Guatemala", Language::spanish},
    {"Haiti", Language::french},
    {"Honduras", Language::spanish},
    {"Hong Kong", Language::cantonese},
    {"Hungary", Language::hungarian},
    {"Iceland", Language::icelandic},
    {"India", Language::hindi},
    {"Indonesia", Language::indonesian},
    {"Iran", Language::persian},
    {"Iraq", Language::arabic},
    {"Ireland", Language::english},
    {"Israel", Language::hebrew},
    {"Italy", Language::italian},
    {"Jamaica", Language::english},
    {"Japan", Language::japanese},
    {"Jordan", Language::arabic},
    {"Kazakhstan", Language::kazakh},
    {"Kenya", Language::swahili},
    {"Kosovo", Language::albanian},
    {"Kurdistan", Language::kurdish},
    {"Kuwait", Language::arabic},
    {"Latvia", Language::latvian},
    {"Lebanon", Language::arabic},
    {"Libya", Language::arabic},
    {"Lithuania", Language::lithuanian},
    {"Luxembourg", Language::none},
    {"Malaysia", Language::malay},
    {"Malta", Language::maltese},
    {"Mauritania", Language::arabic},
    {"Mauritius", Language::none},
    {"Mexico", Language::spanish},
    {"Moldova", Language::romanian},
    {"Montenegro", Language::serbo_croatian},
    {"Morocco", Language::arabic},
    {"Nepal", Language::nepali},
    {"Netherlands", Language::dutch},
    {"New Zealand", Language::english},
    {"Nicaragua", Language::spanish},
    {"Nigeria", Language::english},
    {"North Macedonia", Language::macedonian},
    {"Norway", Language::norwegian},
    {"Oman", Language::arabic},
    {"Pakistan", Language::urdu},
    {"Palestine", Language::arabic},
    {"Panama", Language::spanish},
    {"Paraguay", Language::spanish},
    {"Peru", Language::spanish},
    {"Philippines", Language::filipino},
    {"Poland", Language::polish},
    {"Portugal", Language::portuguese},
    {"Puerto Rico", Language::spanish},
    {"Qatar", Language::arabic},
    {"Quebec", Language::french},
    {"Romania", Language::romanian},
    {"Russia", Language::russian},
    {"Saudi Arabia", Language::arabic},
    {"Scotland", Language::english},
    {"Serbia", Language::serbian},
    {"Singapore", Language::none},
    {"Slovakia", Language::slovak},
    {"Slovenia", Language::slovene},
    {"Somalia", Language::somali},
    {"South Africa", Language::none},
    {"South Korea", Language::korean},
    {"Spain", Language::spanish},
    {"Sri Lanka", Language::sinhala},
    {"Sudan", Language::arabic},
    {"Suriname", Language::dutch},
    {"Sweden", Language::swedish},
    {"Switzerland", Language::none},
    {"Syria", Language::arabic},
    {"Taiwan", Language::chinese},
    {"Tajikistan", Language::none},
    {"Thailand", Language::thai},
    {"Trinidad and Tobago", Language::english},
    {"Tunisia", Language::arabic},
    {"Turkey", Language::turkish},
    {"United Arab Emirates", Language::arabic},
    {"Uganda", Language::english},
    {"Ukraine", Language::ukrainian},
    {"United Kingdom", Language::english},
    {"United States", Language::english},
    {"Uruguay", Language::spanish},
    {"Uzbekistan", Language::uzbek},
    {"Venezuela", Language::spanish},
    {"Vietnam", Language::vietnamese},
    {"Wales", Language::english},
    {"Yemen", Language::arabic},
};
static_assert(std::size(kPlaces) == static_cast<std::size_t>(Place::count));

// In the order of the Language enum.
constexpr const char *kLanguages[] = {
    "",
    "Original",
    "Multiple languages",
    "Afrikaans",
    "Albanian",
    "Amharic",
    "Arabic",
    "Armenian",
    "Assamese",
    "Azerbaijani",
    "Belarusian",
    "Bengali",
    "Bhojpuri",
    "Bosnian",
    "Bulgarian",
    "Cantonese",
    "Catalan",
    "Chinese",
    "Croatian",
    "Czech",
    "Danish",
    "Dutch",
    "English",
    "Estonian",
    "Filipino",
    "Finnish",
    "French",
    "Georgian",
    "German",
    "Greek",
    "Gujarati",
    "Hebrew",
    "Hindi",
    "Hungarian",
    "Icelandic",
    "Indonesian",
    "Italian",
    "Japanese",
    "Kannada",
    "Kazakh",
    "Korean",
    "Kurdish",
    "Latvian",
    "Lithuanian",
    "Macedonian",
    "Malay",
    "Malayalam",
    "Maltese",
    "Marathi",
    "Nepali",
    "Norwegian",
    "Odia",
    "Pashto",
    "Persian",
    "Polish",
    "Portuguese",
    "Punjabi",
    "Romanian",
    "Russian",
    "Serbian",
    "Serbo-Croatian",
    "Sinhala",
    "Slovak",
    "Slovene",
    "Somali",
    "Spanish",
    "Swahili",
    "Swedish",
    "Tamil",
    "Telugu",
    "Thai",
    "Turkish",
    "Ukrainian",
    "Urdu",
    "Uzbek",
    "Vietnamese",
};
static_assert(std::size(kLanguages) == static_cast<std::size_t>(Language::count));

// In the order of the Category enum.
constexpr const char *kCategories[] = {
    "",          "General",  "Entertainment", "News",  "Sports",    "Events",   "Kids",
    "Movies",    "Series",   "Documentary",   "Music", "Radio",     "Religion", "Local",
    "Lifestyle", "Shopping", "Adult",         "24/7",  "Streaming",
};
static_assert(std::size(kCategories) == static_cast<std::size_t>(Category::count));

// By genre bit.
constexpr const char *kGenres[] = {
    "Action",          "Adventure", "Animation", "Anime",    "Comedy",      "Crime",    "Drama",
    "Family",          "Fantasy",   "History",   "Horror",   "Musical",     "Mystery",  "Romance",
    "Science fiction", "Thriller",  "War",       "Western",  "Reality",     "Talk",     "Soap",
    "Christmas",       "Classics",  "Bollywood", "Stand-up", "Collections", "Concerts", "Biography",
    "Sport",
};
static_assert(std::size(kGenres) == kGenreCount);

// Which word says more about the kind of channel: the higher rank wins.
constexpr int category_rank(Category category)
{
    switch (category)
    {
    case Category::adult:
        return 15;
    case Category::events:
        return 14;
    case Category::kids:
        return 13;
    case Category::news:
        return 12;
    case Category::sports:
        return 11;
    case Category::radio:
        return 10;
    case Category::music:
        return 9;
    case Category::documentary:
        return 8;
    case Category::religion:
        return 7;
    case Category::movies:
    case Category::series:
        return 6;
    case Category::local:
        return 5;
    case Category::streaming:
        return 4;
    case Category::lifestyle:
    case Category::shopping:
        return 3;
    case Category::general:
    case Category::entertainment:
        return 2;
    case Category::around_the_clock:
        return 1;
    default:
        return 0;
    }
}

// What a word (or two or three words) stands for.
enum class Says : std::uint8_t
{
    place,       // value: Place
    language,    // value: Language
    category,    // value: Category
    genre,       // value: Genre bit
    tag,         // value: Tag bit
    platform,    // value: Category (streaming, or sports for a sports service)
    subtitled,   // "sub", "vostfr": value: Language the subtitles are in, or none
    dubbed,      // "dubbed": value: Language the sound is in, or none
    dubbed_here, // "dublaj", "مدبلج": the sound is in the group's own language
};

struct Word
{
    const char *text; // folded; up to three words with one space between
    Says says;
    unsigned value;
};

constexpr unsigned P(Place place)
{
    return static_cast<unsigned>(place);
}
constexpr unsigned L(Language language)
{
    return static_cast<unsigned>(language);
}
constexpr unsigned C(Category category)
{
    return static_cast<unsigned>(category);
}

// Words of the leaves. Folded: small letters, no accents, Greek without tonos,
// Arabic alef and ya in one shape.
constexpr Word kWords[] = {
    // ---- places ----
    {"afghanistan", Says::place, P(Place::afghanistan)},
    {"afghan", Says::place, P(Place::afghanistan)},
    {"albania", Says::place, P(Place::albania)},
    {"albanian", Says::place, P(Place::albania)},
    {"shqip", Says::place, P(Place::albania)},
    {"algeria", Says::place, P(Place::algeria)},
    {"algerie", Says::place, P(Place::algeria)},
    {"algerian", Says::place, P(Place::algeria)},
    {"argentina", Says::place, P(Place::argentina)},
    {"arg", Says::place, P(Place::argentina)},
    {"armenia", Says::place, P(Place::armenia)},
    {"australia", Says::place, P(Place::australia)},
    {"australian", Says::place, P(Place::australia)},
    {"aussie", Says::place, P(Place::australia)},
    {"austria", Says::place, P(Place::austria)},
    {"osterreich", Says::place, P(Place::austria)},
    {"azerbaijan", Says::place, P(Place::azerbaijan)},
    {"azerbaycan", Says::place, P(Place::azerbaijan)},
    {"bahrain", Says::place, P(Place::bahrain)},
    {"bangladesh", Says::place, P(Place::bangladesh)},
    {"belarus", Says::place, P(Place::belarus)},
    {"belgium", Says::place, P(Place::belgium)},
    {"belgique", Says::place, P(Place::belgium)},
    {"belgie", Says::place, P(Place::belgium)},
    {"bhutan", Says::place, P(Place::bhutan)},
    {"bolivia", Says::place, P(Place::bolivia)},
    {"bosnia", Says::place, P(Place::bosnia)},
    {"bosna", Says::place, P(Place::bosnia)},
    {"brazil", Says::place, P(Place::brazil)},
    {"brasil", Says::place, P(Place::brazil)},
    {"brazilian", Says::place, P(Place::brazil)},
    {"bulgaria", Says::place, P(Place::bulgaria)},
    {"bulgarian", Says::place, P(Place::bulgaria)},
    {"cabo verde", Says::place, P(Place::cabo_verde)},
    {"cape verde", Says::place, P(Place::cabo_verde)},
    {"canada", Says::place, P(Place::canada)},
    {"canadian", Says::place, P(Place::canada)},
    {"chile", Says::place, P(Place::chile)},
    {"china", Says::place, P(Place::china)},
    {"colombia", Says::place, P(Place::colombia)},
    {"costa rica", Says::place, P(Place::costa_rica)},
    {"croatia", Says::place, P(Place::croatia)},
    {"hrvatska", Says::place, P(Place::croatia)},
    {"cuba", Says::place, P(Place::cuba)},
    {"cyprus", Says::place, P(Place::cyprus)},
    {"czech", Says::place, P(Place::czechia)},
    {"czechia", Says::place, P(Place::czechia)},
    {"cesko", Says::place, P(Place::czechia)},
    {"denmark", Says::place, P(Place::denmark)},
    {"danmark", Says::place, P(Place::denmark)},
    {"dominicana", Says::place, P(Place::dominican_republic)},
    {"dominican", Says::place, P(Place::dominican_republic)},
    {"ecuador", Says::place, P(Place::ecuador)},
    {"egypt", Says::place, P(Place::egypt)},
    {"egypte", Says::place, P(Place::egypt)},
    {"masr", Says::place, P(Place::egypt)},
    {"مصر", Says::place, P(Place::egypt)},
    {"el salvador", Says::place, P(Place::el_salvador)},
    {"salvador", Says::place, P(Place::el_salvador)},
    {"estonia", Says::place, P(Place::estonia)},
    {"ethiopia", Says::place, P(Place::ethiopia)},
    {"ethopia", Says::place, P(Place::ethiopia)},
    {"finland", Says::place, P(Place::finland)},
    {"suomi", Says::place, P(Place::finland)},
    {"france", Says::place, P(Place::france)},
    {"georgia", Says::place, P(Place::georgia)},
    {"germany", Says::place, P(Place::germany)},
    {"deutschland", Says::place, P(Place::germany)},
    {"ghana", Says::place, P(Place::ghana)},
    {"greece", Says::place, P(Place::greece)},
    {"hellas", Says::place, P(Place::greece)},
    {"ελλαδα", Says::place, P(Place::greece)},
    {"guatemala", Says::place, P(Place::guatemala)},
    {"haiti", Says::place, P(Place::haiti)},
    {"honduras", Says::place, P(Place::honduras)},
    {"hong kong", Says::place, P(Place::hong_kong)},
    {"hongkong", Says::place, P(Place::hong_kong)},
    {"hungary", Says::place, P(Place::hungary)},
    {"magyar", Says::place, P(Place::hungary)},
    {"iceland", Says::place, P(Place::iceland)},
    {"india", Says::place, P(Place::india)},
    {"indian", Says::place, P(Place::india)},
    {"indonesia", Says::place, P(Place::indonesia)},
    {"iran", Says::place, P(Place::iran)},
    {"iranian", Says::place, P(Place::iran)},
    {"iraq", Says::place, P(Place::iraq)},
    {"iraqi", Says::place, P(Place::iraq)},
    {"ireland", Says::place, P(Place::ireland)},
    {"irish", Says::place, P(Place::ireland)},
    {"eire", Says::place, P(Place::ireland)},
    {"loi", Says::place, P(Place::ireland)},
    {"nifl", Says::place, P(Place::ireland)},
    {"gaa", Says::place, P(Place::ireland)},
    {"clubber", Says::place, P(Place::ireland)},
    {"israel", Says::place, P(Place::israel)},
    {"italy", Says::place, P(Place::italy)},
    {"italia", Says::place, P(Place::italy)},
    {"jamaica", Says::place, P(Place::jamaica)},
    {"japan", Says::place, P(Place::japan)},
    {"jordan", Says::place, P(Place::jordan)},
    {"kazakhstan", Says::place, P(Place::kazakhstan)},
    {"kazachstan", Says::place, P(Place::kazakhstan)},
    {"kenya", Says::place, P(Place::kenya)},
    {"kosovo", Says::place, P(Place::kosovo)},
    {"kosova", Says::place, P(Place::kosovo)},
    {"kurdistan", Says::place, P(Place::kurdistan)},
    {"kuwait", Says::place, P(Place::kuwait)},
    {"latvia", Says::place, P(Place::latvia)},
    {"lebanon", Says::place, P(Place::lebanon)},
    {"liban", Says::place, P(Place::lebanon)},
    {"libya", Says::place, P(Place::libya)},
    {"lithuania", Says::place, P(Place::lithuania)},
    {"luxembourg", Says::place, P(Place::luxembourg)},
    {"malaysia", Says::place, P(Place::malaysia)},
    {"malta", Says::place, P(Place::malta)},
    {"mauritania", Says::place, P(Place::mauritania)},
    {"mauritius", Says::place, P(Place::mauritius)},
    {"mexico", Says::place, P(Place::mexico)},
    {"mexican", Says::place, P(Place::mexico)},
    {"moldova", Says::place, P(Place::moldova)},
    {"montenegro", Says::place, P(Place::montenegro)},
    {"crna gora", Says::place, P(Place::montenegro)},
    {"morocco", Says::place, P(Place::morocco)},
    {"maroc", Says::place, P(Place::morocco)},
    {"المغرب", Says::place, P(Place::morocco)},
    {"nepal", Says::place, P(Place::nepal)},
    {"netherlands", Says::place, P(Place::netherlands)},
    {"nederland", Says::place, P(Place::netherlands)},
    {"holland", Says::place, P(Place::netherlands)},
    {"new zealand", Says::place, P(Place::new_zealand)},
    {"nicaragua", Says::place, P(Place::nicaragua)},
    {"nigeria", Says::place, P(Place::nigeria)},
    {"macedonia", Says::place, P(Place::north_macedonia)},
    {"norway", Says::place, P(Place::norway)},
    {"norge", Says::place, P(Place::norway)},
    {"oman", Says::place, P(Place::oman)},
    {"pakistan", Says::place, P(Place::pakistan)},
    {"pakistani", Says::place, P(Place::pakistan)},
    {"palestine", Says::place, P(Place::palestine)},
    {"palestina", Says::place, P(Place::palestine)},
    {"palastina", Says::place, P(Place::palestine)},
    {"panama", Says::place, P(Place::panama)},
    {"paraguay", Says::place, P(Place::paraguay)},
    {"peru", Says::place, P(Place::peru)},
    {"philippines", Says::place, P(Place::philippines)},
    {"pinoy", Says::place, P(Place::philippines)},
    {"poland", Says::place, P(Place::poland)},
    {"polska", Says::place, P(Place::poland)},
    {"portugal", Says::place, P(Place::portugal)},
    {"puerto rico", Says::place, P(Place::puerto_rico)},
    {"qatar", Says::place, P(Place::qatar)},
    {"quebec", Says::place, P(Place::quebec)},
    {"romania", Says::place, P(Place::romania)},
    {"romanian", Says::place, P(Place::romania)},
    {"russia", Says::place, P(Place::russia)},
    {"saudi", Says::place, P(Place::saudi_arabia)},
    {"saudia", Says::place, P(Place::saudi_arabia)},
    {"ksa", Says::place, P(Place::saudi_arabia)},
    {"scotland", Says::place, P(Place::scotland)},
    {"scottish", Says::place, P(Place::scotland)},
    {"serbia", Says::place, P(Place::serbia)},
    {"srbija", Says::place, P(Place::serbia)},
    {"singapore", Says::place, P(Place::singapore)},
    {"slovakia", Says::place, P(Place::slovakia)},
    {"slovenia", Says::place, P(Place::slovenia)},
    {"somalia", Says::place, P(Place::somalia)},
    {"south africa", Says::place, P(Place::south_africa)},
    {"south korea", Says::place, P(Place::south_korea)},
    {"korea", Says::place, P(Place::south_korea)},
    {"spain", Says::place, P(Place::spain)},
    {"espana", Says::place, P(Place::spain)},
    {"sri lanka", Says::place, P(Place::sri_lanka)},
    {"sri linka", Says::place, P(Place::sri_lanka)},
    {"sudan", Says::place, P(Place::sudan)},
    {"suriname", Says::place, P(Place::suriname)},
    {"sweden", Says::place, P(Place::sweden)},
    {"sverige", Says::place, P(Place::sweden)},
    {"switzerland", Says::place, P(Place::switzerland)},
    {"schweiz", Says::place, P(Place::switzerland)},
    {"suisse", Says::place, P(Place::switzerland)},
    {"swiss", Says::place, P(Place::switzerland)},
    {"syria", Says::place, P(Place::syria)},
    {"syrian", Says::place, P(Place::syria)},
    {"taiwan", Says::place, P(Place::taiwan)},
    {"tajikistan", Says::place, P(Place::tajikistan)},
    {"thailand", Says::place, P(Place::thailand)},
    {"trinidad", Says::place, P(Place::trinidad)},
    {"tunisia", Says::place, P(Place::tunisia)},
    {"tunisie", Says::place, P(Place::tunisia)},
    {"turkey", Says::place, P(Place::turkey)},
    {"turkiye", Says::place, P(Place::turkey)},
    {"uae", Says::place, P(Place::uae)},
    {"emirates", Says::place, P(Place::uae)},
    {"uganda", Says::place, P(Place::uganda)},
    {"ukraine", Says::place, P(Place::ukraine)},
    {"uk", Says::place, P(Place::united_kingdom)},
    {"united kingdom", Says::place, P(Place::united_kingdom)},
    {"britain", Says::place, P(Place::united_kingdom)},
    {"british", Says::place, P(Place::united_kingdom)},
    {"england", Says::place, P(Place::united_kingdom)},
    {"usa", Says::place, P(Place::united_states)},
    {"united states", Says::place, P(Place::united_states)},
    {"america", Says::place, P(Place::united_states)},
    {"american", Says::place, P(Place::united_states)},
    {"uruguay", Says::place, P(Place::uruguay)},
    {"uzbekistan", Says::place, P(Place::uzbekistan)},
    {"venezuela", Says::place, P(Place::venezuela)},
    {"vietnam", Says::place, P(Place::vietnam)},
    {"wales", Says::place, P(Place::wales)},
    {"cymru", Says::place, P(Place::wales)},
    {"welsh", Says::place, P(Place::wales)},
    {"yemen", Says::place, P(Place::yemen)},
    // regions
    {"caribbean", Says::place, P(Place::caribbean)},
    {"africa", Says::place, P(Place::africa)},
    {"african", Says::place, P(Place::africa)},
    {"afrique", Says::place, P(Place::africa)},
    {"latino", Says::place, P(Place::latin_america)},
    {"latin", Says::place, P(Place::latin_america)},
    {"latina", Says::place, P(Place::latin_america)},
    {"nordic", Says::place, P(Place::nordics)},
    {"scandinavia", Says::place, P(Place::nordics)},
    {"balkan", Says::place, P(Place::balkans)},
    {"balkans", Says::place, P(Place::balkans)},
    {"exyu", Says::place, P(Place::balkans)},
    {"europe", Says::place, P(Place::europe)},
    {"european", Says::place, P(Place::europe)},
    {"avrupa", Says::place, P(Place::europe)},
    {"asia", Says::place, P(Place::asia)},
    {"asian", Says::place, P(Place::asia)},
    {"world", Says::place, P(Place::world)},
    {"international", Says::place, P(Place::world)},
    // ---- languages ----
    {"english", Says::language, L(Language::english)},
    {"eng", Says::language, L(Language::english)},
    {"french", Says::language, L(Language::french)},
    {"francais", Says::language, L(Language::french)},
    {"wallonie", Says::language, L(Language::french)},
    {"german", Says::language, L(Language::german)},
    {"deutsch", Says::language, L(Language::german)},
    {"spanish", Says::language, L(Language::spanish)},
    {"espanol", Says::language, L(Language::spanish)},
    {"castellano", Says::language, L(Language::spanish)},
    {"italian", Says::language, L(Language::italian)},
    {"italiano", Says::language, L(Language::italian)},
    {"portuguese", Says::language, L(Language::portuguese)},
    {"portugues", Says::language, L(Language::portuguese)},
    {"dutch", Says::language, L(Language::dutch)},
    {"nederlands", Says::language, L(Language::dutch)},
    {"vlaams", Says::language, L(Language::dutch)},
    {"flemish", Says::language, L(Language::dutch)},
    {"polish", Says::language, L(Language::polish)},
    {"polski", Says::language, L(Language::polish)},
    {"polskie", Says::language, L(Language::polish)},
    {"turkish", Says::language, L(Language::turkish)},
    {"turkce", Says::language, L(Language::turkish)},
    {"turk", Says::language, L(Language::turkish)},
    {"arabic", Says::language, L(Language::arabic)},
    {"arab", Says::language, L(Language::arabic)},
    {"عربي", Says::language, L(Language::arabic)},
    {"عربيه", Says::language, L(Language::arabic)},
    {"greek", Says::language, L(Language::greek)},
    {"russian", Says::language, L(Language::russian)},
    {"hebrew", Says::language, L(Language::hebrew)},
    {"persian", Says::language, L(Language::persian)},
    {"farsi", Says::language, L(Language::persian)},
    {"فارسي", Says::language, L(Language::persian)},
    {"kurdish", Says::language, L(Language::kurdish)},
    {"kurdi", Says::language, L(Language::kurdish)},
    {"hindi", Says::language, L(Language::hindi)},
    {"tamil", Says::language, L(Language::tamil)},
    {"telugu", Says::language, L(Language::telugu)},
    {"telegu", Says::language, L(Language::telugu)},
    {"malayalam", Says::language, L(Language::malayalam)},
    {"kannada", Says::language, L(Language::kannada)},
    {"marathi", Says::language, L(Language::marathi)},
    {"maharati", Says::language, L(Language::marathi)},
    {"punjabi", Says::language, L(Language::punjabi)},
    {"panjabi", Says::language, L(Language::punjabi)},
    {"gujarati", Says::language, L(Language::gujarati)},
    {"gugarati", Says::language, L(Language::gujarati)},
    {"bengali", Says::language, L(Language::bengali)},
    {"bangla", Says::language, L(Language::bengali)},
    {"bangal", Says::language, L(Language::bengali)},
    {"urdu", Says::language, L(Language::urdu)},
    {"bhojpuri", Says::language, L(Language::bhojpuri)},
    {"odia", Says::language, L(Language::odia)},
    {"odisha", Says::language, L(Language::odia)},
    {"assamese", Says::language, L(Language::assamese)},
    {"assam", Says::language, L(Language::assamese)},
    {"sinhala", Says::language, L(Language::sinhala)},
    {"nepali", Says::language, L(Language::nepali)},
    {"pashto", Says::language, L(Language::pashto)},
    {"somali", Says::language, L(Language::somali)},
    {"swahili", Says::language, L(Language::swahili)},
    {"amharic", Says::language, L(Language::amharic)},
    {"afrikaans", Says::language, L(Language::afrikaans)},
    {"chinese", Says::language, L(Language::chinese)},
    {"mandarin", Says::language, L(Language::chinese)},
    {"cantonese", Says::language, L(Language::cantonese)},
    {"japanese", Says::language, L(Language::japanese)},
    {"korean", Says::language, L(Language::korean)},
    {"thai", Says::language, L(Language::thai)},
    {"vietnamese", Says::language, L(Language::vietnamese)},
    {"indonesian", Says::language, L(Language::indonesian)},
    {"malay", Says::language, L(Language::malay)},
    {"filipino", Says::language, L(Language::filipino)},
    {"tagalog", Says::language, L(Language::filipino)},
    {"serbian", Says::language, L(Language::serbian)},
    {"srpski", Says::language, L(Language::serbian)},
    {"croatian", Says::language, L(Language::croatian)},
    {"hrvatski", Says::language, L(Language::croatian)},
    {"bosnian", Says::language, L(Language::bosnian)},
    {"macedonian", Says::language, L(Language::macedonian)},
    {"slovene", Says::language, L(Language::slovene)},
    {"slovenian", Says::language, L(Language::slovene)},
    {"hungarian", Says::language, L(Language::hungarian)},
    {"czech language", Says::language, L(Language::czech)},
    {"cesky", Says::language, L(Language::czech)},
    {"slovak", Says::language, L(Language::slovak)},
    {"ukrainian", Says::language, L(Language::ukrainian)},
    {"belarusian", Says::language, L(Language::belarusian)},
    {"lithuanian", Says::language, L(Language::lithuanian)},
    {"latvian", Says::language, L(Language::latvian)},
    {"estonian", Says::language, L(Language::estonian)},
    {"finnish", Says::language, L(Language::finnish)},
    {"swedish", Says::language, L(Language::swedish)},
    {"svensk", Says::language, L(Language::swedish)},
    {"svenska", Says::language, L(Language::swedish)},
    {"norwegian", Says::language, L(Language::norwegian)},
    {"norsk", Says::language, L(Language::norwegian)},
    {"danish", Says::language, L(Language::danish)},
    {"dansk", Says::language, L(Language::danish)},
    {"danske", Says::language, L(Language::danish)},
    {"icelandic", Says::language, L(Language::icelandic)},
    {"georgian", Says::language, L(Language::georgian)},
    {"armenian", Says::language, L(Language::armenian)},
    {"azerbaijani", Says::language, L(Language::azerbaijani)},
    {"kazakh", Says::language, L(Language::kazakh)},
    {"uzbek", Says::language, L(Language::uzbek)},
    {"maltese", Says::language, L(Language::maltese)},
    {"catalan", Says::language, L(Language::catalan)},
    {"catala", Says::language, L(Language::catalan)},
    {"multi", Says::language, L(Language::multi)},
    {"multilingual", Says::language, L(Language::multi)},
    {"dual", Says::language, L(Language::multi)},
    {"تركي", Says::language, L(Language::turkish)},
    {"تركيه", Says::language, L(Language::turkish)},
    {"هندي", Says::language, L(Language::hindi)},
    {"كوري", Says::language, L(Language::korean)},
    {"كوريه", Says::language, L(Language::korean)},
    {"ايراني", Says::language, L(Language::persian)},
    {"ايرانيه", Says::language, L(Language::persian)},
    {"اجنبي", Says::language, L(Language::english)},
    {"اجنبيه", Says::language, L(Language::english)},
    {"انجليزي", Says::language, L(Language::english)},
    {"مصري", Says::place, P(Place::egypt)},
    {"مصريه", Says::place, P(Place::egypt)},
    {"سوري", Says::place, P(Place::syria)},
    {"سوريه", Says::place, P(Place::syria)},
    {"لبناني", Says::place, P(Place::lebanon)},
    {"لبنانيه", Says::place, P(Place::lebanon)},
    {"عراقي", Says::place, P(Place::iraq)},
    {"عراقيه", Says::place, P(Place::iraq)},
    {"خليجي", Says::place, P(Place::arab_world)},
    {"خليجيه", Says::place, P(Place::arab_world)},
    {"مغربي", Says::place, P(Place::morocco)},
    {"مغربيه", Says::place, P(Place::morocco)},
    {"امازيغيه", Says::place, P(Place::morocco)},
    {"تونسي", Says::place, P(Place::tunisia)},
    {"تونسيه", Says::place, P(Place::tunisia)},
    {"جزائري", Says::place, P(Place::algeria)},
    {"جزائريه", Says::place, P(Place::algeria)},
    {"اسيويه", Says::place, P(Place::asia)},
    {"نتفليكس", Says::platform, C(Category::streaming)},
    {"مارفيل", Says::genre, kGenreSciFi},
    {"رمضان", Says::category, C(Category::series)},
    {"رمضانك", Says::category, C(Category::series)},
    {"كلاسيكيه", Says::genre, kGenreClassic},
    {"سریال", Says::category, C(Category::series)},
    {"ترکی", Says::language, L(Language::turkish)},
    // ---- words that say "a channel of this place" and little else ----
    {"network", Says::category, C(Category::general)},
    {"networks", Says::category, C(Category::general)},
    {"tv", Says::category, C(Category::general)},
    {"channel", Says::category, C(Category::general)},
    {"channels", Says::category, C(Category::general)},
    {"canal", Says::category, C(Category::general)},
    {"canale", Says::category, C(Category::general)},
    {"kanali", Says::category, C(Category::general)},
    {"kanallari", Says::category, C(Category::general)},
    {"kanaly", Says::category, C(Category::general)},
    {"kanalen", Says::category, C(Category::general)},
    {"kanale", Says::category, C(Category::general)},
    {"nbc", Says::category, C(Category::general)},
    {"cbs", Says::category, C(Category::general)},
    {"abc", Says::category, C(Category::general)},
    {"fox", Says::category, C(Category::general)},
    {"cw", Says::category, C(Category::general)},
    {"pbs", Says::category, C(Category::general)},
    {"telemundo", Says::category, C(Category::general)},
    {"univision", Says::category, C(Category::general)},
    {"sky", Says::category, C(Category::general)},
    {"bbc", Says::category, C(Category::general)},
    {"itv", Says::category, C(Category::general)},
    {"rtl", Says::category, C(Category::general)},
    {"dstv", Says::category, C(Category::general)},
    {"azam", Says::category, C(Category::general)},
    {"amp", Says::category, C(Category::general)},
    {"sbt", Says::category, C(Category::general)},
    {"globo", Says::category, C(Category::general)},
    {"record", Says::category, C(Category::general)},
    {"rogers", Says::category, C(Category::general)},
    {"tvn", Says::category, C(Category::general)},
    {"tvp", Says::category, C(Category::general)},
    {"pink", Says::category, C(Category::general)},
    {"roya", Says::category, C(Category::general)},
    {"focus sat", Says::category, C(Category::general)},
    {"expressen", Says::category, C(Category::general)},
    {"vgtv", Says::category, C(Category::general)},
    {"oneplay", Says::category, C(Category::general)},
    {"direct one", Says::category, C(Category::general)},
    {"foreign", Says::category, C(Category::general)},
    {"adult swim", Says::category, C(Category::entertainment)},
    {"on air", Says::category, C(Category::general)},
    {"city", Says::category, C(Category::local)},
    {"mena", Says::place, P(Place::arab_world)},
    {"arabia", Says::place, P(Place::arab_world)},
    {"worldwide", Says::place, P(Place::world)},
    {"play", Says::platform, C(Category::streaming)},
    {"online", Says::platform, C(Category::streaming)},
    {"go", Says::platform, C(Category::streaming)},
    {"player", Says::platform, C(Category::streaming)},
    {"wow", Says::platform, C(Category::streaming)},
    {"plus", Says::platform, C(Category::streaming)},
    {"hot", Says::platform, C(Category::streaming)},
    {"yes", Says::platform, C(Category::streaming)},
    {"cellcom", Says::platform, C(Category::streaming)},
    {"partner", Says::platform, C(Category::streaming)},
    {"cinemania", Says::category, C(Category::movies)},
    {"hollywood", Says::category, C(Category::movies)},
    {"kurdcinema", Says::category, C(Category::movies)},
    {"singer", Says::category, C(Category::music)},
    {"cultura", Says::category, C(Category::lifestyle)},
    {"package", Says::category, C(Category::sports)},
    {"leagues", Says::category, C(Category::sports)},
    {"college", Says::category, C(Category::sports)},
    {"team", Says::category, C(Category::sports)},
    {"games", Says::category, C(Category::sports)},
    {"pass", Says::category, C(Category::sports)},
    {"club", Says::category, C(Category::sports)},
    {"premier", Says::category, C(Category::sports)},
    {"campeones", Says::category, C(Category::sports)},
    {"vanarama", Says::category, C(Category::sports)},
    {"superleague", Says::category, C(Category::sports)},
    {"betting", Says::category, C(Category::sports)},
    {"champ", Says::category, C(Category::sports)},
    {"spfl", Says::category, C(Category::sports)},
    {"otros", Says::category, C(Category::general)},
    // ---- subtitles and dubbing ----
    {"sub", Says::subtitled, L(Language::none)},
    {"subs", Says::subtitled, L(Language::none)},
    {"subtitles", Says::subtitled, L(Language::none)},
    {"subtitled", Says::subtitled, L(Language::none)},
    {"subtitulada", Says::subtitled, L(Language::spanish)},
    {"multisub", Says::subtitled, L(Language::multi)},
    {"multi sub", Says::subtitled, L(Language::multi)},
    {"vostfr", Says::subtitled, L(Language::french)},
    {"vost", Says::subtitled, L(Language::none)},
    {"مترجم", Says::subtitled, L(Language::arabic)},
    {"مترجمه", Says::subtitled, L(Language::arabic)},
    {"زيرنويس", Says::subtitled, L(Language::persian)},
    {"dub", Says::dubbed, L(Language::none)},
    {"dubbed", Says::dubbed, L(Language::none)},
    {"dubbing", Says::dubbed, L(Language::none)},
    {"vf", Says::dubbed, L(Language::french)},
    {"dublaj", Says::dubbed_here, 0},
    {"szinkronizalt", Says::dubbed_here, 0},
    {"doblado", Says::dubbed_here, 0},
    {"doblada", Says::dubbed_here, 0},
    {"doblaje", Says::dubbed_here, 0},
    {"sync", Says::dubbed_here, 0},
    {"مدبلج", Says::dubbed_here, 0},
    {"مدبلجه", Says::dubbed_here, 0},
    {"دوبله", Says::dubbed_here, 0},
    // ---- categories ----
    {"sport", Says::category, C(Category::sports)},
    {"sports", Says::category, C(Category::sports)},
    {"spor", Says::category, C(Category::sports)},
    {"deporte", Says::category, C(Category::sports)},
    {"deportes", Says::category, C(Category::sports)},
    {"desporto", Says::category, C(Category::sports)},
    {"esporte", Says::category, C(Category::sports)},
    {"esportes", Says::category, C(Category::sports)},
    {"sportowe", Says::category, C(Category::sports)},
    {"sportski", Says::category, C(Category::sports)},
    {"sportske", Says::category, C(Category::sports)},
    {"sportovi", Says::category, C(Category::sports)},
    {"αθλητικα", Says::category, C(Category::sports)},
    {"αθλητισμος", Says::category, C(Category::sports)},
    {"رياضه", Says::category, C(Category::sports)},
    {"رياضيه", Says::category, C(Category::sports)},
    {"mac kanallari", Says::category, C(Category::sports)},
    {"football", Says::category, C(Category::sports)},
    {"soccer", Says::category, C(Category::sports)},
    {"futbol", Says::category, C(Category::sports)},
    {"fussball", Says::category, C(Category::sports)},
    {"calcio", Says::category, C(Category::sports)},
    {"voetbal", Says::category, C(Category::sports)},
    {"pilka", Says::category, C(Category::sports)},
    {"liga", Says::category, C(Category::sports)},
    {"laliga", Says::category, C(Category::sports)},
    {"league", Says::category, C(Category::sports)},
    {"ligue", Says::category, C(Category::sports)},
    {"bundesliga", Says::category, C(Category::sports)},
    {"serie a", Says::category, C(Category::sports)},
    {"seria a", Says::category, C(Category::sports)},
    {"epl", Says::category, C(Category::sports)},
    {"efl", Says::category, C(Category::sports)},
    {"nfl", Says::category, C(Category::sports)},
    {"nba", Says::category, C(Category::sports)},
    {"wnba", Says::category, C(Category::sports)},
    {"nhl", Says::category, C(Category::sports)},
    {"mlb", Says::category, C(Category::sports)},
    {"milb", Says::category, C(Category::sports)},
    {"mls", Says::category, C(Category::sports)},
    {"ncaa", Says::category, C(Category::sports)},
    {"ncaab", Says::category, C(Category::sports)},
    {"ncaaf", Says::category, C(Category::sports)},
    {"nfhs", Says::category, C(Category::sports)},
    {"ahl", Says::category, C(Category::sports)},
    {"ohl", Says::category, C(Category::sports)},
    {"whl", Says::category, C(Category::sports)},
    {"qmjhl", Says::category, C(Category::sports)},
    {"nrl", Says::category, C(Category::sports)},
    {"afl", Says::category, C(Category::sports)},
    {"ufc", Says::category, C(Category::sports)},
    {"wwe", Says::category, C(Category::sports)},
    {"aew", Says::category, C(Category::sports)},
    {"boxing", Says::category, C(Category::sports)},
    {"boxeo", Says::category, C(Category::sports)},
    {"boxe", Says::category, C(Category::sports)},
    {"fight", Says::category, C(Category::sports)},
    {"fights", Says::category, C(Category::sports)},
    {"walki", Says::category, C(Category::sports)},
    {"mma", Says::category, C(Category::sports)},
    {"glory", Says::category, C(Category::sports)},
    {"wrestling", Says::category, C(Category::sports)},
    {"f1", Says::category, C(Category::sports)},
    {"formula", Says::category, C(Category::sports)},
    {"formule", Says::category, C(Category::sports)},
    {"motogp", Says::category, C(Category::sports)},
    {"moto gp", Says::category, C(Category::sports)},
    {"mxgp", Says::category, C(Category::sports)},
    {"rally", Says::category, C(Category::sports)},
    {"supercross", Says::category, C(Category::sports)},
    {"racing", Says::category, C(Category::sports)},
    {"nascar", Says::category, C(Category::sports)},
    {"indycar", Says::category, C(Category::sports)},
    {"cricket", Says::category, C(Category::sports)},
    {"rugby", Says::category, C(Category::sports)},
    {"tennis", Says::category, C(Category::sports)},
    {"atp", Says::category, C(Category::sports)},
    {"golf", Says::category, C(Category::sports)},
    {"hockey", Says::category, C(Category::sports)},
    {"hokej", Says::category, C(Category::sports)},
    {"liiga", Says::category, C(Category::sports)},
    {"handball", Says::category, C(Category::sports)},
    {"handbollsligan", Says::category, C(Category::sports)},
    {"innebandy", Says::category, C(Category::sports)},
    {"basketball", Says::category, C(Category::sports)},
    {"baloncesto", Says::category, C(Category::sports)},
    {"basket", Says::category, C(Category::sports)},
    {"volley", Says::category, C(Category::sports)},
    {"volleyball", Says::category, C(Category::sports)},
    {"darts", Says::category, C(Category::sports)},
    {"pdc", Says::category, C(Category::sports)},
    {"pool", Says::category, C(Category::sports)},
    {"snooker", Says::category, C(Category::sports)},
    {"cycling", Says::category, C(Category::sports)},
    {"arena", Says::category, C(Category::sports)},
    {"matchday", Says::category, C(Category::sports)},
    {"teams", Says::category, C(Category::sports)},
    {"cup", Says::category, C(Category::sports)},
    {"uefa", Says::category, C(Category::sports)},
    {"fifa", Says::category, C(Category::sports)},
    {"champions", Says::category, C(Category::sports)},
    {"championship", Says::category, C(Category::sports)},
    {"worldcup", Says::category, C(Category::sports)},
    {"world cup", Says::category, C(Category::sports)},
    {"olympics", Says::category, C(Category::sports)},
    {"horse racing", Says::category, C(Category::sports)},
    {"toros", Says::category, C(Category::sports)},
    {"replay", Says::tag, kTagReplay},
    {"replays", Says::tag, kTagReplay},
    {"repeticion", Says::tag, kTagReplay},
    {"ppv", Says::category, C(Category::events)},
    {"pay per view", Says::category, C(Category::events)},
    {"event", Says::category, C(Category::events)},
    {"events", Says::category, C(Category::events)},
    {"live events", Says::category, C(Category::events)},
    {"live event", Says::category, C(Category::events)},
    {"events only", Says::tag, kTagEventsOnly},
    {"event only", Says::tag, kTagEventsOnly},
    {"directs", Says::category, C(Category::events)},
    {"direkte", Says::category, C(Category::events)},
    {"news", Says::category, C(Category::news)},
    {"haber", Says::category, C(Category::news)},
    {"haber kanallari", Says::category, C(Category::news)},
    {"nieuws", Says::category, C(Category::news)},
    {"nachrichten", Says::category, C(Category::news)},
    {"noticias", Says::category, C(Category::news)},
    {"notizie", Says::category, C(Category::news)},
    {"informacyjne", Says::category, C(Category::news)},
    {"informatii", Says::category, C(Category::news)},
    {"stiri", Says::category, C(Category::news)},
    {"vesti", Says::category, C(Category::news)},
    {"vijesti", Says::category, C(Category::news)},
    {"ειδησεις", Says::category, C(Category::news)},
    {"اخبار", Says::category, C(Category::news)},
    {"اخباريه", Says::category, C(Category::news)},
    {"aktualnosci", Says::category, C(Category::news)},
    {"kids", Says::category, C(Category::kids)},
    {"kid", Says::category, C(Category::kids)},
    {"children", Says::category, C(Category::kids)},
    {"childrens", Says::category, C(Category::kids)},
    {"child", Says::category, C(Category::kids)},
    {"cartoon", Says::category, C(Category::kids)},
    {"cartoons", Says::category, C(Category::kids)},
    {"cartoni", Says::category, C(Category::kids)},
    {"enfants", Says::category, C(Category::kids)},
    {"jeunesse", Says::category, C(Category::kids)},
    {"kinder", Says::category, C(Category::kids)},
    {"kinderen", Says::category, C(Category::kids)},
    {"dzieci", Says::category, C(Category::kids)},
    {"dla dzieci", Says::category, C(Category::kids)},
    {"dzieciece", Says::category, C(Category::kids)},
    {"bajki", Says::category, C(Category::kids)},
    {"deti", Says::category, C(Category::kids)},
    {"copii", Says::category, C(Category::kids)},
    {"cocuk", Says::category, C(Category::kids)},
    {"ninos", Says::category, C(Category::kids)},
    {"ninas", Says::category, C(Category::kids)},
    {"infantil", Says::category, C(Category::kids)},
    {"infantiles", Says::category, C(Category::kids)},
    {"criancas", Says::category, C(Category::kids)},
    {"bambini", Says::category, C(Category::kids)},
    {"figli", Says::category, C(Category::kids)},
    {"barn", Says::category, C(Category::kids)},
    {"born", Says::category, C(Category::kids)},
    {"borne", Says::category, C(Category::kids)},
    {"bornefilm", Says::category, C(Category::kids)},
    {"detsa", Says::category, C(Category::kids)},
    {"deca", Says::category, C(Category::kids)},
    {"deciji", Says::category, C(Category::kids)},
    {"decije", Says::category, C(Category::kids)},
    {"djecji", Says::category, C(Category::kids)},
    {"crtani", Says::category, C(Category::kids)},
    {"παιδικα", Says::category, C(Category::kids)},
    {"اطفال", Says::category, C(Category::kids)},
    {"كرتون", Says::category, C(Category::kids)},
    {"per femije", Says::category, C(Category::kids)},
    {"femije", Says::category, C(Category::kids)},
    {"junior", Says::category, C(Category::kids)},
    {"boomerang", Says::category, C(Category::kids)},
    {"cbeebies", Says::category, C(Category::kids)},
    {"movie", Says::category, C(Category::movies)},
    {"movies", Says::category, C(Category::movies)},
    {"moveis", Says::category, C(Category::movies)},
    {"film", Says::category, C(Category::movies)},
    {"films", Says::category, C(Category::movies)},
    {"filme", Says::category, C(Category::movies)},
    {"filmy", Says::category, C(Category::movies)},
    {"filmi", Says::category, C(Category::movies)},
    {"filmovi", Says::category, C(Category::movies)},
    {"filmat", Says::category, C(Category::movies)},
    {"filmer", Says::category, C(Category::movies)},
    {"filmes", Says::category, C(Category::movies)},
    {"filmek", Says::category, C(Category::movies)},
    {"filmski", Says::category, C(Category::movies)},
    {"filmowe", Says::category, C(Category::movies)},
    {"cinema", Says::category, C(Category::movies)},
    {"cine", Says::category, C(Category::movies)},
    {"kino", Says::category, C(Category::movies)},
    {"sinema", Says::category, C(Category::movies)},
    {"pelicula", Says::category, C(Category::movies)},
    {"peliculas", Says::category, C(Category::movies)},
    {"pellicola", Says::category, C(Category::movies)},
    {"bioscoop", Says::category, C(Category::movies)},
    {"افلام", Says::category, C(Category::movies)},
    {"فيلم", Says::category, C(Category::movies)},
    {"ταινιες", Says::category, C(Category::movies)},
    {"κινηματογραφος", Says::category, C(Category::movies)},
    {"box office", Says::category, C(Category::movies)},
    {"blockbuster", Says::category, C(Category::movies)},
    {"series", Says::category, C(Category::series)},
    {"serie", Says::category, C(Category::series)},
    {"serien", Says::category, C(Category::series)},
    {"seriale", Says::category, C(Category::series)},
    {"seriali", Says::category, C(Category::series)},
    {"serije", Says::category, C(Category::series)},
    {"serier", Says::category, C(Category::series)},
    {"sorozat", Says::category, C(Category::series)},
    {"dizi", Says::category, C(Category::series)},
    {"diziler", Says::category, C(Category::series)},
    {"sdarot", Says::category, C(Category::series)},
    {"مسلسلات", Says::category, C(Category::series)},
    {"مسلسل", Says::category, C(Category::series)},
    {"سريال", Says::category, C(Category::series)},
    {"natok", Says::category, C(Category::series)},
    {"web series", Says::category, C(Category::series)},
    {"webseries", Says::category, C(Category::series)},
    {"tv shows", Says::category, C(Category::series)},
    {"shows", Says::category, C(Category::series)},
    {"show", Says::category, C(Category::series)},
    {"telenovela", Says::category, C(Category::series)},
    {"telenovelas", Says::category, C(Category::series)},
    {"novelas", Says::category, C(Category::series)},
    {"documentary", Says::category, C(Category::documentary)},
    {"documentaries", Says::category, C(Category::documentary)},
    {"doc", Says::category, C(Category::documentary)},
    {"docs", Says::category, C(Category::documentary)},
    {"doku", Says::category, C(Category::documentary)},
    {"dokus", Says::category, C(Category::documentary)},
    {"dokumentation", Says::category, C(Category::documentary)},
    {"dokumentar", Says::category, C(Category::documentary)},
    {"dokumentarni", Says::category, C(Category::documentary)},
    {"dokumentalne", Says::category, C(Category::documentary)},
    {"documentaire", Says::category, C(Category::documentary)},
    {"documentaires", Says::category, C(Category::documentary)},
    {"documentales", Says::category, C(Category::documentary)},
    {"documentario", Says::category, C(Category::documentary)},
    {"documentare", Says::category, C(Category::documentary)},
    {"belgesel", Says::category, C(Category::documentary)},
    {"ντοκιμαντερ", Says::category, C(Category::documentary)},
    {"وثائقيه", Says::category, C(Category::documentary)},
    {"وثائقي", Says::category, C(Category::documentary)},
    {"discovery", Says::category, C(Category::documentary)},
    {"nat geo", Says::category, C(Category::documentary)},
    {"national geographic", Says::category, C(Category::documentary)},
    {"music", Says::category, C(Category::music)},
    {"musik", Says::category, C(Category::music)},
    {"musica", Says::category, C(Category::music)},
    {"muziek", Says::category, C(Category::music)},
    {"muzyka", Says::category, C(Category::music)},
    {"muzyczne", Says::category, C(Category::music)},
    {"muzicki", Says::category, C(Category::music)},
    {"muzik", Says::category, C(Category::music)},
    {"muzikale", Says::category, C(Category::music)},
    {"muzicale", Says::category, C(Category::music)},
    {"hudba", Says::category, C(Category::music)},
    {"μουσικη", Says::category, C(Category::music)},
    {"موسيقى", Says::category, C(Category::music)},
    {"mtv", Says::category, C(Category::music)},
    {"karaoke", Says::category, C(Category::music)},
    {"radio", Says::category, C(Category::radio)},
    {"radios", Says::category, C(Category::radio)},
    {"dab", Says::category, C(Category::radio)},
    {"fm", Says::category, C(Category::radio)},
    {"religion", Says::category, C(Category::religion)},
    {"religious", Says::category, C(Category::religion)},
    {"religiao", Says::category, C(Category::religion)},
    {"religioso", Says::category, C(Category::religion)},
    {"dini", Says::category, C(Category::religion)},
    {"islamic", Says::category, C(Category::religion)},
    {"islam", Says::category, C(Category::religion)},
    {"christian", Says::category, C(Category::religion)},
    {"catholic", Says::category, C(Category::religion)},
    {"church", Says::category, C(Category::religion)},
    {"bible", Says::category, C(Category::religion)},
    {"biblical", Says::category, C(Category::religion)},
    {"biblique", Says::category, C(Category::religion)},
    {"quran", Says::category, C(Category::religion)},
    {"القران", Says::category, C(Category::religion)},
    {"اسلاميه", Says::category, C(Category::religion)},
    {"دينيه", Says::category, C(Category::religion)},
    {"naats", Says::category, C(Category::religion)},
    {"qawwali", Says::category, C(Category::religion)},
    {"gospel", Says::category, C(Category::religion)},
    {"hindu", Says::category, C(Category::religion)},
    {"general", Says::category, C(Category::general)},
    {"generale", Says::category, C(Category::general)},
    {"generalistas", Says::category, C(Category::general)},
    {"generalist", Says::category, C(Category::general)},
    {"ogolnotematyczne", Says::category, C(Category::general)},
    {"allgemein", Says::category, C(Category::general)},
    {"algemeen", Says::category, C(Category::general)},
    {"γενικα", Says::category, C(Category::general)},
    {"γενικος", Says::category, C(Category::general)},
    {"عامه", Says::category, C(Category::general)},
    {"live tv", Says::category, C(Category::general)},
    {"entertainment", Says::category, C(Category::entertainment)},
    {"unterhaltung", Says::category, C(Category::entertainment)},
    {"vermaak", Says::category, C(Category::entertainment)},
    {"divertisment", Says::category, C(Category::entertainment)},
    {"eglence", Says::category, C(Category::entertainment)},
    {"intretenimento", Says::category, C(Category::entertainment)},
    {"entretenimiento", Says::category, C(Category::entertainment)},
    {"entretenimento", Says::category, C(Category::entertainment)},
    {"zabava", Says::category, C(Category::entertainment)},
    {"variety", Says::category, C(Category::entertainment)},
    {"varietes", Says::category, C(Category::entertainment)},
    {"برامج", Says::category, C(Category::entertainment)},
    {"منوعه", Says::category, C(Category::entertainment)},
    {"local", Says::category, C(Category::local)},
    {"locals", Says::category, C(Category::local)},
    {"locales", Says::category, C(Category::local)},
    {"loc", Says::category, C(Category::local)},
    {"lokal", Says::category, C(Category::local)},
    {"regional", Says::category, C(Category::local)},
    {"regionali", Says::category, C(Category::local)},
    {"regionaal", Says::category, C(Category::local)},
    {"regionalne", Says::category, C(Category::local)},
    {"regionales", Says::category, C(Category::local)},
    {"yerel", Says::category, C(Category::local)},
    {"xxx", Says::category, C(Category::adult)},
    {"adult", Says::category, C(Category::adult)},
    {"adults", Says::category, C(Category::adult)},
    {"adultos", Says::category, C(Category::adult)},
    {"adulte", Says::category, C(Category::adult)},
    {"adultes", Says::category, C(Category::adult)},
    {"erotic", Says::category, C(Category::adult)},
    {"erotik", Says::category, C(Category::adult)},
    {"porn", Says::category, C(Category::adult)},
    {"porno", Says::category, C(Category::adult)},
    {"18+", Says::category, C(Category::adult)},
    {"+18", Says::category, C(Category::adult)},
    {"24x7", Says::category, C(Category::around_the_clock)},
    {"shopping", Says::category, C(Category::shopping)},
    {"teleshopping", Says::category, C(Category::shopping)},
    {"shop", Says::category, C(Category::shopping)},
    {"qvc", Says::category, C(Category::shopping)},
    {"lifestyle", Says::category, C(Category::lifestyle)},
    {"estilo de vida", Says::category, C(Category::lifestyle)},
    {"cooking", Says::category, C(Category::lifestyle)},
    {"food", Says::category, C(Category::lifestyle)},
    {"travel", Says::category, C(Category::lifestyle)},
    {"education", Says::category, C(Category::lifestyle)},
    {"relax", Says::category, C(Category::lifestyle)},
    {"cams", Says::category, C(Category::lifestyle)},
    {"webcam", Says::category, C(Category::lifestyle)},
    // ---- platforms ----
    {"netflix", Says::platform, C(Category::streaming)},
    {"nf", Says::platform, C(Category::streaming)},
    {"prime", Says::platform, C(Category::streaming)},
    {"prime video", Says::platform, C(Category::streaming)},
    {"amazon", Says::platform, C(Category::streaming)},
    {"amazon prime", Says::platform, C(Category::streaming)},
    {"disney", Says::platform, C(Category::streaming)},
    {"disney+", Says::platform, C(Category::streaming)},
    {"d+", Says::platform, C(Category::streaming)},
    {"apple", Says::platform, C(Category::streaming)},
    {"apple+", Says::platform, C(Category::streaming)},
    {"a+", Says::platform, C(Category::streaming)},
    {"apple tv", Says::platform, C(Category::streaming)},
    {"apple tv+", Says::platform, C(Category::streaming)},
    {"hbo", Says::platform, C(Category::streaming)},
    {"hbo max", Says::platform, C(Category::streaming)},
    {"max", Says::platform, C(Category::streaming)},
    {"hulu", Says::platform, C(Category::streaming)},
    {"peacock", Says::platform, C(Category::streaming)},
    {"paramount", Says::platform, C(Category::streaming)},
    {"paramount+", Says::platform, C(Category::streaming)},
    {"p+", Says::platform, C(Category::streaming)},
    {"sky store", Says::platform, C(Category::streaming)},
    {"now tv", Says::platform, C(Category::streaming)},
    {"nowtv", Says::platform, C(Category::streaming)},
    {"joyn", Says::platform, C(Category::streaming)},
    {"rtl+", Says::platform, C(Category::streaming)},
    {"ardmediathek", Says::platform, C(Category::streaming)},
    {"viaplay", Says::platform, C(Category::streaming)},
    {"discovery+", Says::platform, C(Category::streaming)},
    {"c more", Says::platform, C(Category::streaming)},
    {"tv2 play", Says::platform, C(Category::streaming)},
    {"svtplay", Says::platform, C(Category::streaming)},
    {"telia", Says::platform, C(Category::streaming)},
    {"play+", Says::platform, C(Category::streaming)},
    {"videoland", Says::platform, C(Category::streaming)},
    {"npo plus", Says::platform, C(Category::streaming)},
    {"ziggo", Says::platform, C(Category::streaming)},
    {"canal+", Says::platform, C(Category::streaming)},
    {"mycanal", Says::platform, C(Category::streaming)},
    {"tf1+", Says::platform, C(Category::streaming)},
    {"atres player", Says::platform, C(Category::streaming)},
    {"atresplayer", Says::platform, C(Category::streaming)},
    {"tivify", Says::platform, C(Category::streaming)},
    {"movistar", Says::platform, C(Category::streaming)},
    {"movistar+", Says::platform, C(Category::streaming)},
    {"m+", Says::platform, C(Category::streaming)},
    {"rakuten", Says::platform, C(Category::streaming)},
    {"tubi", Says::platform, C(Category::streaming)},
    {"pluto", Says::platform, C(Category::streaming)},
    {"plex", Says::platform, C(Category::streaming)},
    {"tod", Says::platform, C(Category::streaming)},
    {"shahid", Says::platform, C(Category::streaming)},
    {"stc", Says::platform, C(Category::streaming)},
    {"yupp", Says::platform, C(Category::streaming)},
    {"yupp tv", Says::platform, C(Category::streaming)},
    {"yp", Says::platform, C(Category::streaming)},
    {"watch it", Says::platform, C(Category::streaming)},
    {"weyyak", Says::platform, C(Category::streaming)},
    {"osn", Says::platform, C(Category::streaming)},
    {"starz", Says::platform, C(Category::streaming)},
    {"starzplay", Says::platform, C(Category::streaming)},
    {"starz play", Says::platform, C(Category::streaming)},
    {"vix", Says::platform, C(Category::streaming)},
    {"exxen", Says::platform, C(Category::streaming)},
    {"gain", Says::platform, C(Category::streaming)},
    {"blutv", Says::platform, C(Category::streaming)},
    {"tabii", Says::platform, C(Category::streaming)},
    {"vidio", Says::platform, C(Category::streaming)},
    {"vision+", Says::platform, C(Category::streaming)},
    {"stan", Says::platform, C(Category::streaming)},
    {"9now", Says::platform, C(Category::streaming)},
    {"binge", Says::platform, C(Category::streaming)},
    {"showtime", Says::platform, C(Category::streaming)},
    {"sky showtime", Says::platform, C(Category::streaming)},
    {"skyshowtime", Says::platform, C(Category::streaming)},
    {"ruutu+", Says::platform, C(Category::streaming)},
    {"vtmgo", Says::platform, C(Category::streaming)},
    {"streamz", Says::platform, C(Category::streaming)},
    {"nl ziet", Says::platform, C(Category::streaming)},
    {"itv x", Says::platform, C(Category::streaming)},
    {"itvx", Says::platform, C(Category::streaming)},
    {"bbci", Says::platform, C(Category::streaming)},
    {"bbciplayer", Says::platform, C(Category::streaming)},
    {"iplayer", Says::platform, C(Category::streaming)},
    {"skymix", Says::platform, C(Category::streaming)},
    {"magenta", Says::platform, C(Category::streaming)},
    {"vodafone", Says::platform, C(Category::streaming)},
    {"telenet", Says::platform, C(Category::streaming)},
    {"telesat", Says::platform, C(Category::streaming)},
    {"skylink", Says::platform, C(Category::streaming)},
    {"tele2", Says::platform, C(Category::streaming)},
    {"t2", Says::platform, C(Category::streaming)},
    {"caiway", Says::platform, C(Category::streaming)},
    {"avatel", Says::platform, C(Category::streaming)},
    {"tdt", Says::platform, C(Category::streaming)},
    {"cabel tv", Says::platform, C(Category::streaming)},
    {"telefonica", Says::platform, C(Category::streaming)},
    {"spectrum", Says::platform, C(Category::streaming)},
    {"directv", Says::platform, C(Category::streaming)},
    {"direct tv", Says::platform, C(Category::streaming)},
    {"directvwebos", Says::platform, C(Category::streaming)},
    {"luxplay", Says::platform, C(Category::streaming)},
    {"premium play", Says::platform, C(Category::streaming)},
    {"direct one", Says::platform, C(Category::streaming)},
    {"cosmote", Says::platform, C(Category::streaming)},
    {"nova", Says::platform, C(Category::streaming)},
    {"boxes", Says::platform, C(Category::streaming)},
    {"mediathek", Says::platform, C(Category::streaming)},
    {"hotstar", Says::platform, C(Category::streaming)},
    {"zee5", Says::platform, C(Category::streaming)},
    {"sonyliv", Says::platform, C(Category::streaming)},
    {"jio", Says::platform, C(Category::streaming)},
    {"mbc", Says::platform, C(Category::streaming)},
    {"rotana", Says::platform, C(Category::streaming)},
    {"shoof", Says::platform, C(Category::streaming)},
    {"thmanyah", Says::platform, C(Category::streaming)},
    {"anghami", Says::platform, C(Category::music)},
    {"stingray", Says::platform, C(Category::music)},
    {"redbox", Says::platform, C(Category::streaming)},
    // sports services
    {"dazn", Says::platform, C(Category::sports)},
    {"espn", Says::platform, C(Category::sports)},
    {"espn+", Says::platform, C(Category::sports)},
    {"espn plus", Says::platform, C(Category::sports)},
    {"eurosport", Says::platform, C(Category::sports)},
    {"eurosports", Says::platform, C(Category::sports)},
    {"sky sport", Says::platform, C(Category::sports)},
    {"sky sports", Says::platform, C(Category::sports)},
    {"sky sport+", Says::platform, C(Category::sports)},
    {"tnt sports", Says::platform, C(Category::sports)},
    {"bt sport", Says::platform, C(Category::sports)},
    {"tsn", Says::platform, C(Category::sports)},
    {"tsn+", Says::platform, C(Category::sports)},
    {"sportsnet", Says::platform, C(Category::sports)},
    {"sn+", Says::platform, C(Category::sports)},
    {"bein", Says::platform, C(Category::sports)},
    {"bein sports", Says::platform, C(Category::sports)},
    {"supersport", Says::platform, C(Category::sports)},
    {"kayo", Says::platform, C(Category::sports)},
    {"optus", Says::platform, C(Category::sports)},
    {"sport klub", Says::platform, C(Category::sports)},
    {"fox sports", Says::platform, C(Category::sports)},
    {"fubo", Says::platform, C(Category::sports)},
    {"setanta", Says::platform, C(Category::sports)},
    {"premier sports", Says::platform, C(Category::sports)},
    {"polsat sport", Says::platform, C(Category::sports)},
    {"alkass", Says::platform, C(Category::sports)},
    {"al kass", Says::platform, C(Category::sports)},
    {"ad sports", Says::platform, C(Category::sports)},
    {"fite", Says::platform, C(Category::sports)},
    {"fite tv", Says::platform, C(Category::sports)},
    {"flo", Says::platform, C(Category::sports)},
    {"b1g+", Says::platform, C(Category::sports)},
    {"btn+", Says::platform, C(Category::sports)},
    {"fanatiz", Says::platform, C(Category::sports)},
    {"mola", Says::platform, C(Category::sports)},
    {"monomax", Says::platform, C(Category::sports)},
    {"coupang", Says::platform, C(Category::sports)},
    {"star hub", Says::platform, C(Category::sports)},
    {"astro", Says::platform, C(Category::sports)},
    {"clipmyhorse", Says::platform, C(Category::sports)},
    {"rikstoto", Says::platform, C(Category::sports)},
    {"atg", Says::platform, C(Category::sports)},
    {"victory+", Says::platform, C(Category::sports)},
    {"dyn", Says::platform, C(Category::sports)},
    {"mysport", Says::platform, C(Category::sports)},
    {"mysports", Says::platform, C(Category::sports)},
    {"staige", Says::platform, C(Category::sports)},
    {"myteam", Says::platform, C(Category::sports)},
    {"shasha", Says::platform, C(Category::sports)},
    {"solo sport", Says::platform, C(Category::sports)},
    {"gobox", Says::platform, C(Category::sports)},
    {"matchroom", Says::platform, C(Category::sports)},
    {"dirtvision", Says::platform, C(Category::sports)},
    {"onboards", Says::platform, C(Category::sports)},
    // ---- genres ----
    {"action", Says::genre, kGenreAction},
    {"akcja", Says::genre, kGenreAction},
    {"accion", Says::genre, kGenreAction},
    {"azione", Says::genre, kGenreAction},
    {"aksiyon", Says::genre, kGenreAction},
    {"akcija", Says::genre, kGenreAction},
    {"اكشن", Says::genre, kGenreAction},
    {"adventure", Says::genre, kGenreAdventure},
    {"aventure", Says::genre, kGenreAdventure},
    {"aventura", Says::genre, kGenreAdventure},
    {"avventura", Says::genre, kGenreAdventure},
    {"abenteuer", Says::genre, kGenreAdventure},
    {"przygodowe", Says::genre, kGenreAdventure},
    {"animation", Says::genre, kGenreAnimation},
    {"animacion", Says::genre, kGenreAnimation},
    {"animazione", Says::genre, kGenreAnimation},
    {"animacja", Says::genre, kGenreAnimation},
    {"animations", Says::genre, kGenreAnimation},
    {"animatie", Says::genre, kGenreAnimation},
    {"pixar", Says::genre, kGenreAnimation},
    {"anime", Says::genre, kGenreAnime},
    {"manga", Says::genre, kGenreAnime},
    {"مانجا", Says::genre, kGenreAnime},
    {"comedy", Says::genre, kGenreComedy},
    {"comedie", Says::genre, kGenreComedy},
    {"comedia", Says::genre, kGenreComedy},
    {"commedia", Says::genre, kGenreComedy},
    {"komedie", Says::genre, kGenreComedy},
    {"komodie", Says::genre, kGenreComedy},
    {"komedia", Says::genre, kGenreComedy},
    {"komedija", Says::genre, kGenreComedy},
    {"komedi", Says::genre, kGenreComedy},
    {"كوميدي", Says::genre, kGenreComedy},
    {"كوميديه", Says::genre, kGenreComedy},
    {"crime", Says::genre, kGenreCrime},
    {"crimen", Says::genre, kGenreCrime},
    {"krimi", Says::genre, kGenreCrime},
    {"kryminal", Says::genre, kGenreCrime},
    {"policier", Says::genre, kGenreCrime},
    {"misdaad", Says::genre, kGenreCrime},
    {"mafia", Says::genre, kGenreCrime},
    {"gangster", Says::genre, kGenreCrime},
    {"gangsterskie", Says::genre, kGenreCrime},
    {"drama", Says::genre, kGenreDrama},
    {"drame", Says::genre, kGenreDrama},
    {"dramat", Says::genre, kGenreDrama},
    {"dramma", Says::genre, kGenreDrama},
    {"drammatico", Says::genre, kGenreDrama},
    {"دراما", Says::genre, kGenreDrama},
    {"family", Says::genre, kGenreFamily},
    {"famille", Says::genre, kGenreFamily},
    {"familia", Says::genre, kGenreFamily},
    {"familie", Says::genre, kGenreFamily},
    {"familijne", Says::genre, kGenreFamily},
    {"fantasy", Says::genre, kGenreFantasy},
    {"fantastique", Says::genre, kGenreFantasy},
    {"fantastico", Says::genre, kGenreFantasy},
    {"fantasia", Says::genre, kGenreFantasy},
    {"fantasty", Says::genre, kGenreFantasy},
    {"history", Says::genre, kGenreHistory},
    {"histoire", Says::genre, kGenreHistory},
    {"historia", Says::genre, kGenreHistory},
    {"historyczne", Says::genre, kGenreHistory},
    {"hechosreales", Says::genre, kGenreHistory},
    {"horror", Says::genre, kGenreHorror},
    {"horreur", Says::genre, kGenreHorror},
    {"epouvante", Says::genre, kGenreHorror},
    {"terror", Says::genre, kGenreHorror},
    {"korku", Says::genre, kGenreHorror},
    {"رعب", Says::genre, kGenreHorror},
    {"musical", Says::genre, kGenreMusical},
    {"broadway", Says::genre, kGenreMusical},
    {"mystery", Says::genre, kGenreMystery},
    {"mystere", Says::genre, kGenreMystery},
    {"misterio", Says::genre, kGenreMystery},
    {"romance", Says::genre, kGenreRomance},
    {"romantique", Says::genre, kGenreRomance},
    {"romantico", Says::genre, kGenreRomance},
    {"romatico", Says::genre, kGenreRomance},
    {"romans", Says::genre, kGenreRomance},
    {"milosc", Says::genre, kGenreRomance},
    {"رومانسي", Says::genre, kGenreRomance},
    {"sci fi", Says::genre, kGenreSciFi},
    {"scifi", Says::genre, kGenreSciFi},
    {"sf", Says::genre, kGenreSciFi},
    {"science fiction", Says::genre, kGenreSciFi},
    {"fantascienza", Says::genre, kGenreSciFi},
    {"ciencia ficcion", Says::genre, kGenreSciFi},
    {"katastroficzne", Says::genre, kGenreSciFi},
    {"superheroes", Says::genre, kGenreSciFi},
    {"marvel", Says::genre, kGenreSciFi},
    {"thriller", Says::genre, kGenreThriller},
    {"thrillers", Says::genre, kGenreThriller},
    {"suspense", Says::genre, kGenreThriller},
    {"suspenso", Says::genre, kGenreThriller},
    {"sensacja", Says::genre, kGenreThriller},
    {"war", Says::genre, kGenreWar},
    {"guerre", Says::genre, kGenreWar},
    {"guerra", Says::genre, kGenreWar},
    {"krieg", Says::genre, kGenreWar},
    {"wojenne", Says::genre, kGenreWar},
    {"belico", Says::genre, kGenreWar},
    {"حرب", Says::genre, kGenreWar},
    {"western", Says::genre, kGenreWestern},
    {"westerns", Says::genre, kGenreWestern},
    {"reality", Says::genre, kGenreReality},
    {"tele realite", Says::genre, kGenreReality},
    {"reality show", Says::genre, kGenreReality},
    {"talk", Says::genre, kGenreTalk},
    {"soap", Says::genre, kGenreSoap},
    {"christmas", Says::genre, kGenreChristmas},
    {"noel", Says::genre, kGenreChristmas},
    {"navidad", Says::genre, kGenreChristmas},
    {"natale", Says::genre, kGenreChristmas},
    {"weihnachts", Says::genre, kGenreChristmas},
    {"kerst", Says::genre, kGenreChristmas},
    {"kerstfilms", Says::genre, kGenreChristmas},
    {"jul", Says::genre, kGenreChristmas},
    {"swiateczne", Says::genre, kGenreChristmas},
    {"swiatecznie", Says::genre, kGenreChristmas},
    {"bozicni", Says::genre, kGenreChristmas},
    {"halloween", Says::genre, kGenreChristmas},
    {"classic", Says::genre, kGenreClassic},
    {"classics", Says::genre, kGenreClassic},
    {"classique", Says::genre, kGenreClassic},
    {"classico", Says::genre, kGenreClassic},
    {"clasico", Says::genre, kGenreClassic},
    {"klassiker", Says::genre, kGenreClassic},
    {"ancienne", Says::genre, kGenreClassic},
    {"antiguas", Says::genre, kGenreClassic},
    {"old", Says::genre, kGenreClassic},
    {"older", Says::genre, kGenreClassic},
    {"زمان", Says::genre, kGenreClassic},
    {"bollywood", Says::genre, kGenreBollywood},
    {"هنديه", Says::genre, kGenreBollywood},
    {"stand up", Says::genre, kGenreStandUp},
    {"standup", Says::genre, kGenreStandUp},
    {"kabaret", Says::genre, kGenreStandUp},
    {"kabarety", Says::genre, kGenreStandUp},
    {"saga", Says::genre, kGenreCollection},
    {"sagas", Says::genre, kGenreCollection},
    {"collection", Says::genre, kGenreCollection},
    {"collections", Says::genre, kGenreCollection},
    {"kollektion", Says::genre, kGenreCollection},
    {"kolekcja", Says::genre, kGenreCollection},
    {"universe", Says::genre, kGenreCollection},
    {"univers", Says::genre, kGenreCollection},
    {"actors", Says::genre, kGenreCollection},
    {"acteurs", Says::genre, kGenreCollection},
    {"directors", Says::genre, kGenreCollection},
    {"filmographie", Says::genre, kGenreCollection},
    {"concert", Says::genre, kGenreConcert},
    {"concerts", Says::genre, kGenreConcert},
    {"konzerte", Says::genre, kGenreConcert},
    {"koncerti", Says::genre, kGenreConcert},
    {"spectacles", Says::genre, kGenreConcert},
    {"theatre", Says::genre, kGenreConcert},
    {"teatroteka", Says::genre, kGenreConcert},
    {"مسرحيات", Says::genre, kGenreConcert},
    {"biography", Says::genre, kGenreBiography},
    {"martial arts", Says::genre, kGenreAction},
    {"art martiaux", Says::genre, kGenreAction},
    {"sztuki walki", Says::genre, kGenreAction},
    // ---- tags ----
    {"vip", Says::tag, kTagVip},
    {"raw", Says::tag, kTagRaw},
    {"gold", Says::tag, kTagGold},
    {"premium", Says::tag, kTagPremium},
    {"exclusive", Says::tag, kTagExclusive},
    {"hevc", Says::tag, kTagHevc},
    {"h265", Says::tag, kTagHevc},
    {"x265", Says::tag, kTagHevc},
    {"hd", Says::tag, kTagHd},
    {"720p", Says::tag, kTagHd},
    {"720", Says::tag, kTagHd},
    {"fhd", Says::tag, kTagFullHd},
    {"1080p", Says::tag, kTagFullHd},
    {"1080", Says::tag, kTagFullHd},
    {"1920p", Says::tag, kTagFullHd},
    {"uhd", Says::tag, kTagUhd},
    {"4k", Says::tag, kTagUhd},
    {"2160p", Says::tag, kTagUhd},
    {"3840p", Says::tag, kTagUhd},
    {"ultra", Says::tag, kTagUhd},
    {"8k", Says::tag, kTag8k},
    {"sd", Says::tag, kTagSd},
    {"576p", Says::tag, kTagSd},
    {"480p", Says::tag, kTagSd},
    {"480", Says::tag, kTagSd},
    {"lq", Says::tag, kTagLowQuality},
    {"low", Says::tag, kTagLowQuality},
    {"60fps", Says::tag, kTag60fps},
    {"50fps", Says::tag, kTag60fps},
    {"dolby", Says::tag, kTagDolby},
    {"dolby audio", Says::tag, kTagDolby},
    {"multi audio", Says::tag, kTagMultiAudio},
    {"multiaudio", Says::tag, kTagMultiAudio},
    {"backup", Says::tag, kTagBackup},
    {"bk", Says::tag, kTagBackup},
    {"+6h", Says::tag, kTagTimeshift},
    {"+1", Says::tag, kTagTimeshift},
    {"+1 channels", Says::tag, kTagTimeshift},
    {"beta", Says::tag, kTagBeta},
    {"not guaranteed", Says::tag, kTagNotGuaranteed},
    {"new", Says::tag, kTagNew},
    {"new released", Says::tag, kTagNew},
    {"latest", Says::tag, kTagNew},
    {"estrenos", Says::tag, kTagNew},
    {"nouveautes", Says::tag, kTagNew},
    {"ostatnio dodane", Says::tag, kTagNew},
    {"aggiunto recenti", Says::tag, kTagNew},
    {"cam", Says::tag, kTagCam},
    {"hdcam", Says::tag, kTagCam},
    {"pre releases", Says::tag, kTagCam},
    {"3d", Says::tag, kTag3d},
    {"hdr", Says::tag, kTagHdr},
    {"dv", Says::tag, kTagHdr},
    {"dolby vision", Says::tag, kTagHdr},
    {"bluray", Says::tag, kTagBluray},
    {"super", Says::tag, kTagPremium},
};

// The first level of a title, or the prefix of a name: "AR", "|EN|", "fr:",
// "MXC|", "HINDI|". The same code is read the same way everywhere.
struct Code
{
    const char *text; // folded
    Place place;
    Language language;
};

constexpr Code kCodes[] = {
    {"uk", Place::united_kingdom, Language::english},
    {"gb", Place::united_kingdom, Language::english},
    {"us", Place::united_states, Language::english},
    {"usa", Place::united_states, Language::english},
    {"ca", Place::canada, Language::none},
    {"can", Place::canada, Language::none},
    {"au", Place::australia, Language::english},
    {"aus", Place::australia, Language::english},
    {"nz", Place::new_zealand, Language::english},
    {"nzl", Place::new_zealand, Language::english},
    {"ie", Place::ireland, Language::english},
    {"ire", Place::ireland, Language::english},
    {"irl", Place::ireland, Language::english},
    {"fr", Place::france, Language::french},
    {"fra", Place::france, Language::french},
    {"qc", Place::quebec, Language::french},
    {"qfr", Place::quebec, Language::french},
    {"de", Place::germany, Language::german},
    {"ger", Place::germany, Language::german},
    {"deu", Place::germany, Language::german},
    {"at", Place::austria, Language::german},
    {"aut", Place::austria, Language::german},
    {"ch", Place::switzerland, Language::none},
    {"it", Place::italy, Language::italian},
    {"ita", Place::italy, Language::italian},
    {"es", Place::spain, Language::spanish},
    {"esp", Place::spain, Language::spanish},
    {"pt", Place::portugal, Language::portuguese},
    {"por", Place::portugal, Language::portuguese},
    {"br", Place::brazil, Language::portuguese},
    {"bra", Place::brazil, Language::portuguese},
    {"nl", Place::netherlands, Language::dutch},
    {"nld", Place::netherlands, Language::dutch},
    {"be", Place::belgium, Language::none},
    {"bel", Place::belgium, Language::none},
    {"lu", Place::luxembourg, Language::none},
    {"pl", Place::poland, Language::polish},
    {"pol", Place::poland, Language::polish},
    {"cz", Place::czechia, Language::czech},
    {"cze", Place::czechia, Language::czech},
    {"hu", Place::hungary, Language::hungarian},
    {"hun", Place::hungary, Language::hungarian},
    {"ro", Place::romania, Language::romanian},
    {"rom", Place::romania, Language::romanian},
    {"rou", Place::romania, Language::romanian},
    {"md", Place::moldova, Language::romanian},
    {"bg", Place::bulgaria, Language::bulgarian},
    {"bul", Place::bulgaria, Language::bulgarian},
    {"gr", Place::greece, Language::greek},
    {"gre", Place::greece, Language::greek},
    {"grc", Place::greece, Language::greek},
    {"cy", Place::cyprus, Language::greek},
    {"mt", Place::malta, Language::maltese},
    {"tr", Place::turkey, Language::turkish},
    {"tur", Place::turkey, Language::turkish},
    {"ru", Place::russia, Language::russian},
    {"rus", Place::russia, Language::russian},
    {"ua", Place::ukraine, Language::ukrainian},
    {"ukr", Place::ukraine, Language::ukrainian},
    {"by", Place::belarus, Language::belarusian},
    {"lt", Place::lithuania, Language::lithuanian},
    {"lv", Place::latvia, Language::latvian},
    {"ee", Place::estonia, Language::estonian},
    {"est", Place::estonia, Language::estonian},
    {"fi", Place::finland, Language::finnish},
    {"fin", Place::finland, Language::finnish},
    {"fl", Place::finland, Language::finnish},
    {"se", Place::sweden, Language::swedish},
    {"swe", Place::sweden, Language::swedish},
    {"no", Place::norway, Language::norwegian},
    {"nor", Place::norway, Language::norwegian},
    {"dk", Place::denmark, Language::danish},
    {"dnk", Place::denmark, Language::danish},
    {"den", Place::denmark, Language::danish},
    {"isl", Place::iceland, Language::icelandic},
    {"sca", Place::nordics, Language::multi},
    {"sc", Place::nordics, Language::multi},
    {"al", Place::albania, Language::albanian},
    {"alb", Place::albania, Language::albanian},
    {"ko", Place::kosovo, Language::albanian},
    {"kos", Place::kosovo, Language::albanian},
    {"xk", Place::kosovo, Language::albanian},
    {"mk", Place::north_macedonia, Language::macedonian},
    {"mkd", Place::north_macedonia, Language::macedonian},
    {"rs", Place::serbia, Language::serbian},
    {"srb", Place::serbia, Language::serbian},
    {"hr", Place::croatia, Language::croatian},
    {"hrv", Place::croatia, Language::croatian},
    {"ba", Place::bosnia, Language::bosnian},
    {"bh", Place::bosnia, Language::bosnian},
    {"bih", Place::bosnia, Language::bosnian},
    {"si", Place::slovenia, Language::slovene},
    {"sl", Place::slovenia, Language::slovene},
    {"svn", Place::slovenia, Language::slovene},
    {"me", Place::montenegro, Language::serbo_croatian},
    {"cg", Place::montenegro, Language::serbo_croatian},
    {"mne", Place::montenegro, Language::serbo_croatian},
    {"mo", Place::montenegro, Language::serbo_croatian},
    {"exyu", Place::balkans, Language::serbo_croatian},
    {"ex", Place::balkans, Language::serbo_croatian},
    {"ge", Place::georgia, Language::georgian},
    {"am", Place::armenia, Language::armenian},
    {"az", Place::azerbaijan, Language::azerbaijani},
    {"kz", Place::kazakhstan, Language::kazakh},
    {"ka", Place::kazakhstan, Language::kazakh},
    {"uz", Place::uzbekistan, Language::uzbek},
    {"tj", Place::tajikistan, Language::none},
    {"ar", Place::arab_world, Language::arabic},
    {"ara", Place::arab_world, Language::arabic},
    {"bee", Place::arab_world, Language::arabic},
    {"eg", Place::egypt, Language::arabic},
    {"egy", Place::egypt, Language::arabic},
    {"dz", Place::algeria, Language::arabic},
    {"tn", Place::tunisia, Language::arabic},
    {"ly", Place::libya, Language::arabic},
    {"iq", Place::iraq, Language::arabic},
    {"sy", Place::syria, Language::arabic},
    {"lb", Place::lebanon, Language::arabic},
    {"jo", Place::jordan, Language::arabic},
    {"kw", Place::kuwait, Language::arabic},
    {"qa", Place::qatar, Language::arabic},
    {"om", Place::oman, Language::arabic},
    {"ye", Place::yemen, Language::arabic},
    {"ae", Place::uae, Language::arabic},
    {"uae", Place::uae, Language::arabic},
    {"sa", Place::saudi_arabia, Language::arabic},
    {"ksa", Place::saudi_arabia, Language::arabic},
    {"ps", Place::palestine, Language::arabic},
    {"ku", Place::kurdistan, Language::kurdish},
    {"kur", Place::kurdistan, Language::kurdish},
    {"ir", Place::iran, Language::persian},
    {"irn", Place::iran, Language::persian},
    {"is", Place::israel, Language::hebrew},
    {"isr", Place::israel, Language::hebrew},
    {"il", Place::israel, Language::hebrew},
    {"af", Place::africa, Language::none},
    {"afr", Place::africa, Language::none},
    {"za", Place::south_africa, Language::none},
    {"rsa", Place::south_africa, Language::none},
    {"ng", Place::nigeria, Language::english},
    {"gh", Place::ghana, Language::english},
    {"ke", Place::kenya, Language::swahili},
    {"et", Place::ethiopia, Language::amharic},
    {"so", Place::somalia, Language::somali},
    {"som", Place::somalia, Language::somali},
    {"in", Place::india, Language::hindi},
    {"ind", Place::india, Language::hindi},
    {"hindi", Place::india, Language::hindi},
    {"sth", Place::india, Language::none},
    {"tm", Place::india, Language::tamil},
    {"tam", Place::india, Language::tamil},
    {"tg", Place::india, Language::telugu},
    {"tl", Place::india, Language::telugu},
    {"ml", Place::india, Language::malayalam},
    {"kn", Place::india, Language::kannada},
    {"pb", Place::india, Language::punjabi},
    {"pn", Place::india, Language::punjabi},
    {"bn", Place::india, Language::bengali},
    {"mr", Place::india, Language::marathi},
    {"ur", Place::pakistan, Language::urdu},
    {"pk", Place::pakistan, Language::urdu},
    {"pak", Place::pakistan, Language::urdu},
    {"bd", Place::bangladesh, Language::bengali},
    {"ban", Place::bangladesh, Language::bengali},
    {"bgd", Place::bangladesh, Language::bengali},
    {"lk", Place::sri_lanka, Language::sinhala},
    {"sri", Place::sri_lanka, Language::sinhala},
    {"np", Place::nepal, Language::nepali},
    {"npl", Place::nepal, Language::nepali},
    {"bt", Place::bhutan, Language::none},
    {"afg", Place::afghanistan, Language::pashto},
    {"afgh", Place::afghanistan, Language::pashto},
    {"asia", Place::south_asia, Language::none},
    {"cn", Place::china, Language::chinese},
    {"chn", Place::china, Language::chinese},
    {"hk", Place::hong_kong, Language::cantonese},
    {"tw", Place::taiwan, Language::chinese},
    {"tai", Place::taiwan, Language::chinese},
    {"jp", Place::japan, Language::japanese},
    {"jpn", Place::japan, Language::japanese},
    {"kr", Place::south_korea, Language::korean},
    {"kor", Place::south_korea, Language::korean},
    {"sk", Place::south_korea, Language::korean},
    {"th", Place::thailand, Language::thai},
    {"tha", Place::thailand, Language::thai},
    {"vn", Place::vietnam, Language::vietnamese},
    {"vt", Place::vietnam, Language::vietnamese},
    {"vie", Place::vietnam, Language::vietnamese},
    {"id", Place::indonesia, Language::indonesian},
    {"idn", Place::indonesia, Language::indonesian},
    {"my", Place::malaysia, Language::malay},
    {"mys", Place::malaysia, Language::malay},
    {"sg", Place::singapore, Language::none},
    {"sgp", Place::singapore, Language::none},
    {"ph", Place::philippines, Language::filipino},
    {"phl", Place::philippines, Language::filipino},
    {"lat", Place::latin_america, Language::spanish},
    {"la", Place::latin_america, Language::spanish},
    {"latam", Place::latin_america, Language::spanish},
    {"mx", Place::mexico, Language::spanish},
    {"mxc", Place::mexico, Language::spanish},
    {"mex", Place::mexico, Language::spanish},
    {"arg", Place::argentina, Language::spanish},
    {"cl", Place::chile, Language::spanish},
    {"co", Place::colombia, Language::spanish},
    {"col", Place::colombia, Language::spanish},
    {"pe", Place::peru, Language::spanish},
    {"ve", Place::venezuela, Language::spanish},
    {"ven", Place::venezuela, Language::spanish},
    {"ec", Place::ecuador, Language::spanish},
    {"uy", Place::uruguay, Language::spanish},
    {"py", Place::paraguay, Language::spanish},
    {"bo", Place::bolivia, Language::spanish},
    {"cr", Place::costa_rica, Language::spanish},
    {"pa", Place::panama, Language::spanish},
    {"gt", Place::guatemala, Language::spanish},
    {"hn", Place::honduras, Language::spanish},
    {"ni", Place::nicaragua, Language::spanish},
    {"sv", Place::el_salvador, Language::spanish},
    {"do", Place::dominican_republic, Language::spanish},
    {"pr", Place::puerto_rico, Language::spanish},
    {"cu", Place::cuba, Language::spanish},
    {"ht", Place::haiti, Language::french},
    {"jm", Place::jamaica, Language::english},
    {"tt", Place::trinidad, Language::english},
    {"sr", Place::suriname, Language::dutch},
    {"su", Place::suriname, Language::dutch},
    {"crb", Place::caribbean, Language::none},
    {"car", Place::caribbean, Language::none},
    {"eu", Place::europe, Language::none},
    {"na", Place::north_america, Language::english},
    {"wal", Place::wales, Language::english},
    {"cymru", Place::wales, Language::english},
    {"sco", Place::scotland, Language::english},
    {"en", Place::none, Language::english},
    {"eng", Place::none, Language::english},
    {"multi", Place::none, Language::multi},
    {"int", Place::world, Language::none},
    {"intl", Place::world, Language::none},
    {"ww", Place::world, Language::none},
};

// ---- folding ----

std::uint32_t next_code(std::string_view text, std::size_t *index)
{
    const auto byte = [&](std::size_t at) { return static_cast<unsigned char>(text[at]); };
    const std::size_t at = *index;
    const unsigned char lead = byte(at);
    const std::size_t length = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if (length == 1 || at + length > text.size())
    {
        ++*index;
        return lead;
    }
    std::uint32_t code = lead & (0xFFu >> (length + 1));
    for (std::size_t i = 1; i < length; ++i)
    {
        const unsigned char next = byte(at + i);
        if ((next & 0xC0) != 0x80)
        {
            ++*index;
            return lead;
        }
        code = (code << 6) | (next & 0x3Fu);
    }
    *index += length;
    return code;
}

void append_code(std::string *out, std::uint32_t code)
{
    if (code < 0x80)
        out->push_back(static_cast<char>(code));
    else if (code < 0x800)
    {
        out->push_back(static_cast<char>(0xC0 | (code >> 6)));
        out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
    else if (code < 0x10000)
    {
        out->push_back(static_cast<char>(0xE0 | (code >> 12)));
        out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
    else
    {
        out->push_back(static_cast<char>(0xF0 | (code >> 18)));
        out->push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

// The small letter a Latin code point is filed under, accents put aside; 0 for none.
char latin_small(std::uint32_t code)
{
    if (code < 0x80)
        return code >= 'A' && code <= 'Z' ? static_cast<char>(code + 32)
               : (code >= 'a' && code <= 'z') || (code >= '0' && code <= '9')
                   ? static_cast<char>(code)
                   : '\0';
    if (code == 0xDF)
        return 's'; // ß
    if (code >= 0xC0 && code <= 0xFF)
    {
        static constexpr char kLatin1[] = "aaaaaaaceeeeiiiidnooooo\0ouuuuyts";
        return code == 0xFF ? 'y' : kLatin1[(code - 0xC0) % 32];
    }
    if (code >= 0x100 && code <= 0x17F)
    {
        static constexpr struct
        {
            std::uint16_t last;
            char base;
        } kRuns[] = {{0x105, 'a'}, {0x10D, 'c'}, {0x111, 'd'}, {0x11B, 'e'}, {0x123, 'g'},
                     {0x127, 'h'}, {0x133, 'i'}, {0x135, 'j'}, {0x138, 'k'}, {0x142, 'l'},
                     {0x14B, 'n'}, {0x153, 'o'}, {0x159, 'r'}, {0x161, 's'}, {0x167, 't'},
                     {0x173, 'u'}, {0x175, 'w'}, {0x178, 'y'}, {0x17E, 'z'}, {0x17F, 's'}};
        for (const auto &run : kRuns)
            if (code <= run.last)
                return run.base;
    }
    return '\0';
}

// Greek without tonos and in small letters, Cyrillic in small letters, Arabic
// with one alef and one ya; 0 when the code point is not one of theirs.
std::uint32_t other_small(std::uint32_t code)
{
    if (code >= 0x391 && code <= 0x3A9)
        return code + 0x20;
    if (code >= 0x3B1 && code <= 0x3C9)
        return code == 0x3C2 ? 0x3C3 : code;
    switch (code)
    {
    case 0x386:
    case 0x3AC:
        return 0x3B1;
    case 0x388:
    case 0x3AD:
        return 0x3B5;
    case 0x389:
    case 0x3AE:
        return 0x3B7;
    case 0x38A:
    case 0x3AF:
    case 0x3CA:
    case 0x390:
        return 0x3B9;
    case 0x38C:
    case 0x3CC:
        return 0x3BF;
    case 0x38E:
    case 0x3CD:
    case 0x3CB:
    case 0x3B0:
        return 0x3C5;
    case 0x38F:
    case 0x3CE:
        return 0x3C9;
    case 0x401:
        return 0x435;
    case 0x622:
    case 0x623:
    case 0x625:
        return 0x627;
    case 0x629:
        return 0x647;
    case 0x649:
    case 0x6CC:
        return 0x64A;
    case 0x6A9:
        return 0x643;
    default:
        break;
    }
    if (code >= 0x410 && code <= 0x42F)
        return code + 0x20;
    if (code >= 0x400 && code <= 0x40F)
        return code + 0x50;
    if (code >= 0x430 && code <= 0x45F)
        return code;
    if ((code >= 0x620 && code <= 0x64A) || (code >= 0x660 && code <= 0x6FF))
        return code;
    if (code >= 0x5D0 && code <= 0x5EA)
        return code;
    if (code >= 0x900 && code <= 0xDFF) // Indic scripts
        return code;
    if (code >= 0xE00 && code <= 0xE7F) // Thai
        return code;
    if (code >= 0x3040 && code <= 0x9FFF) // Kana, CJK
        return code;
    if (code >= 0xAC00 && code <= 0xD7A3) // Hangul
        return code;
    return 0;
}

struct Token
{
    std::string text;
    bool in_parens = false; // "(ZA)", "[HD]": set beside the words
};

// The words of a provider's text: small letters without accents, every sign
// and picture left out. "24/7" is one word, "24x7".
std::vector<Token> fold_tokens(std::string_view text)
{
    const std::string plain = plain_text(text);
    std::vector<Token> tokens;
    std::string word;
    int depth = 0;
    bool flag = false;
    const auto flush = [&]
    {
        if (!word.empty())
            tokens.push_back({word, flag});
        word.clear();
    };
    const auto add = [&](auto put)
    {
        if (word.empty())
            flag = depth > 0;
        put();
    };
    for (std::size_t index = 0; index < plain.size();)
    {
        const std::size_t start = index;
        const std::uint32_t code = next_code(plain, &index);
        if (code == '(' || code == '[')
        {
            flush();
            ++depth;
            continue;
        }
        if (code == ')' || code == ']')
        {
            flush();
            depth = depth > 0 ? depth - 1 : 0;
            continue;
        }
        // "24/7", "24-7", "24 / 7", "24x7"
        if (code == '2' && start + 2 < plain.size() && plain[start + 1] == '4' &&
            (start == 0 || !std::isalnum(static_cast<unsigned char>(plain[start - 1]))))
        {
            std::size_t at = start + 2;
            while (at < plain.size() && plain[at] == ' ')
                ++at;
            if (at < plain.size() &&
                (plain[at] == '/' || plain[at] == '-' || plain[at] == 'x' || plain[at] == 'X'))
            {
                ++at;
                while (at < plain.size() && plain[at] == ' ')
                    ++at;
                if (at < plain.size() && plain[at] == '7' &&
                    (at + 1 == plain.size() ||
                     !std::isalnum(static_cast<unsigned char>(plain[at + 1]))))
                {
                    flush();
                    tokens.push_back({"24x7", depth > 0});
                    index = at + 1;
                    continue;
                }
            }
        }
        if (const char small = latin_small(code); small != '\0')
        {
            add([&] { word.push_back(small); });
            continue;
        }
        if (code == '+')
        {
            add([&] { word.push_back('+'); });
            continue;
        }
        if (code == '\'' || code == 0x2019 || code == 0x60) // "children's", "L'EQUIPE"
            continue;
        if (code < 0x80)
        {
            flush();
            continue;
        }
        if (code >= 0x64B && code <= 0x652) // Arabic vowel marks
            continue;
        if (code == 0x640 || code == 0xFE0F || code == 0x200B || code == 0x200C || code == 0x200D)
            continue;
        if (const std::uint32_t small = other_small(code); small != 0)
        {
            add([&] { append_code(&word, small); });
            continue;
        }
        flush(); // a sign, a picture, or a letter of a script that is not read here
    }
    flush();
    return tokens;
}

std::string fold(std::string_view text)
{
    std::string out;
    for (const Token &token : fold_tokens(text))
    {
        if (!out.empty())
            out.push_back(' ');
        out += token.text;
    }
    return out;
}

std::vector<Token> tokens_of(std::string_view level)
{
    return fold_tokens(level);
}

const std::unordered_map<std::string_view, const Word *> &words()
{
    static const std::unordered_map<std::string_view, const Word *> table = []
    {
        std::unordered_map<std::string_view, const Word *> map;
        static std::vector<std::string> folded;
        folded.reserve(std::size(kWords));
        map.reserve(std::size(kWords));
        for (const Word &word : kWords)
        {
            folded.push_back(fold(word.text));
            map.emplace(folded.back(), &word);
        }
        return map;
    }();
    return table;
}

const std::unordered_map<std::string_view, const Code *> &codes()
{
    static const std::unordered_map<std::string_view, const Code *> table = []
    {
        std::unordered_map<std::string_view, const Code *> map;
        map.reserve(std::size(kCodes));
        for (const Code &code : kCodes)
            map.emplace(code.text, &code);
        return map;
    }();
    return table;
}

const Code *code_of(std::string_view folded)
{
    const auto &table = codes();
    const auto found = table.find(folded);
    return found == table.end() ? nullptr : found->second;
}

const Word *word_of(std::string_view folded)
{
    const auto &table = words();
    const auto found = table.find(folded);
    return found == table.end() ? nullptr : found->second;
}

bool is_country(Place place)
{
    return place >= Place::afghanistan;
}

// "|EN|", " AR ", "ar": the code a level is, or nullptr.
const Code *level_code(std::string_view level)
{
    std::string folded = fold(level);
    if (folded.empty() || folded.find(' ') != std::string::npos)
        return nullptr;
    return code_of(folded);
}

// The levels of a title, left to right: "AR| BEIN SPORTS" is {"AR", "BEIN
// SPORTS"}; "|EN| 4K MOVIES" is {"EN", "4K MOVIES"}; "⚽️EPL - FUBO SPORTS" is
// {"⚽️EPL", "FUBO SPORTS"}.
std::vector<std::string_view> levels_of(std::string_view title)
{
    std::vector<std::string_view> levels;
    title = category_trim(title);
    while (!title.empty() && title.front() == '|')
        title = category_trim(title.substr(1));
    std::size_t start = 0;
    for (std::size_t at = 0; at < title.size(); ++at)
    {
        const char c = title[at];
        std::size_t width = 0;
        if (c == '|')
            width = 1;
        else if (c == ':' && at + 1 < title.size() && title[at + 1] == ':')
            width = 2;
        else if (c == '/' && at > 0 && at + 1 < title.size() && title[at - 1] == ' ' &&
                 title[at + 1] == ' ')
            width = 1;
        else if (c == '-' && at > 0 && at + 1 < title.size() && title[at - 1] == ' ' &&
                 title[at + 1] == ' ')
            width = 1;
        else if (c == '\xE2' && at + 2 < title.size() && title[at + 1] == '\x97' &&
                 title[at + 2] == '\x88') // ◈
            width = 3;
        if (width == 0)
            continue;
        const std::string_view level = category_trim(title.substr(start, at - start));
        if (!level.empty())
            levels.push_back(level);
        at += width - 1;
        start = at + 1;
    }
    const std::string_view last = category_trim(title.substr(start));
    if (!last.empty())
        levels.push_back(last);
    return levels;
}

bool is_year(std::string_view word)
{
    return word.size() == 4 && (word[0] == '1' || word[0] == '2') &&
           std::all_of(word.begin(), word.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// What a run of words says, gathered.
struct Gathered
{
    Place country = Place::none;
    Place region = Place::none;
    Place paren_country = Place::none;
    unsigned paren_countries = 0;
    Language language = Language::none;       // the first language word
    Language other_language = Language::none; // the second
    Language subtitles = Language::none;
    bool subtitled = false;
    bool dubbed = false;
    bool dubbed_here = false;
    Language dubbed_in = Language::none;
    Category category = Category::none;
    std::uint32_t seen = 0; // every category word, as bits
    Category platform = Category::none;
    std::uint32_t genres = 0;
    std::uint32_t tags = 0;
    std::vector<std::string> unknown;
};

void gather(const std::vector<Token> &tokens, Gathered *out)
{
    const auto note_language = [&](Language language)
    {
        if (out->language == Language::none)
            out->language = language;
        else if (out->other_language == Language::none && language != out->language)
            out->other_language = language;
    };
    for (std::size_t at = 0; at < tokens.size();)
    {
        const Word *word = nullptr;
        std::size_t used = 1;
        for (std::size_t length = std::min<std::size_t>(3, tokens.size() - at);
             length >= 1 && !word; --length)
        {
            std::string joined;
            for (std::size_t i = 0; i < length; ++i)
            {
                if (i)
                    joined.push_back(' ');
                joined += tokens[at + i].text;
            }
            word = word_of(joined);
            if (!word && length == 1 && joined.size() > 1 && joined.back() == '+')
                word = word_of(std::string_view(joined).substr(0, joined.size() - 1));
            if (word)
                used = length;
        }
        const Token &token = tokens[at];
        if (!word)
        {
            if (is_year(token.text))
                out->tags |= kTagYear;
            else if (token.in_parens)
            {
                // "(ZA)", "(IE)": a country beside the words.
                if (const Code *code = code_of(token.text); code && is_country(code->place))
                {
                    out->paren_country = code->place;
                    ++out->paren_countries;
                }
            }
            else if (token.text.size() > 1)
                out->unknown.push_back(token.text);
            ++at;
            continue;
        }
        const Language before = at > 0 ? [&]
        {
            const Word *previous = word_of(tokens[at - 1].text);
            return previous && previous->says == Says::language
                       ? static_cast<Language>(previous->value)
                       : Language::none;
        }()
                                       : Language::none;
        switch (word->says)
        {
        case Says::place:
        {
            const auto place = static_cast<Place>(word->value);
            if (is_country(place))
            {
                if (out->country == Place::none)
                    out->country = place;
            }
            else if (out->region == Place::none)
                out->region = place;
            break;
        }
        case Says::language:
            note_language(static_cast<Language>(word->value));
            break;
        case Says::category:
        {
            const auto category = static_cast<Category>(word->value);
            out->seen |= 1u << word->value;
            if (category_rank(category) > category_rank(out->category))
                out->category = category;
            if (category == Category::events)
                out->tags |= kTagPpv;
            break;
        }
        case Says::genre:
            out->genres |= word->value;
            break;
        case Says::tag:
            out->tags |= word->value;
            break;
        case Says::platform:
            if (out->platform == Category::none || word->value == C(Category::sports))
                out->platform = static_cast<Category>(word->value);
            break;
        case Says::subtitled:
            out->subtitled = true;
            out->tags |= kTagSubtitled;
            if (word->value != L(Language::none))
                out->subtitles = static_cast<Language>(word->value);
            else if (before != Language::none)
                out->subtitles = before;
            break;
        case Says::dubbed:
            out->dubbed = true;
            out->tags |= kTagDubbed;
            if (word->value != L(Language::none))
                out->dubbed_in = static_cast<Language>(word->value);
            break;
        case Says::dubbed_here:
            out->dubbed = true;
            out->dubbed_here = true;
            out->tags |= kTagDubbed;
            break;
        }
        at += used;
    }
}

GroupFacts resolve(const Code *code, const Gathered &got, iptv::MediaKind kind)
{
    GroupFacts facts;
    facts.coded = code != nullptr;
    // The place: a country in the words, then the code, then a region in the words.
    if (got.country != Place::none)
        facts.place = got.country;
    else if (got.paren_country != Place::none && got.paren_countries == 1)
        facts.place = got.paren_country;
    else if (code && code->place != Place::none)
        facts.place = code->place;
    else if (got.region != Place::none)
        facts.place = got.region;
    // The language of the place, unless the words say otherwise.
    const Language code_language = code ? code->language : Language::none;
    Language home = code_language != Language::none ? code_language : place_language(facts.place);
    if (got.country != Place::none && got.country != (code ? code->place : Place::none))
        home = place_language(got.country) != Language::none ? place_language(got.country) : home;
    if (got.language != Language::none && !got.subtitled && !got.dubbed)
        home = got.language;
    facts.audio = home;
    if (got.subtitled)
    {
        // "GREECE MOVIES SUB": the sound is as made, the subtitles are Greek.
        // "TURKISH زیرنویس": Turkish sound, Persian subtitles.
        facts.subtitles = got.subtitles != Language::none ? got.subtitles : home;
        Language spoken = Language::none;
        if (got.language != Language::none && got.language != facts.subtitles)
            spoken = got.language;
        else if (got.other_language != Language::none && got.other_language != facts.subtitles)
            spoken = got.other_language;
        facts.audio = spoken != Language::none ? spoken : Language::original;
        if (got.dubbed)
            facts.audio = got.dubbed_in != Language::none ? got.dubbed_in : home;
    }
    else if (got.dubbed)
    {
        // "HINDI DUBBED": Hindi sound. "DUBLAJ", "مدبلج": the group's own language.
        facts.audio = got.dubbed_here                   ? home
                      : got.dubbed_in != Language::none ? got.dubbed_in
                      : got.language != Language::none  ? got.language
                                                        : home;
    }
    facts.category = got.category;
    if (facts.category == Category::none && got.platform != Category::none)
        facts.category = got.platform;
    else if (got.platform == Category::sports && facts.category == Category::events)
        facts.category = Category::sports; // "DAZN PPV": sport, on pay-per-view
    else if (got.platform == Category::sports &&
             category_rank(facts.category) < category_rank(Category::sports))
        facts.category = Category::sports;
    if (facts.category == Category::none && (got.tags & (kTagPpv | kTagEventsOnly)) != 0)
        facts.category = Category::events;
    // "FR| FRANCE HD", "LAT| MEXICO VIP", "Germany": a place and nothing
    // else is that place's channels in general.
    if (facts.category == Category::none && kind == iptv::MediaKind::live &&
        facts.place != Place::none && got.unknown.empty() && got.genres == 0)
        facts.category = Category::general;
    // A film or a series group that names no kind is films or series.
    if (kind == iptv::MediaKind::movie &&
        (facts.category == Category::none || facts.category == Category::general ||
         facts.category == Category::streaming))
        facts.category = Category::movies;
    if (kind == iptv::MediaKind::episode &&
        (facts.category == Category::none || facts.category == Category::general ||
         facts.category == Category::streaming))
        facts.category = Category::series;
    facts.genres = got.genres;
    facts.tags = got.tags;
    return facts;
}

GroupFacts parse_group_words(std::string_view title, iptv::MediaKind kind, Gathered *gathered)
{
    const std::vector<std::string_view> levels = levels_of(title);
    GroupFacts none;
    if (levels.empty())
        return none;
    const Code *code = level_code(levels[0]);
    Gathered got;
    for (std::size_t at = code ? 1 : 0; at < levels.size(); ++at)
        gather(tokens_of(levels[at]), &got);
    GroupFacts facts = resolve(code, got, kind);
    facts.levels = static_cast<std::uint8_t>(std::min<std::size_t>(levels.size(), 255));
    if (gathered)
        *gathered = std::move(got);
    return facts;
}

bool starts_with(std::string_view text, std::string_view head)
{
    return text.size() >= head.size() && text.substr(0, head.size()) == head;
}

// The head of a name before "|", ":", "◈" or " - ": "FR", "fr", "IND-Hindi",
// "US HD", "[Kayo]". `bytes` is where the name goes on after the separator.
struct Head
{
    std::string_view text;
    std::size_t bytes = 0;
    bool bracket = false;
};

Head head_of(std::string_view name)
{
    Head head;
    std::size_t at = 0;
    while (at < name.size() && name[at] == ' ')
        ++at;
    if (at < name.size() && name[at] == '[')
    {
        const std::size_t close = name.find(']', at);
        if (close == std::string_view::npos || close - at > 28)
            return head;
        head.text = name.substr(at + 1, close - at - 1);
        head.bracket = true;
        at = close + 1;
        while (at < name.size() && name[at] == ' ')
            ++at;
        head.bytes = at;
        return head;
    }
    const std::size_t limit = std::min<std::size_t>(name.size(), at + 20);
    for (std::size_t i = at; i < limit; ++i)
    {
        const char c = name[i];
        std::size_t width = 0;
        if (c == '|' || c == ':')
            width = 1;
        else if (c == '-' && i > at && name[i - 1] == ' ' && i + 1 < name.size() &&
                 name[i + 1] == ' ')
            width = 1;
        else if (c == '\xE2' && i + 2 < name.size() && name[i + 1] == '\x97' &&
                 name[i + 2] == '\x88')
            width = 3;
        if (width == 0)
            continue;
        std::string_view text = name.substr(at, i - at);
        while (!text.empty() && text.back() == ' ')
            text.remove_suffix(1);
        if (text.empty())
            return head;
        std::size_t rest = i + width;
        while (rest < name.size() && name[rest] == ' ')
            ++rest;
        head.text = text;
        head.bytes = rest;
        return head;
    }
    return head;
}

// The code a head stands for: "FR", "fr", "IND-Hindi", "US HD", "SC-4K",
// "4K-DE", "BD-loc". A platform's own label ("PRIME", "NF") counts with one
// word only, as does the group's own first level.
const Code *head_code(std::string_view head_text, std::string_view group_first, bool *known)
{
    *known = false;
    const std::string folded = fold(head_text);
    if (folded.empty() || folded.size() > 16)
        return nullptr;
    std::vector<std::string_view> parts;
    for (std::size_t at = 0; at <= folded.size();)
    {
        const std::size_t end = std::min(folded.find(' ', at), folded.size());
        if (end > at)
            parts.push_back(std::string_view(folded).substr(at, end - at));
        at = end + 1;
    }
    if (parts.empty() || parts.size() > 2)
        return nullptr;
    // A word of digits is not a code: "PPV 01", "NFL 1".
    for (const std::string_view part : parts)
        if (std::all_of(part.begin(), part.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return nullptr;
    if (const Code *code = code_of(parts[0]))
    {
        *known = true;
        return code;
    }
    // "4K-DE", "SC-4K": the code can follow a mark, joined by a hyphen.
    if (parts.size() == 2 && head_text.find('-') != std::string_view::npos &&
        head_text.find(' ') == std::string_view::npos)
        if (const Code *code = code_of(parts[1]))
        {
            *known = true;
            return code;
        }
    if (parts.size() == 1)
        if (const Word *word = word_of(parts[0]); word && word->says == Says::platform)
        {
            *known = true;
            return nullptr;
        }
    if (!group_first.empty() && fold(group_first) == folded)
        *known = true;
    return nullptr;
}

} // namespace

const char *place_name(Place place)
{
    const auto index = static_cast<std::size_t>(place);
    return index < std::size(kPlaces) ? kPlaces[index].name : "";
}

const char *language_name(Language language)
{
    const auto index = static_cast<std::size_t>(language);
    return index < std::size(kLanguages) ? kLanguages[index] : "";
}

const char *category_name(Category category)
{
    const auto index = static_cast<std::size_t>(category);
    return index < std::size(kCategories) ? kCategories[index] : "";
}

const char *genre_name(unsigned bit)
{
    return bit < kGenreCount ? kGenres[bit] : "";
}

Language place_language(Place place)
{
    const auto index = static_cast<std::size_t>(place);
    return index < std::size(kPlaces) ? kPlaces[index].language : Language::none;
}

GroupFacts parse_group(std::string_view title, iptv::MediaKind kind)
{
    return parse_group_words(title, kind, nullptr);
}

// The kind of row a name is, from its first words.
RowKind row_of(std::string_view name)
{
    std::string_view rest = name;
    while (!rest.empty() && rest.front() == ' ')
        rest.remove_prefix(1);
    if (starts_with(rest, "##"))
        return RowKind::heading;
    // "NEXT |", "Next |", "ENDED |", "NO EVENT": the first word, then a bar.
    const unsigned char first = rest.empty() ? 0 : static_cast<unsigned char>(rest.front());
    if (first != 'n' && first != 'N' && first != 'e' && first != 'E')
        return RowKind::channel;
    const std::string start = fold(rest.substr(0, std::min<std::size_t>(rest.size(), 24)));
    if (starts_with(start, "next ") || start == "next")
    {
        const Head head = head_of(rest);
        if (fold(head.text) == "next")
            return RowKind::event_next;
    }
    else if (starts_with(start, "ended ") || start == "ended")
    {
        const Head head = head_of(rest);
        if (fold(head.text) == "ended")
            return RowKind::event_ended;
    }
    else if (starts_with(start, "no event"))
        return RowKind::no_event;
    return RowKind::channel;
}

// The prefix of a name, and what it says; the rest of the name is left alone.
void read_prefix(std::string_view name, std::string_view group_title, NameFacts *facts)
{
    const std::vector<std::string_view> levels = levels_of(group_title);
    const std::string_view group_first = levels.empty() ? std::string_view() : levels[0];
    std::size_t bytes = 0;
    for (int round = 0; round < 3; ++round)
    {
        const Head head = head_of(name.substr(bytes));
        if (head.bytes == 0)
            break;
        bool known = false;
        const Code *code = head_code(head.text, group_first, &known);
        if (!known && !head.bracket)
            break;
        if (code)
        {
            if (facts->place == Place::none)
                facts->place = code->place;
            if (facts->audio == Language::none)
                facts->audio = code->language;
        }
        else if (head.bracket)
        {
            // "[Radio][ES]": a note, then a language.
            Gathered got;
            gather(tokens_of(head.text), &got);
            if (facts->audio == Language::none && got.language != Language::none)
                facts->audio = got.language;
        }
        bytes += head.bytes;
        if (bytes + 1 >= name.size())
        {
            bytes = 0; // a name that is nothing but its prefix keeps it
            break;
        }
    }
    facts->prefix_bytes = static_cast<std::uint8_t>(std::min<std::size_t>(bytes, 255));
}

NameFacts parse_name(std::string_view name, std::string_view group_title)
{
    NameFacts facts;
    facts.row = row_of(name);
    if (facts.row != RowKind::channel)
        return facts;
    read_prefix(name, group_title, &facts);
    // Marks in the name: the size, the codec, RAW, a backup, a time shift.
    Gathered got;
    gather(tokens_of(name.substr(facts.prefix_bytes)), &got);
    facts.tags |= got.tags;
    return facts;
}

unsigned name_prefix_bytes(std::string_view name, std::string_view group_title)
{
    // A name is not read for its marks here: the lists ask this of every row.
    std::string_view rest = name;
    while (!rest.empty() && rest.front() == ' ')
        rest.remove_prefix(1);
    if (starts_with(rest, "##"))
        return 0;
    NameFacts facts;
    read_prefix(name, group_title, &facts);
    return facts.prefix_bytes;
}

std::string category_level_label(std::string_view level)
{
    if (const Code *code = level_code(level))
    {
        if (code->place != Place::none)
            return tr(place_name(code->place));
        if (code->language != Language::none)
            return tr(language_name(code->language));
    }
    return label_text(level);
}

std::string category_path_label(std::string_view path)
{
    std::string label;
    for (const std::string_view level : levels_of(path))
    {
        if (!label.empty())
            label += " / ";
        label += category_level_label(level);
    }
    return label.empty() ? label_text(path) : label;
}

bool category_words_say(std::string_view category, bool adult)
{
    Gathered got;
    for (const std::string_view level : levels_of(category))
        gather(tokens_of(level), &got);
    return (got.seen & (1u << C(adult ? Category::adult : Category::kids))) != 0;
}

// For the tests: the words of a title that matched nothing.
std::vector<std::string> unknown_words(std::string_view title, iptv::MediaKind kind)
{
    Gathered got;
    parse_group_words(title, kind, &got);
    return got.unknown;
}

} // namespace ptv
