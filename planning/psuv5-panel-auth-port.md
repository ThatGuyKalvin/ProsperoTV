# Plan: Port PSUV5 panel authentication (Allvue / Maximus) to ProsperoTV

## Status (2026-10-05)
Implementation steps 1–7 are committed. Nothing has run on a console yet: Step 0 and on-device Verification steps 3–5 are still open.

| Step | Commit | Checked by |
|---|---|---|
| Land existing WIP | `753cbe4` | `make test-unit` (89 pass) |
| 1. Extract JSON helpers | `4635c27` | Unit tests unchanged and passing |
| 2. HTTP POST | `86ec59a` | Host tests for argument checks; console path compiled with `-D__PROSPERO__ -Werror` |
| 3. OneStream module | `ce2e6f3` | 15 new host tests |
| 4. Source descriptor refactor | `5f9fa26` | Compile check; integration tests match baseline |
| 5–6. App wiring, movies, series, episodes | `5135155` | Compile check; `test_tools.py` updated; `opengl-ui` files compiled with `-Werror` |
| 7. Docs | `99f2eeb` | — |

### Where the code differs from this plan
- **`iptv_json.h` is header-only**, not `.h` plus `.cpp`. `opengl-ui/tools/run-tests.sh` and `host-snapshots.sh` compile a fixed list of root sources, so a new `.cpp` would break that build's link.
- **POST must keep the `sce*` call counts in `iptv_http.cpp` unchanged.** `opengl-ui/ps5/patch_tree.py` renames those calls and requires an exact count of each. The `Content-Type` header therefore shares one `sceHttpAddRequestHeader` call site with `Referer`.
- **`OneStreamStatus` is an alias of `XtreamStatus`**, not a new enum. The same goes for categories, library kinds, fetch outcomes and reports. This gives the app one status path for both account sources without a separate neutral type.
- **A series entry's `url` is its tokenless `.../content/series/<id>` address**, not an `onestream-series:` placeholder. It is a real HTTP(S) URL, but the episode worker builds its own URL from `series_id` with a fresh token.
- **The opening of a series signs in again** (auth plus user-info) before downloading episodes. No token is kept between threads.
- **`opengl-ui` needed small changes.** Its `switch` statements over `SourceKind` gained an `OneStream` case so its `-Werror` build still compiles. `is_set_up(OneStream)` returns false there, so that interface never selects the source.
- **`opengl-ui`'s libcurl shim (`ps5/src/tv_http_curl.cpp`) ignores the method and the body**, so `tv_http_send` always sends a GET. OneStream sign-in will not work in that interface until the shim supports POST. This needs fixing before that interface replaces the root build.
- **The host test build doesn't run on macOS as written.** It expects `wget`, `sha256sum` and GNU `ld`. Shims for the first two, plus `HOST_TEST_LDFLAGS=-Wl,-dead_strip` and a separate `HOST_UNIT_TEST` path, were used. Five integration tests fail on macOS both before and after this work (bash 3.2 `${x,,}`, `--gc-sections`).

## Context
PSUV5.apk is a rebranded IPTV Smarters app. Allvue and Maximus are provider panels whose users sign in through it.
Neither name is in the APK, so the work is to port the login flows the APK has, with a panel URL and account entered by the user.
The repo is C++20 (`src/*.cpp`), not C.

PSUV5 has three mutually exclusive provider types (`smali/m7/e$c.smali:93-171`, `FirebaseRegisterDeviceActivity.smali:941-1042`):
- `api`: Xtream `player_api.php`. ProsperoTV already implements this in `src/iptv_xtream.cpp`.
- `m3u`: ProsperoTV already implements this as the custom source.
- `onestream_api`: a token-based REST API under `play/b2c/v1/`. This is the only new work.

The modes never fall back to each other. If Allvue or Maximus turn out to be Xtream panels, typing the panel URL into the existing Xtream editor is enough and no code is needed.

### How to reproduce the smali references
```
apktool d -f -o <scratch>/psuv5 external_apps/PSUV5.apk   # apktool 3.0.3
```
- Line numbers below assume that apktool version.
- `…/activity/` stands for `smali_classes4/com/nst/iptvsmarterstvbox/view/activity/`.
- The decompiled tree is not committed; only the APK is, under `external_apps/`.

## The OneStream API (traced from the APK)

The base URL always ends in `/`. The token always goes in the query string as `token=<T>`; no call uses an `Authorization` header. PSUV5 concatenates the token without URL-encoding it. We will percent-encode it, which is a no-op for normal tokens.

| # | Method | Path relative to the base | Used for | Source |
|---|---|---|---|---|
| A | POST form | `play/b2c/v1/auth` | login | `smali/m7/e.smali:276-306`, `e$a.smali:53-75` |
| B | GET | `play/b2c/v1/user-info?token=T` | account check | `smali/m7/e$b.smali:94-258`, `h$c.smali:72-250` |
| C | GET | `play/b2c/v1/categories/{live\|vod\|series}?token=T` | category names | `…/activity/ImportOneStreamActivity.smali:333,441,551` |
| C | GET | `play/b2c/v1/content/{live\|vod\|series}?token=T&category_id=all` | full lists | `ImportOneStreamActivity.smali:384-392,494-502,604-612` |
| D | GET | `play/b2c/v1/content/vod/{stream_id}?token=T` | movie details (optional) | `…/activity/ViewDetailsActivity.smali:6283-6293` |
| S | GET | `play/b2c/v1/content/series/{series_id}?token=T` | seasons and episodes | `smali/com/nst/iptvsmarterstvbox/model/webrequest/RetrofitPost.smali:2308-2338` |
| E | GET | `play/b2c/v1/epg/{stream_id}?token=T` | per-channel EPG | `…/activity/SubTVArchiveActivity.smali:517-529` (out of scope) |
| X | GET | `play/b2c/v1/xml-epg?token=T` | XMLTV EPG | `…/activity/NewDashboardActivity.smali:14321` (out of scope) |
| P | play | `play/link_archive_nt/{id}/{date}{time}/duration_{min}.m3u8?token=T` | catch-up | `SubTVArchiveActivity$d.smali:690-718` (out of scope) |

### A. auth
- The body is `username=<u>&password=<p>`.
- PSUV5 sets a literal `Content-Type: FormUrlEncoded` header, but OkHttp's `FormBody` replaces it with `application/x-www-form-urlencoded`. We send the standard value.
- The response is `{"auth_token":"..."}`. A missing or empty token, a transport error or a non-2xx status all mean failure. PSUV5 never inspects HTTP status codes.

