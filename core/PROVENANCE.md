# Shared Core source provenance

GPL-2.0-or-later source snapshot copied on 2026-10-24 from the local
OBS Dynamic Stream Title project, `integration/overlay/src`, `common` and
`data/locale`, plus its shared ABI/runtime headers and channel-fields helper.
The upstream project and installed OBS are not modified. Changes here build the
same shared `nexus-twitch-core.dll`, preserving ABI 6 and adding an optional ticker
feed export. No user config, credentials, build outputs or network cache copied.
Unused UI/renderer source is retained for provenance but no OBS Overlay module
or independent visual renderer is built or installed by this Core target.

2026-10-25: optional ABI 6 Hype Train extension added in Core 1.3.0.
The existing base and ticker APIs and the C++ Service object layout remain
unchanged. Hype snapshots contain live Twitch state for the authorized channel;
all callbacks are borrowed, UI-thread-only and scoped to the existing base lease.

Core 1.3.1 adds an optional, separately named Hype preview API for the current Overlays panel TEST. The live ABI and its structures remain unchanged. The preview stream never authorizes, publishes or persists Twitch data.
