# Sharing sources and favorites

Settings → Share with another console offers a one-shot transfer over the home
network. Both consoles must run this version. The receiver chooses Receive; the
sender previews its network sources, favorites and folders, then enters the
receiver's IPv4 address and **all 64 hex digits** of the SHA-256 certificate
fingerprint displayed on that TV. Compare or copy it directly from the receiving
TV, not from a network message. Colon-separated fingerprints are accepted.

The connection uses OpenSSL TLS 1.2 or newer with an ephemeral P-256 certificate.
The sender verifies the complete certificate fingerprint before sending data.
The receiver must approve the incoming connection, then separately review and
approve the library import. An IPv4 address identifies the connecting machine
for that approval; it is not cryptographic sender authentication. Only approve a
transfer that you initiated from a trusted console. Cancel closes the connection;
the listener accepts one connection and expires after three minutes. There is no
discovery, background synchronization, plaintext credential endpoint, or saved
pairing relationship.

Transfers include account credentials, source HTTP settings, and XMLTV overrides
and channel mappings. They are held in memory, never written to a transfer file or
diagnostic log. USB playlists are counted in the preview and omitted: their paths
refer to a drive on the sending console. The built-in public source is already
available on the receiver. Favorite IDs remain unchanged; a favorite whose source
is unavailable will become usable when that source is present again.

Import merges folders and favorites. Accounts with identical source identity
fields are skipped, retaining the receiver's settings. Other accounts receive new
local database IDs. Selected source, parental controls, viewing limits, history,
paired phones and interface settings stay local. The existing profile rollback
journal protects the library and favorites together; a failed import restores the
previous profile, and an interrupted import is recovered on the next launch.

Limits: 128 network sources, 256 folders, 65,536 favorites and folder entries,
4,096 guide mappings per source, and 8 MiB total. Oversized or malformed transfers
are rejected. Only local/private IPv4 destinations are supported. Port 18127 must
be reachable directly; there is no relay, router configuration or Internet mode.

Host tests exercise real loopback TLS, full fingerprint mismatch, receiver approval,
cancellation, bounded parsing, source-ID remapping, duplicate imports and profile
rollback. Native SceNet sockets use OpenSSL BIO callbacks rather than assuming
that SceNet IDs are POSIX file descriptors. Actual two-console TLS, keyboard,
profile-switch and interrupted-import behavior still require native verification;
the roadmap remains unchecked until that acceptance is complete.

The integrated branch passes all 262 UI tests under ASan/UBSan, including sharing,
crash-report export, source checks and viewing limits. Actual SDK compilation caught
the need for an explicit `netinet/in.h` include and native `sockaddr_in.sin_len`;
both are fixed. Native candidate `b68d44a` links successfully (40,357,796 bytes,
SHA-256 `181b1103cebc0e6a0ae649d8778f88e59209b7cd7086a2336440215a30b1a5ec`),
with evidence in `results/integration/console-83`. This confirms compilation and
linking only. The console remained occupied, so no transfer was attempted there.