### B. user-info
```json
{"user_info":{"auth":1,"status":"Active","username":"u","message":"","expire_at":"2026-12-31 00:00:00",
  "active_connections":"0","created_at":"2025-01-01","max_connections":"1","allowed_output_formats":["m3u8","ts"]},
 "server_info":{"time_now":"2026-10-05 12:00:00","timezone":"UTC"}}
```
PSUV5 treats any of the following as a failed login:
- `user_info.auth` parses to 0.
- `user_info.status` is not exactly `"Active"`.
- Any of the listed fields is missing.

PSUV5 does **not** check `expire_at` against the clock, does not compare `active_connections` with `max_connections`, and ignores `allowed_output_formats` and `message`.

Our port will be:
- **Strict on** `auth` and `status`. We compare `status` without regard to case.
- **Lenient on** the other fields. A missing `created_at` should not lock someone out.
- **Informational for** `expire_at` and the connection counts. They are parsed and shown in the status detail, but they never block login.
- **Displaying** `message`, when it is non-empty, on failure.

### C. catalog lists
Every response wraps its array in `content`. Categories are `content[] = {category_id, category_name, parent_id}`.

Live:
```json
{"content":[{"num":"1","name":"BBC One","stream_type":"live","stream_id":"abc123","stream_icon":"http://…png",
  "epg_channel_id":"bbc1.uk","is_adult":"0","links":{"m3u8":"http://…/x.m3u8","ts":"http://…/x.ts"},
  "tv_archive":"1","tv_archive_duration":"7","categories":["12"]}]}
```

VOD:
- Fields are `{num, name, stream_type, stream_id, stream_icon, rating, rating_5based, added, is_adult, links, categories[]}`.
- Parsed in `ImportOneStreamActivity$c.smali:129-430`.

Series:
- Fields are `{num, name, series_id, cover, genre, release_date, plot, cast, rating, rating_5based, director, backdrop_path, last_modified, episode_run_time, categories[]}`.
- Parsed in `ImportOneStreamActivity$f.smali:136-530`.

Two PSUV5 behaviours to avoid:
- **Only `categories[0]` is used.** We put `categories[0]` in `group_title` and later entries in `alternate_group_titles`, which is capped at 4.
- **Missing fields inherit from the previous item.** PSUV5 reads each field in its own try/catch into shared state. We skip items without `name`, an id, or a usable link.

### S. series info
- `episodes` is either an array of arrays or an object such as `{"1":[…]}`, keyed by season.
- Each episode has `id`, `season` (int), `episode_num` (int), `title`, `added`, `links {fmt:url}` and `info.{movie_image, duration_secs, duration, plot, rating}`.
- `direct_source`, `container_extension` and `custom_sid` are ignored in OneStream mode (`SeriesDetailActivity.smali:2067-2980`).

### Playback URLs are supplied by the server
PSUV5 never builds live, VOD or episode stream URLs. It plays the values from each item's `links` object:
- **Live**, in `…/view/demo/NSTEXOPlayerSkyActivity.smali:13887-14104`: the user's format preference picks `links.ts` or `links.m3u8`, and the other is the fallback.
- **VOD and episodes**, in `ViewDetailsActivity$g.smali:98-121`: the app uses whichever `links` key it iterates last.

Our port sets the item's `url` to `links.m3u8` when present, otherwise `links.ts`, otherwise the first `links` value. The remaining values go into `alternate_urls`, capped at `kMaxAlternateUrls` (3). Every URL passes through `CanonicalizeStreamUrl`, and items with no valid link are skipped. This matches the Xtream VOD ordering (`.m3u8` first, `.ts` second), and `main.cpp:1122-1141` already tries the alternates in order.

### Base URL handling
PSUV5 (`smali/m7/e.smali:1440-1530`, `smali/m7/w.smali:275-366`):
1. Trims the input.
2. Lowercases the **whole** URL.
3. Adds `http://` if no scheme is present.
4. Adds a trailing `/`.
5. Tries each URL in a multi-DNS list in turn.

We lowercase the scheme and host only, because lowercasing the path can break panels hosted under a path. We accept one URL, as the Xtream editor does. Multi-DNS failover is out of scope.

### Token lifetime
- PSUV5 stores the token in prefs (`onestream_token`) and re-runs A then B on every app start (`SplashActivity.smali:5657-5694`).
- It has no refresh on 401 and no expiry handling.

Our port keeps the token in memory only, never on disk, and re-authenticates at the start of every refresh and every series open.

## Step 0: confirm on a real account before writing app code
Step 0 needs the user's own Allvue or Maximus account. Nothing from it goes in the repo.

1. **Scheme check.** Try the panel URL in the existing Xtream editor. If live channels load, the panel is Xtream and the rest of this plan applies only to panels that fail. Then check with curl:
   ```
   curl -si -X POST -d 'username=U&password=P' 'https://PANEL/play/b2c/v1/auth'
   ```
   It should return `auth_token` JSON.
2. **Token in `links`.** Fetch `content/live?token=T&category_id=all` and look at the stream URLs:
   - If they **contain the token**, cached URLs go stale when the token expires, and the "cache the catalog, refresh every 12 h" model (`kCatalogRefreshSeconds`) breaks. Measure the token lifetime by replaying an old link after re-authenticating, or after an hour.
   - If links do expire, adopt option (b) under Implementation step 4.
   - If links are stable, the simple design stands.
3. **Response size.** Record the byte size of `content/{live,vod,series}?category_id=all`. The response cap is 16 MiB (`kMaxXtreamResponseBytes`).
   - If a list is larger, check whether `category_id=<id>` works per category. PSUV5 only ever sends `all`, so this is unverified.
   - Per-category fetching would mirror the `FetchXtreamLibrary` fallback (`src/iptv_xtream.cpp:1261-1366`).
4. **Content-Type.** Confirm the server accepts `application/x-www-form-urlencoded`, which is expected. If it insists on the literal `FormUrlEncoded`, make the content type a parameter.
5. **Redirects.** Check whether `auth` is reached directly or through a 30x. This decides how much redirect handling POST needs; see step 2.
6. Record the findings at the bottom of this file, without hosts, tokens or credentials.

## Delivery order
The working tree has about 2,600 lines of uncommitted changes (the Xtream VOD/series work and `iptv_catalog_view`), and they touch most of the files below. **Commit those first.** Then land the work as separate commits that each pass `make test`:

