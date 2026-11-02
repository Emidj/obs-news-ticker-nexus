# OBS News Ticker NEXUS 1.0.0

Native ticker source for OBS Studio on Windows x64. First stable release, with 17 Normal/Compact designs and one shared rendering engine.

**[Download the official stable release](https://github.com/Emidj/obs-news-ticker-nexus/releases/tag/v1.0.0)**

## Preview

![Ticker overview](docs/images/ticker-overview.png)

*Actual renderer output from a controlled development demonstration: header, Now Playing, clock/weather and lower event/news band. Sample tracks and event data are not evidence of a live broadcast.*

![Complete ticker in the Light style](docs/images/ticker-light.png)

*Complete ticker in the Light style: Now Playing, weather, custom header and lower Twitch event/message band. Actual renderer output with controlled sample data; not a live event capture.*

## Install

1. Close OBS Studio.
2. Run `OBS-News-Ticker-NEXUS-1.0.0-Windows-x64-Setup.exe` and select your OBS folder.
3. Open OBS and add **OBS News Ticker NEXUS** as a source.

The installer follows the NEXUS series layout, with the original supplied sidebar artwork. It supports English, Spanish, Italian, French and German. Settings follow the OBS language; automatic broadcast labels are English. Custom texts are preserved.

Updating preserves scenes, source settings and Twitch credentials. To uninstall, close OBS and run the same installer, then choose Uninstall. Unrelated files, configuration and the shared Core are preserved.

## Features

- 10 Normal designs including Basic, and 7 Compact designs; full configured width.
- Independent upper/lower band heights, editable header, fonts, colors and opacity.
- Fixed-width Now Playing, optional ping-pong movement and song-change fades.
- Joined lower news/messages, continuous scrolling, dynamic blocks and whole-band fades.
- Shared Twitch events: follows, subscriptions, cheers, gifts, raids and recorded daily totals.
- Fixed clock/weather regions, with weather before the outer clock when aligned right.
- Automatic World Weather with 24 default capitals, editable ordered city list and city-change fades.
- Optional short Hype Train Breaking News band above every style: adjustable width/height/colors, configurable message blink, real level/progress/countdown and explicit test previews.

## Playing NEXUS

Automatic music requires the **separately installed Playing NEXUS plugin**, updated with its Now Playing bridge (Playing NEXUS 1.1.0 includes it). Keep one configured Playing source in OBS. It may be hidden: the ticker keeps receiving current songs. Deleting that source stops reception. Ticker does not include an independent music detector. Manual text and the documented local JSON input remain available.

[Playing setup](docs/now-playing-setup.md) · [JSON format](docs/now-playing-json-v1.md)

## Twitch and Hype Train

The installer includes **Twitch Core Shared 1.3.1, ABI 6**. Compatible NEXUS plugins share its connection, authorization and event state. Ticker does not require another NEXUS plugin for Twitch. Daily counters represent recorded events rather than guaranteed complete Twitch totals.

Enable Breaking News in its own settings section. It grows upward while preserving the ticker baseline and meets the upper band without a gap. Width zero uses 65%; height defaults to 28 px. Blink duration defaults to 700 ms per cycle. The Overlays panel TEST is received separately and marked TEST; ending it restores the real state or the local preview if still enabled.

[Twitch Core](docs/twitch-core.md) · [Breaking News setup](docs/hype-train.md)

## Weather

The selected free Open-Meteo mode is for non-commercial use. Include public attribution: **Weather data by Open-Meteo — https://open-meteo.com/ — CC BY 4.0**. The ticker omits provider text from the band; properties and the guide retain its terms. No temperature is invented when data is unavailable or expired.

[Weather setup](docs/weather.md)

## Validation and limits

Validated with OBS Studio 32.2.2, native libobs/D3D11 checks, isolated portable OBS and installer fixtures. Earlier real VirtualDJ testing is documented separately. A real Hype Train and a prolonged broadcast session remain pending; simulations are not presented as evidence of real Twitch events. See the [1.0.0 release notes](docs/RELEASE-1.0.0.md) for final evidence.

The installer is not digitally signed. No broadcast, chat message or authorization is started during packaging tests.

## Source and license

Corresponding source is delivered as `OBS-News-Ticker-NEXUS-1.0.0-Source.zip`. Version comes from `VERSION`; build with `scripts/build.py`, Core with `scripts/build-core.ps1`, then `installer/build.ps1` and `scripts/package-source.ps1`. The SDK/toolchain requirements are checked by these scripts. Test plugins are not shipped in the installer.

Code: GPL-2.0-or-later. Shared Core provenance and retained upstream notices: [core/PROVENANCE.md](core/PROVENANCE.md) and [LICENSE](LICENSE).

**a plugin from [DjEmiMas](https://discord.gg/dhcqZPDYh)**
