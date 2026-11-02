# OBS News Ticker NEXUS 1.0.0

First stable release — 2026-10-25. Windows x64; OBS Studio 32.x, validated with 32.2.2.

## Included

- 17 native Normal/Compact styles, full configured width, editable header and independent band heights.
- Fixed-width Now Playing, ping-pong and fades; automatic input from separately installed Playing NEXUS, or manual text/JSON.
- Shared Twitch events and recorded daily totals, World Weather with 24 default capitals, stable clock/weather layout.
- Hype Train Breaking News in every style, upward expansion without moving the existing ticker, no extra gap, adjustable width/height/colors and blink duration. The Overlays panel TEST is received separately and visibly marked TEST.
- Twitch Core Shared 1.3.1 included; existing compatible NEXUS consumers retain ABI 6.
- Installer and properties in Spanish, English, Italian, French and German. English automatic broadcast labels; custom user text is preserved.
- NEXUS-series installer layout using the supplied original neon sidebar image without modification.

## Requirements and intentional dependency

Automatic music requires Playing NEXUS installed separately, with its current Now Playing bridge and one configured source. Hiding Playing keeps reception alive; deleting its source stops reception. Music detection is intentionally not duplicated inside Ticker.

Free Open-Meteo access is for non-commercial use and requires public attribution. See weather.md. Close OBS before installing or updating.

## Verification of the release

- Local weather checks: 61, no failures.
- Playing public receiver: 14, no failures.
- Twitch observer and TEST separation: 33, no failures.
- General native libobs/D3D11 regression: 216, no failures.
- Hype rendering, 17 styles, anchoring, size, blinking and controlled event flow: 41, no failures.
- Shared Core: 2 CTest suites passed, including legacy ABI clients and the Overlays panel simulation path.
- Isolated real OBS portable smoke: 11 checks passed, normal exit, OBS reported zero memory leaks. This checks the stable binaries, Playing, the Hype preview and activation/height anchoring; it is not a prolonged broadcast test.
- Installer fixtures verify five languages, Unicode folders, running-OBS refusal, update/reinstall, preservation, downgrade refusal and uninstall. Migration from development records preserves scene JSON and credentials. Final checksums accompany the release artifacts.

The five installer language previews were rendered from the actual installer form and reviewed for layout. Sidebar bytes match the supplied source image.

## Known validation limits

A real Hype Train and a prolonged broadcast session remain pending. Controlled events and the panel TEST do not certify delivery of real Twitch events. Previous real VirtualDJ testing is recorded in virtualdj-real-validation.md; this release does not claim a new live DJ session or a full OBS-version matrix.

The installer is unsigned. No GitHub publication, live stream, chat message or account authorization was performed while preparing this release.

## Delivery

`OBS-News-Ticker-NEXUS-1.0.0-Windows-x64-Setup.exe`

`OBS-News-Ticker-NEXUS-1.0.0-Source.zip`

`LEEME-1.0.0.txt` and `SHA256SUMS.txt`

Code GPL-2.0-or-later; corresponding source and retained notices are included in the source ZIP. User settings and the shared Core remain after uninstall.