1. Extract the JSON helpers (refactor, no behaviour change).
2. Add HTTP POST support.
3. Add the `iptv_onestream` module and its tests (host-testable, not yet wired in).
4. Refactor the source descriptors in the app (no behaviour change).
5. Wire up live TV for OneStream.
6. Wire up OneStream movies, series and episodes.
7. Update the docs.

## Implementation

### 1. Extract the shared JSON and encoding helpers
All of these currently sit in an anonymous namespace in `src/iptv_xtream.cpp:23-504`, so they can't be reused.

- **Create `include/iptv_json.h` and `src/iptv_json.cpp`** with `namespace iptv::json`.
  - Move `JsonReader` (cpp:142-353) into it with the same interface: `String`, `Scalar`, `StringOrScalar`, `SkipValue`, `Peek`, `Consume`, `Finished`, `Whitespace`.
  - Move `kMaxJsonDepth`, `HexDigit` and `AppendUtf8`.
  - Move the `ReadObject`/`ReadArray` templates (cpp:355-413). They must live in the header.
  - Keep `ReadArrayResponse` and teach it a `content` wrapper key, or add `ReadArrayResponseKey(reader, "content", ...)`. Today it accepts a top-level `[...]` or `{"data":[...]}`.
- **Move into the same header** `PercentEncode` (cpp:57-78), `EqualsCi` (cpp:30), `SafeCredential` (cpp:41) and `ReplaceFile` (cpp:80), plus `ParseUnsigned`, `ParseYear` and `ParseRatingTenths`.
  - Option: a separate `iptv_text.h` if the header gets mixed.
- **Leave unchanged:** keep the escape quirk where `\b \f \n \r \t` decode to a space (cpp:212-226). Existing tests depend on it.
- **Update `iptv_xtream.cpp`** to `#include "iptv_json.h"` and use `using namespace iptv::json;` inside its anonymous namespace, so the call sites don't change.
- **Makefile:** add `src/iptv_json.cpp` to the dependency list (Makefile:83-94) and the compile list (Makefile:111-113). `tools/build.sh:100-103` globs `src/*.cpp`, so the PS5 build picks it up automatically.
- **Check:** `make test-unit` passes with no test changes.

### 2. HTTP POST support (`include/iptv_http.h`, `src/iptv_http.cpp`)
Today's state:
- `GetM3uResolved` (cpp:923-1078) is GET-only.
- It calls `sceHttpCreateRequestWithURL(connection, kHttpMethodGet, url, 0)` at cpp:961.
- It hard-codes an M3U `Accept` (cpp:974-977).
- It calls `sceHttpSendRequest(request, nullptr, 0)` at cpp:979.

Steps:
1. **Add constants.** Add `kHttpMethodPost = 1` next to `kHttpMethodGet` (cpp:584-588). The SCE enum is GET=0, POST=1.
2. **Factor out a shared core.** Move the body of `GetM3uResolved` into an internal
   ```cpp
   FetchResult PerformRequest(const RequestSpec &spec, char *buffer, std::size_t capacity,
                              char *effective_url, std::size_t effective_capacity,
                              std::size_t max_bytes, const RequestHeaders *headers,
                              const RequestControl *control);
   struct RequestSpec { const char *url; int method; const char *body; std::size_t body_len;
                        const char *content_type; const char *accept; };
   ```
   - It passes `body_len` as `content_length` to `sceHttpCreateRequestWithURL`.
   - It adds `Content-Type` through `sceHttpAddRequestHeader(..., kHeaderOverwrite)` when `content_type` is set.
   - It calls `sceHttpSendRequest(request, body, body_len)`.
   - It uses `spec.accept` in place of the hard-coded string.
   - `GetM3uResolved` becomes a thin wrapper that passes the current M3U accept, `kHttpMethodGet` and no body. **Its behaviour must not change.**
3. **Redirects for POST.**
   - On 301, 302 or 303, switch to GET with no body. This matches browser and OkHttp behaviour.
   - On 307 or 308, re-send the same method and body.
   - The existing `RedirectHistory` (cpp:753-794) and the 5-redirect limit apply unchanged.
4. **Add the public API** to the header:
   ```cpp
   FetchResult PostForm(const char *url, const char *body, std::size_t body_len,
                        char *buffer, std::size_t buffer_capacity,
                        std::size_t max_bytes = kDefaultMaxPlaylistBytes,
                        const RequestHeaders *headers = nullptr,
                        const RequestControl *control = nullptr);
   FetchResult GetJson(const char *url, char *buffer, std::size_t buffer_capacity,
                       std::size_t max_bytes, const RequestHeaders *headers = nullptr,
                       const RequestControl *control = nullptr);
   ```
   - `PostForm` always sends `Content-Type: application/x-www-form-urlencoded` and `Accept: application/json`.
   - `GetJson` is optional: it sends `Accept: application/json`. The existing `GetM3u` also works against JSON endpoints, as Xtream shows. Add `GetJson` only if a panel rejects the M3U accept.
   - Bound `body_len` by a new `kMaxFormBodyBytes = 2048`. Return `invalid_argument` if it is exceeded or if `body` contains a NUL.
5. **Error bodies.** A non-2xx response already returns up to 511 bytes of the error body in `buffer` (cpp:1008-1016). The OneStream code uses this to show the server `message` when it can.
6. **Host stub.** In the `#else` branch (cpp:1175-1248), `PostForm` validates its arguments and returns `platform_unavailable`, matching `GetM3uResolved`.
7. **Tests** (`tests/test_iptv_http.cpp`): add host tests that `PostForm` rejects a null or oversized body, an unsupported URL, and `capacity < max_bytes + 1`. The SCE path has no host coverage; see Risks.

### 3. New module (`include/iptv_onestream.h`, `src/iptv_onestream.cpp`)
It follows the shape of the Xtream module. Everything is pure or fetcher-driven, so it can be tested on the host.

```cpp
namespace iptv {
inline constexpr char kDefaultOneStreamCredentialsPath[] = "/download0/prosperotv-onestream-v1.txt";
inline constexpr std::size_t kMaxOneStreamServerBytes = 1020;    // same as Xtream
inline constexpr std::size_t kMaxOneStreamTokenBytes  = 4096;
inline constexpr std::size_t kMaxOneStreamResponseBytes = kMaxXtreamResponseBytes; // 16 MiB

enum class OneStreamStatus { ok, invalid_argument, not_found, too_large, io_error, corrupt,
    malformed_json, authentication_failed /*empty token*/, account_inactive /*auth==0 or status!=Active*/,
    no_channels, no_movies, no_series, no_episodes, fetch_failed, cancelled };
const char *OneStreamStatusDescription(OneStreamStatus);

struct OneStreamCredentials { std::string server_url, username, password; };   // server_url ends in '/'
struct OneStreamSession     { std::string token; };                              // never persisted
struct OneStreamAccount     { std::string status, message, expire_at, created_at;
                              std::uint32_t max_connections = 0, active_connections = 0;
                              std::string server_time, timezone; };

// Transport: GET and form-POST, both injectable for tests.
struct OneStreamTransport {
    XtreamFetchOutcome (*get)(void *ctx, const std::string &url, std::string_view *body);
    XtreamFetchOutcome (*post_form)(void *ctx, const std::string &url, std::string_view form,
                                    std::string_view *body, int *http_status);
    void *context;
};
}
```

