# ProsperoTV roadmap

Ideas for future versions: some asked for by the community, some our own.
The order inside a group does not imply priority. Checked items are implemented
in the current source. Console acceptance and physical-device limits are recorded
in docs/ROADMAP_REMAINING.md and docs/SURROUND_AUDIO.md. What the app already does is listed under
[Features](README.md#features).

To ask for something that is not here, or to say which of these matters to
you, [open an issue](https://github.com/ThatGuyKalvin/ProsperoTV/issues).

## From recent reports

- [ ] **Surround sound to a receiver.** Sending 5.1 sound as it is, instead
  of mixing it down to stereo, when the console's output allows it.
  [Decoded PCM support is implemented and console-tested](docs/SURROUND_AUDIO.md);
  physical receiver/speaker verification remains pending.
- [x] **Crash reports.** When the app closes by itself, the next launch
  offers to save what happened, to send with a report.
- [x] **Choosing the quality.** Picking a lower quality of a channel by hand,
  or automatically when the connection cannot keep up.
- [x] **Checking a source.** Going through a source's channels in the
  background and marking the ones that do not open, instead of finding out
  one at a time.

## Watching

- [x] **Recording.** Saving what is playing to a USB drive, and scheduling a
  recording from the programme guide.
- [x] **Continue watching.** Resuming a movie or an episode where it was
  left, and playing the next episode when one ends.
- [x] **Moving within a film.** Skipping by ten seconds or a minute, a bar
  with the time elapsed, and chapters where the file has them.
- [x] **Playback speed.** Faster or slower, for films and catch-up.
- [x] **Picture controls.** Zoom and fill for 4:3 and letterboxed channels,
  and the size and colour of subtitles.
- [x] **Picture in picture.** Keeping a channel in a corner while browsing
  the guide or the lists.
- [ ] **Picture in picture over other apps.** Keeping a channel in a corner
  while using something other than ProsperoTV.
- [ ] **Opening a stream in another player.** Handing a channel or film to
  another player on the console, such as VLC.
- [x] **Sound delay.** Moving the sound earlier or later on a channel whose
  own timing is wrong.
- [x] **Reminders.** Marking a programme in the guide and being told when it
  starts.

## Browsing

- [x] **Channel numbers.** The provider's numbering, and a number pad to
  jump to a channel.
- [x] **Arranging channels.** Reordering and renaming channels and
  favorites, and hiding a single channel rather than a whole category.
- [x] **One search for everything.** Live channels, movies, TV shows and the
  guide from the same box.
- [x] **Movie and show details.** Poster, plot, cast, year and rating from
  the provider, with sorting and filters by genre or year.
- [x] **Recently added and most watched.** Two more lists beside Favorites
  and Recent.
- [x] **A home screen.** What was being watched, the favorites, and what is
  on now on the usual channels, in one place.

## Sources

- [x] **Playlists from a USB drive.** Loading a playlist from a file, not
  only from an address.
- [x] **Request settings for a source.** Its user agent, referrer and other
  headers, editable in the app.
- [x] **A guide of your own.** Adding an XMLTV address and matching it to a
  playlist's channels.
- [x] **Account information.** The expiry date and connection limit of an
  Xtream account, with a notice before it runs out.

## Household and the phone

- [x] **More on the phone remote.** The channel list and the guide on the
  phone, playing a channel with a tap, and typing a search there.
- [x] **Viewing limits.** A daily allowance or a bedtime in the kids' mode.
- [x] **Sharing between consoles.** Favorites and sources carried over the
  home network, without a USB drive.
- [x] **More languages.** More interface languages, and right-to-left
  layout for Arabic and Hebrew.

## The app

- [x] **Themes.** A choice of looks for the interface.
- [x] **A screensaver.** While paused or idle in the menu, to protect OLED
  screens.
- [ ] **Accessibility.** Larger text, high contrast, and spoken focus if the
  console's text-to-speech can be used.

## Remaining implementation and acceptance

- Surround PCM is implemented. Physical receiver and speaker routing still need verification.
- Picture in picture over other apps needs a verified PS5 cross-title video-plane lifecycle and input contract.
- Opening streams in another player needs a supported receiving player and a documented launch/URL contract.
- Larger text and high contrast are implemented. Spoken focus needs a usable speech API or a separately designed speech engine.
- On-demand browsing and refresh cancellation feedback remain improvement work beyond the original feature checklist.
- The optional monitor-speaker boost is implemented in this candidate; see docs/AUDIO_NORMALIZATION.md. Listening on other devices still needs acceptance.

Completion of a source feature does not establish that every provider, codec,
receiver, or console firmware has passed acceptance.