Rename `XtreamFetchOutcome` to a neutral `FetchOutcome` in `iptv_catalog.h`, and keep `using XtreamFetchOutcome = FetchOutcome;` so existing code and tests keep compiling.

**Pure builders.** Each returns `bool` and enforces `kMaxUrlBytes` (4096):
- `NormalizeOneStreamServerUrl(std::string_view input, std::string *out)`:
  1. Trims the input.
  2. Adds `http://` if no scheme is present.
  3. Lowercases the scheme and host.
  4. Rejects `?`, `#`, `@` in the authority, spaces and control characters.
  5. Strips a trailing `play/b2c/v1/...` if the user pasted an endpoint.
  6. Ensures a trailing `/`.
  7. Runs the result through `CanonicalizeStreamUrl`.
- `ValidateOneStreamCredentials(const OneStreamCredentials &)`: the server is normalised; the username and password pass `SafeCredential`.
- `OneStreamSourceId(const OneStreamCredentials &)`: FNV-1a over server, user and password, using the same 0xff separators as `XtreamSourceId` (cpp:536-553), with the prefix **`0x4F53000000000000`** ("OS").
  - The token is excluded, so the ID stays stable across logins.
- `BuildOneStreamAuthUrl(creds, &url)` gives `<server>play/b2c/v1/auth`.
- `BuildOneStreamAuthBody(creds, &body)` gives `username=<pct>&password=<pct>`, using `PercentEncode`. Encoding a space as `%20` is valid in a form body.
- `BuildOneStreamUrl(creds, session, std::string_view path, std::string_view extra_query, &url)` gives `<server>play/b2c/v1/<path>?token=<pct>[&<extra_query>]`. Wrappers built on it:
  - `BuildOneStreamUserInfoUrl` gives `user-info`.
  - `BuildOneStreamCategoriesUrl(kind)` gives `categories/{live|vod|series}`.
  - `BuildOneStreamContentUrl(kind, category_id = "all")` gives `content/{kind}` with `category_id=<pct>`.
  - `BuildOneStreamSeriesInfoUrl(series_id)` gives `content/series/<pct id>`.
  - `OneStreamKind` is `live`, `vod` or `series`.

**Parsers:**
- `ParseOneStreamAuth(json, &session)` returns `ok` for a non-empty `auth_token` no longer than `kMaxOneStreamTokenBytes` that contains only printable ASCII.
  - It returns `authentication_failed` for a missing or empty token, and `malformed_json` for invalid JSON.
  - A non-2xx body containing `message` is surfaced through an optional `std::string *message` out-parameter.
- `ParseOneStreamUserInfo(json, &account)` returns `account_inactive` if `user_info.auth` parses to 0, or if `status` is non-empty and not "active" in any case. Otherwise it returns `ok`, with the optional fields filled in when present.
  - `auth` may be a number or a string, so use `StringOrScalar`.
- `ParseOneStreamCategories(json, std::vector<XtreamCategory> *)` reads `content[]` `{category_id, category_name}`. Reusing `XtreamCategory` (`{id, name}`) is fine; rename it to `CatalogCategory` alongside `FetchOutcome` if desired.
- `ParseOneStreamLinks(JsonReader*, std::string *primary, std::vector<std::string> *alternates)` applies the m3u8 → ts → first-value order from "Playback URLs" above.
  - It accepts `links` either as an object or as a JSON-encoded string containing an object. PSUV5 stores it as text, so some servers may already send a string.
- `ParseOneStreamLiveStreams(json, creds, categories, source_id, CatalogState*, ParseReport*)`:
  - `id` = `"onestream:%016llx:" + stream_id`.
  - `tvg_name` = `name`, `tvg_id` = `epg_channel_id`, `tvg_logo` = `stream_icon`.
  - `group_title` = the name of category `categories[0]`, falling back to "Live TV". Extra categories go into `alternate_group_titles`.
  - `kind` = `live`.
  - Capped at `kDefaultMaxChannels`. Duplicate ids are skipped and counted in `report->duplicates`.
- `ParseOneStreamVodStreams(...)`:
  - `id` = `onestream:<src>:vod:<stream_id>`, `kind` = `movie`, `tvg_logo` = `stream_icon`.
  - `rating_tenths` comes from `rating` via `ParseRatingTenths`. `year` comes from `added` if it looks like a date; otherwise it stays 0.
  - `container_ext` is the extension of the chosen URL's path (`m3u8`, `ts`, `mp4`, …). The catalog view's "may not play" warning (`iptv_catalog_view.cpp:513`, `XtreamContainerStreamable`) then still works.
  - The group falls back to "Movies".
- `ParseOneStreamSeriesList(...)`:
  - `kind` = `series`, `series_id` = `series_id`, `tvg_logo` = `cover`, `year` from `release_date`, `rating_tenths` from `rating`.
  - `url` = **`onestream-series:<series_id>`**, a placeholder that never contains the token. It satisfies the store's non-empty URL requirement (`iptv_store.cpp:103-147`) and is never played.
  - The group falls back to "Series".
- `ParseOneStreamSeriesInfo(json, creds, series_id, series_name, source_id, CatalogState*, ParseReport*)`:
  - Handles `episodes` as an array of arrays or an object keyed by season.
  - Produces `kind = episode`, named `"S%02u E%02u <title>"`, with group "Season N", `season`, `episode`, `duration_secs` (from `info.duration_secs`, or `info.duration` in HH:MM:SS) and `tvg_logo` = `info.movie_image`. URLs come from `links`.
  - It reuses the Xtream episode naming helper if it can be extracted.
  - Capped at `kMaxXtreamEpisodes` (4096).

**Flows**, which take a `OneStreamTransport` and a `RequestControl`-style cancel through the transport context:
- `OneStreamLogin(creds, transport, &session, &account, std::string *message)` runs A, then B. It returns the first failing status.
- `FetchOneStreamLive(creds, session, source_id, transport, CatalogState*, report)` fetches the live categories (a failure there is non-fatal, as with Xtream), then `content/live?category_id=all`.
- `FetchOneStreamLibrary(creds, session, source_id, XtreamLibraryKind kind, transport, CatalogState*, XtreamLibraryReport*)` handles movies or series.
  - On `too_large` it falls back to one request per category with `category_id=<id>`, merging with `MergeXtreamLibraryPart`. Keep this fallback only if Step 0 shows `category_id=<id>` works; otherwise return `too_large`.

**Credentials persistence:**
- `SaveOneStreamCredentials` writes 4 lines: the magic `PROSPEROTV-ONESTREAM-1`, server, user and password.
- It writes to `<path>.tmp` and then calls `ReplaceFile`, the same as `SaveXtreamCredentials` (`iptv_xtream.cpp:615-642`).
- `LoadOneStreamCredentials` reads into a bounded buffer and requires exactly 4 lines plus the magic. It then re-validates, mirroring cpp:644-680.
- Both have default-path overloads.
- **The token is never written to disk.** Storage is plaintext, matching the Xtream record (README.md:216-220).

### 4. App wiring, part 1: refactor sources into a descriptor table (no behaviour change)
The app picks the cache path, source ID and labels with nested `custom ? … : xtream ? … : built-in` ternaries in five places:
- `Initialize` (`src/iptv_app.cpp:498-506`, `531-537`)
- `LoadActiveSourceCache` (1264-1271)
- `SelectSource` (1313-1316)
- `RequestRefresh` (1514-1549)
- `ConsumeLiveStage` (1735-1895)

Indices are also hard-coded in two places:
- `source_health_[1]` and `source_health_[2]` (cpp:482, 486, 1375, 1494).
- `focus_slot_ == 1` and `== 2` (cpp:1068-1100).

A fourth source would push the ternaries to three levels deep, so replace them first:
```cpp
struct SourceDescriptor { SourceKind kind; const char *live_cache, *vod_cache, *series_cache,
                          *live_receipt, *vod_receipt, *series_receipt, *label; bool has_library; };
const SourceDescriptor &Describe(SourceKind);
std::uint64_t ActiveSourceId() const;   // switch on kind → kCatalogSourceId / CustomSourceId / XtreamSourceId / OneStreamSourceId
bool SourceConfigured(SourceKind) const;
void OpenSourceEditor(SourceKind);
```
- Replace the Xtream-only gates (`active_source_ == SourceSelection::Xtream` at cpp:52, 346, 411, 2143, 2163, 2199 and 2604) with `Describe(active_source_).has_library` or a new `IsAccountSource(active_source_)`.
- Land this as its own commit. `make test` passes unchanged and the on-device behaviour is identical.

### 5. App wiring, part 2: the OneStream source
- **Source kind** (`include/iptv_source_state.h:30-34`, `src/iptv_source_state.cpp:116-185`):
  - Add `SourceKind::OneStream = 3` and persist it as `"onestream\n"`. That is 10 bytes, which fits the 16-byte load buffer.
  - Set `SourceCount = 4` (`include/iptv_app.h:101-102`).
  - Extend `tests/test_iptv_source_state.cpp` if it exists, or else the store tests, with the round-trip.
- **Constants** (cpp:36-52), alongside the Xtream ones:
  - `kOneStreamCatalogCachePath "/download0/prosperotv-onestream-catalog.sqlite3"`
  - `kOneStreamVodCachePath "/download0/prosperotv-onestream-vod.sqlite3"`
  - `kOneStreamSeriesCachePath "/download0/prosperotv-onestream-series.sqlite3"`
  - Three receipt paths `prosperotv-onestream{,-vod,-series}-receipt.txt`.
  - Each source has its own files, so switching between the Xtream and OneStream sources never invalidates the other's cache. `source_id` is still checked on load (app:352, 512, 1277).
- **State** (`include/iptv_app.h:185-221`):
  - Add `onestream_credentials_`, plus `onestream_editor_` with the same three stages.
  - Generalise the editor rather than copying it. `XtreamEditorStage` becomes `AccountEditorStage`, with an `editor_kind_` (`SourceKind`) and one `account_editor_` record `{server, username, password}`.
  - `OpenXtreamEditor` through `ApplyXtreamPassword` (cpp:1382-1499) become `OpenAccountEditor(SourceKind)` and the matching `Continue`/`Apply` functions.
  - Keep the static IME callbacks, but route them on `editor_kind_`.
  - The IME prompt labels come from the descriptor: "Xtream password" and "OneStream password". `tests/test_tools.py:460` asserts the exact Xtream string, so keep it.
  - `ApplyServer` calls `NormalizeXtreamServerUrl` or `NormalizeOneStreamServerUrl` depending on the kind.
  - `ApplyPassword` compares source IDs, saves the matching credential file, sets `source_health_[index]`, and calls `SelectSource(kind)`.
- **Status plumbing.** `pending_xtream_status_` and `pending_xtream_message_` (h:206-208), `LibraryStage` (h:212-221) and `episode_status_` are typed on `XtreamStatus`.
  - Add a parallel `pending_onestream_status_`, **or** convert both into a provider-neutral `{bool ok; std::string description;}` produced at the worker boundary through `XtreamStatusDescription` / `OneStreamStatusDescription`.
  - **Recommendation:** use the neutral form. `ConsumeLiveStage` (1807-1898) only needs the text and an ok flag.
- **Refresh worker** (`RefreshThreadEntry`, cpp:1586-1730):
  - Add a OneStream branch next to the Xtream branch (1627-1709).
  - Build a `OneStreamTransport` whose `get` reuses `FetchLibraryUrl` (cpp:131-148) and whose `post_form` wraps the new `http::PostForm`, carrying the same `LibraryFetchContext` and cancel control.
  - Stages and their receipt labels:
    1. `"authentication"`: `OneStreamLogin`. Keep the token in a local variable; it must not outlive the thread.
    2. `"categories"` and `"live-streams"`: `FetchOneStreamLive` into `pending_catalog_`, then `finish_live_stage()`.
    3. For movies and series: `FetchOneStreamLibrary`, then `SaveCatalog(path, library, LibraryStoreLimits())`. Run this only if login succeeded, as Xtream does.
  - Set `refresh_url_` empty, `refresh_cache_path_` from the descriptor and `refresh_source_id_ = OneStreamSourceId(...)`.
- **Receipts.** Generalise `SaveXtreamReceipt` and `SaveXtreamLibraryReceipt` (cpp:54-119) to take a provider tag. The OneStream receipt header is `PROSPEROTV_ONESTREAM_RECEIPT_V1`. It records stage, status, counts and a timestamp, and never the token.
  - Add the account fields `expire_at`, `max_connections` and `active_connections`. These are useful to the user and not secret.
- **Series and episodes** (`OpenSeries` cpp:2187-2241, `EpisodeThreadEntry` 2243-2273, `ConsumeEpisodes` 2275-2318):
  - For a OneStream source, snapshot `onestream_credentials_`.
  - The episode worker runs `OneStreamLogin`, then `GET content/series/<series_id>` (from `channel.series_id`, not `channel.url`), then `ParseOneStreamSeriesInfo`. Re-authenticating costs one extra round trip per series open, and it avoids holding a token across threads.
  - Option: cache the token in an `std::atomic`-guarded member, set by the refresh worker, for the session's lifetime. Only do this if the round trip is noticeably slow on device.
- **Playback.**
  - `QueuePlay` (cpp:2123-2151) needs no change, because URLs come from the catalog.
  - Change `reconnect_live` (cpp:2142-2143) to `kind == live && IsAccountSource(active_source_)`.
  - **If Step 0.2 shows links expire with the token**, choose one of:
    - **(a)** Force a refresh when a cached catalog is older than the measured token lifetime. This is cheap to build: lower `kCatalogRefreshSeconds` per source.
    - **(b)** Store links without the token and append it at play time. That needs a session token at play time, an extra `QueuePlay` hook, and knowledge of how the server embeds the token.
    - Prefer (a) unless the lifetime is minutes.
- **UI** (`ui/main.rml`, `ui/styles/app.rcss`, `tests/test_tools.py`):
  - Add a `<div id="source-slot-3" class="rail-item hidden">` with `source-name-3` "OneStream panel", `source-meta-3` and `source-status-3` after line 18. Only the active source is visible in the rail, so the rail needs no re-layout.
  - Add `<button id="source-management-slot-3" class="source-management-card">` with `-name-3`, `-url-3`, `-health-3` and `-action-3` on line 48.
  - The card text in `RefreshSourceUi` (cpp:1216-1260) comes from the descriptor: "Add a OneStream panel account" or the normalised server, and the action "Triangle: edit".
  - **Layout:** the management region is 640 px high (`app.rcss:116`) with 164 px cards at tops 58, 240 and 422 (`app.rcss:117-118`), so a fourth card would overflow.
    - Change to 4 cards of 130 px at tops 58, 198, 338 and 478. The last card ends at 608 px.
    - Tighten the card child offsets (`app.rcss:119-124`) to fit, and check on device that the name, URL, health and action lines don't clip.
  - Add the missing `#source-slot-2, #source-slot-3` width rule next to `app.rcss:38` (an existing gap).
  - Update the subtitle on `main.rml:47` to mention OneStream panels.
  - Update `tests/test_tools.py:441-453` for the new markup and rcss strings.
- **Input** (`HandleInput` Sources block, cpp:1051-1107): Up/Down is already bounded by `SourceCount`. Triangle and Cross go through `OpenSourceEditor(kind)` and `SelectSource(kind)` instead of index checks.
- **Startup** (`Initialize`, cpp:420-572): call `LoadOneStreamCredentials` and set `source_health_[3] = Saved`. `LoadActiveSource` accepts `OneStream` only when it is configured, as for the others.

### 6. Provider presets (optional; only if the user asks)
Allvue and Maximus differ only by panel URL, so a preset would be a label in the editor that pre-fills the server field.
- It needs the URLs from the user.
- It does not belong in the repo unless they choose that.
- If added, keep presets in a user-editable file on `/download0`, not in the source.

## Tests (host, `make test-unit`)
**New `tests/test_iptv_onestream.cpp`**, using a `FakeTransport` modelled on `FakeServer` (`tests/test_iptv_xtream.cpp:239-271`):
- It maps `url` to `{outcome, body}` for GET, and `url + form` to `{outcome, body, http_status}` for POST.
- It records `requested` and `posted`. Unknown URLs fail.

Cases to cover:
- **Builders:**
  - Normalising `panel.example`, `HTTP://Panel.Example/sub`, `https://p.example/play/b2c/v1/auth` and inputs with a trailing slash.
  - Rejecting `?`, `#`, `@`, spaces, controls, and URLs over 4096 bytes.
  - The auth body encoding `&`, `=`, `+`, a space and UTF-8 in the username and password.
  - The token being percent-encoded in query URLs.
  - `category_id` encoding.
  - `OneStreamSourceId` stable under token changes, different from `XtreamSourceId` for the same triple, with the `0x4F53` prefix.
- **Auth:** success; an empty token; a missing key; a token over the cap; invalid JSON; a non-2xx response whose `{"message":"…"}` is surfaced.
- **User-info:**
  - `auth` as `1`, `"1"`, `0` or `"0"`.
  - `status` as `Active`, `active`, `Expired`, `Banned` or missing.
  - Optional fields missing: still ok.
  - Connection counts as numbers or strings.
- **Login flow:** the call order is POST then GET; there is no GET after a failed auth; cancellation midway returns `cancelled`.
- **Links:** both `m3u8` and `ts` present (m3u8 primary, ts alternate); `ts` only; an unknown key only; `links` as a JSON string; an empty object (item skipped); an invalid URL (skipped); more than 3 alternates (capped).
- **Live mapping:** ids, groups from `categories[0]`, `alternate_group_titles`, a missing category name falling back to "Live TV", duplicate ids, the channel cap, `content` missing or not an array (`malformed_json`), an empty `content` (`no_channels`).
- **No inheritance:** an item missing `stream_icon` gets an empty logo, not the previous item's.
- **VOD and series:** `container_ext` from the URL, rating and year parsing, the series placeholder URL never containing the token, and an empty list giving `no_movies` / `no_series`.
- **Series info:** episodes as array-of-arrays and as a season-keyed object; episode naming; `duration` in HH:MM:SS; an empty list giving `no_episodes`; the 4096 cap.
- **`too_large` fallback**, if kept: per-category requests are merged and de-duplicated.
- **Credentials:** a round trip in `::testing::TempDir()` (pattern at `test_iptv_xtream.cpp:474-498`); a wrong magic, wrong line count or oversized file gives `corrupt`; a missing file gives `not_found`; the file never contains the token.

**Fixtures** are synthetic JSON shaped like the examples above. They must not contain real hosts or tokens.

**Other test changes:**
- `tests/test_iptv_xtream.cpp` keeps passing after the JSON extraction, with no edits.
- Add `tests/test_iptv_onestream.cpp` and `src/iptv_onestream.cpp` (plus `src/iptv_json.cpp`) to **both** Makefile lists: dependencies at Makefile:83-94 and the compile command at Makefile:101-116.
- `tests/test_iptv_store.cpp`: add one case that saves and loads a catalog whose `source_id` has the `0x4F53` prefix and whose series entry has the placeholder URL.
- `tests/test_tools.py`: update it for the new UI slots and IME prompt.

## Documentation
- `docs/CONFIGURATION.md:145-160`:
  - Add the fourth source.
  - Describe the OneStream panel setup: the server URL is the panel root, the token is never stored, and the credential file `prosperotv-onestream-v1.txt` is plaintext.
  - Fix the outdated line saying VOD and series are out of scope.
- `README.md`:
  - Source list (10, 26-28, 85-87).
  - Controls (125, 127; also fix the outdated L1/R1 tab list).
  - Credential disclaimer (216-220).
  - Test coverage (234) and source layout (250): add `iptv_onestream.cpp` and `iptv_json.cpp`.
- Don't name Allvue or Maximus as supported unless Step 0 confirms it on a real account.

## Verification
1. After each commit in "Delivery order", `make test-unit` passes (host clang++, `-std=c++20 -Werror`).
2. `make test-integration` still passes, including `tests/test_tools.py`.
3. `make` (the default target `app`, which runs `tools/build.sh Folder`) builds the PS5 payload.
4. **Regression check, on device, before any OneStream testing:**
   - The built-in, custom M3U and Xtream sources still load, refresh and play.
   - An Xtream series still opens episodes.
   - This covers the refactor in step 4 and the HTTP core change in step 2.
5. **OneStream, on device**, using the user's account:
   - Add the account and confirm the live catalog loads and a channel plays.
   - Movies and Series tabs load, and a movie plays.
   - A series opens episodes and one plays.
   - Status detail shows the expiry date and connection counts.
   - Re-launch: the credentials reload, the cached catalog shows immediately, and a refresh re-authenticates. The receipt shows the `authentication` stage, and no file under `/download0` contains the token (`grep` the downloaded files).
   - A wrong password shows the authentication failure. An expired or disabled account shows "inactive" with the server `message` if there is one.
   - With no network, the cached catalog stays usable and the status shows the fetch failure.
   - Cancel a refresh midway: no crash, and the previous catalog remains.
   - If Step 0.2 found expiring links, play a channel from a catalog older than the token lifetime and confirm the chosen mitigation works.

## Out of scope (possible follow-ups)
- EPG: endpoints E and X.
- Catch-up: endpoint P. Note the odd date/time joining in PSUV5.
- VOD detail metadata: endpoint D.
- Multi-DNS failover.
- A per-user format preference: PSUV5's `allowedFormat` setting.
- Encrypted credential storage.

## Risks and open items
- **Links may embed or expire with the token** (Step 0.2). This decides whether the cached catalog model holds.
- **Response size.** A single `category_id=all` response may exceed 16 MiB. Per-category support is unverified because PSUV5 never uses it.
- **No host coverage for the HTTP core.** POST support refactors the shared HTTP request code, and the host stub cannot test the SCE path. The mitigations are a no-behaviour-change wrapper for `GetM3uResolved`, a separate commit, and the device regression pass in Verification step 4.
- **No host coverage for the app.** `iptv_app.cpp` is not in the host test build. The descriptor refactor (step 4) is the riskiest app change. Keep it mechanical and in its own commit.
- **The UI layout change is checked by eye.** The four-card layout needs on-device checking for clipped text.
- **Uncommitted work.** About 2,600 lines in the working tree overlap almost every file here. Land them before starting.
- **Unconfirmed scheme.** It is not confirmed that Allvue or Maximus use OneStream at all. Step 0.1 may make most of this unnecessary.

## Step 0 findings
_(Fill in after testing. Leave out hosts, tokens and credentials.)_
- Scheme per panel:
- `links` contain the token? Token lifetime:
- `content/*?category_id=all` sizes (live / vod / series):
- `category_id=<id>` supported:
- Content-Type accepted:
- Redirects on auth:

## Phase 2: full Live TV, Movies and Series parity with PSUV5

Phase 1 covers sign-in plus live, movies, series and episodes for Xtream and OneStream. The rest of what PSUV5 offers for these three tabs is below, in the suggested order. Smali references use the same apktool tree as above (`sc4/…` = `smali_classes4/com/nst/iptvsmarterstvbox/…`).

### 2.1 How PSUV5 users get a provider (decision needed)
PSUV5 hides the Xtream server field (`res/layout/login_new.xml:32`). It gets servers from a reseller control panel whose address is built into the APK (`smali_classes5/com/PanelUrl.smali:26`). That address is deliberately kept out of this repository.
- **Server list:** `dns.php` returns `{"su":"url1,url2","sn":"name1,name2"}` and the user picks one (`DnsTask.smali:57-61`, `LoginActivity.onDnsFetched`). Allvue and Maximus are probably entries in this list.
- **Auto-login by device address:** `mac_check.php?mac=` returns `user_info{username,password,server}` (`MacCheckTask`, `MacCheckParser`).
- **TV activation code:** `api.php` actions `registercodetv` / `verifycodetv` return `{username,password,dns,type,m3ulink}`, where `type` is `api`, `onestream_api` or `m3u` (`FirebasePresenter.smali:728,799`, `FirebaseRegisterDeviceActivity.smali:740-1000`).

Options:
- **(a) Do nothing.** The user enters the server address manually. This works today.
- **(b) A server-list picker** that fetches `dns.php` from a panel address the user configures. Don't build the reseller host into the repo.
- **(c) Device-address or activation-code login.** This sends the console's MAC address to a third party, so it needs an explicit opt-in.

**Decision: option (b).** The default panel lives in a gitignored `include/iptv_panel_local.h`, so it is built in locally but never committed.
- **Commits:** `d411f0d` adds the module and tests. `src/iptv_panel.cpp` includes the local header with `__has_include`, and `include/iptv_panel_local.h.example` shows the format.
- **Picker:** the next commit adds it to the account editor. A worker fetches `dns.php` and is exclusive with the other network workers. Left/Right picks a server, Triangle types an address, Square changes the panel, Circle cancels.
- **Panel on the console:** set with Square on an account card, saved in `/download0/prosperotv-panel-v1.txt`. It takes precedence over the built-in default.
- **Format confirmed from smali:** `LoginActivity.onDnsFetched` sends a plain GET to `{panel}dns.php`. `"su"` is required and `"sn"` optional, both comma-separated and matched by position.
- **Not done:** option (c), sign-in by device address or activation code.

### 2.2 Movies and series in M3U playlists (done; not yet tested on a console)
PSUV5 classifies an M3U entry by its stream URL only (`smali_classes4/A7/a.smali:1160-1208`):
- `/movie/` or `/movies/` → movie.
- `/series/` → series.
- Anything else → live.

It then lists a category's "series" lines (`getAllLiveStreasWithCategoryId(category, "series")`), and each line is one episode.

ProsperoTV's port:
- **Same rule, stricter match:** whole path segments only, so a query string or file name can't trigger it.
- **Only for the custom playlist source:** the built-in iptv-org list stays live-only.
- **Episodes grouped into shows:** by the name before `SxxEyy` in the title, which is what Xtream's M3U export produces. Entries without that pattern fall back to their `group-title`.
- **Movies, series and episodes get their own caches.** Opening a series reads its episodes from the episode cache on the worker thread.
- **No tvg-id merging for movies and episodes:** the parser merges entries by `tvg-id` for live channels only.
- **Size limit:** custom playlists may use the 16 MiB hard limit and up to 65,536 entries, the parser's hard cap (`kHardMaxChannels`). Live is still capped at 32,768.
- **Commits:** `8afefb5` (classifier and split, 3 host tests) and the app commit after it. Playlist series and episodes use their own caches (`prosperotv-custom-{vod,series,episodes}.sqlite3`).
- **Playlists with only movies and series** don't show a catalog error.

### 2.3 Movie and series details (done for Xtream and OneStream; not yet tested on a console)
How it was built:
- **Parsing:** `ParseMediaInfo` reads the `info` object of all four responses: Xtream `get_vod_info` and `get_series_info`, OneStream `content/vod/{id}` and `content/series/{id}`.
- **Fetching:** a worker starts once the selection has rested for 20 frames, about a third of a second. It never runs alongside a refresh or an episode download, because they share the HTTP layer's single network session.
- **Caching:** the last 48 titles are kept in memory, including titles whose details failed, so they aren't fetched repeatedly.
- **OneStream token:** kept in memory for the session and renewed once when the panel refuses it.
- **Opening a series** while a details fetch is running cancels the fetch first.
- **M3U titles show no details.** A TMDB lookup would need the user's own API key.
PSUV5 shows plot, cast, director, genre, duration, rating and backdrops:
- **Xtream:** `get_vod_info&vod_id=` (`n7/l.smali:62`) and the `info` block of `get_series_info`.
- **OneStream:** `content/vod/{id}` (`ViewDetailsActivity.smali:6283-6293`) and the series `info` block.
- **M3U:** a TMDB name search (`search/movie`, `search/tv`), which needs the user's own TMDB key. Don't reuse the key built into PSUV5.

Work:
- Add a details record, fetched on demand and cached per item, rather than adding columns to the catalog.
- Add a details panel to the Movies and Series screens.

### 2.4 TV guide (EPG) (done; not yet tested on a console)
How it was built:
- **No zlib in the console build:** zlib is only built for the host packaging tool (`tools/build.sh:81-94`). So `src/iptv_inflate.cpp` is a small streaming gzip/deflate decoder of its own, modelled on zlib's reference "puff", tested against fixtures made with Python's zlib.
- **Parsing:** `src/iptv_xmltv.cpp` is a streaming XMLTV reader. It handles comments, CDATA, entities, quoted `>` and time offsets, and never buffers more than 64 KiB.
- **Storage:** `src/iptv_guide.cpp` writes a SQLite guide, filtered to the source's channel ids (ignoring case) and to the window from now −2 h to +3 days, capped at 400,000 programmes. It is written to a staging file and renamed into place, so a failed download keeps the previous guide.
- **Download:** a refresh stage runs right after the live stage, streaming through `OpenStream`/`ReadStream`. A shutdown ends it within one 5 s receive timeout.
- **Display:**
  - Live TV cards show "NOW: <title>" in place of the stream details line.
  - The selected channel shows now, next and the description in the details text box.
  - Times use `std::localtime`. The console's time zone setting may not reach it, in which case times show in UTC. Check this on the device.
- **Original notes:**
| Provider | Guide source in PSUV5 |
|---|---|
| Xtream | `xmltv.php?username=&password=` (`NewDashboardActivity.smali:14346-14373`) |
| OneStream | `play/b2c/v1/xml-epg?token=` |
| M3U | A guide address the user adds manually. PSUV5 ignores `url-tvg`; ProsperoTV can read `url-tvg` / `x-tvg-url` as a bonus. |

  - **PSUV5's parser** (`smali_classes4/B7/d.smali`) is a streaming SAX parser that handles `.gz`. It reads `<programme start stop channel>` with `title` and `desc` only.
  - **Matching** is an exact `channel_id == epg_channel_id / tvg-id`.

Work:
- A bounded streaming XMLTV parser with gzip support. First check that zlib is available in the PS5 build.
- SQLite storage keyed by `(source_id, channel id, start)`.
- A refresh stage after the live channels.
- Now/next on channel cards and in the details panel.

### 2.5 Catch-up (needs 2.4)
- **Xtream:** listings come from `get_simple_data_table&stream_id=`, whose titles are base64-encoded (`n7/k.smali:64`, `q7.1/a0.smali:754`). Play URLs are `/timeshift/{user}/{pass}/{minutes}/{yyyy-MM-dd:HH-mm}/{id}.ts` (`m7/w.smali:3977-4200`).
- **OneStream:** listings come from `epg/{id}`. Play URLs are `play/link_archive_nt/{id}/{date}{time}/duration_{min}.m3u8?token=` (`SubTVArchiveActivity$d.smali:690-718`).
- Only channels with `tv_archive` set offer catch-up.

### 2.6 Smaller parity items (optional)
- **Parental lock:** dropped at the user's request.
- **Sort order:** A–Z, Z–A, or recently added.
- **Adult flag:** OneStream sends `is_adult`, and PSUV5 stores it without filtering on it.
- **Stream format choice (ts or m3u8):** already covered, because ProsperoTV tries m3u8 first and falls back to ts.

### Not ported
- **Stalker/MAG portals:** no screen in this build opens `LoginActivityStalker`.
- **WHMCS activation:** the code exists but never runs, because `m7/a.y` is false.
- **`get.php`:** unused.
