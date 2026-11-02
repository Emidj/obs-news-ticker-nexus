# Twitch Core Shared 1.3.1 — News Ticker 1.0.0

## One shared runtime

The installer includes `nexus-twitch-core.dll` in OBS's `bin/64bit`. The ticker
retains one runtime owner and one observer for every ticker source combined. Other
compatible NEXUS plugins share the same runtime, OAuth session and EventSub
subscriptions. No Dynamic Stream Title or Overlay installation is required.
The existing ABI 6 structures are unchanged. The optional version-1 ticker export
adds a fixed C snapshot and callbacks tied to the existing lease.

Startup waits for OBS FINISHED_LOADING. An existing DLL gets a Windows module
reference; otherwise it is loaded only from the OBS executable directory, not a
PATH search. Version/ABI are checked before retain. A known legacy private Overlay
blocks startup. EXIT detaches and releases this owner; unload drops its module
reference. Remaining owners continue to run. Only the UI thread calls Core APIs.
Graphics threads copy an in-memory snapshot; no network or disk work occurs there.

The shared protected session path is
`plugin_config/nexus-twitch-core/twitch-session.bin` in the selected OBS config.
Standalone Core stores observed history beside it in `ticker-history.json`.
If another owner supplies its existing central event engine, its established
history store remains in use; no second event engine or copy of its counters is made.

## Connection

Click **Connect Twitch (shared connection)** in source properties, then complete
authorization in Twitch yourself. No browser opens merely by loading the plugin.
An existing enabled shared session can reconnect as it did in previous Core.
No token, credential or HTTP header crosses the ticker ABI.
Only the exact official HTTPS authorization endpoint can be opened by the ticker.
Chat writing and broadcast editing are not requested by ticker-only authorization.
The shared service still requests its existing read/event scopes for chat, follows,
subscriptions, bits and Hype Train. Other plugins can request optional permissions
through their own explicit controls. Disconnect affects every shared consumer.

## Display and recorded totals

Enable any of: last follow, last sub, last cheer, last gift, last raid, recorded
daily subs and recorded daily bits. Anonymous gifts/cheers stay anonymous. Raids
include their reported viewer count. Display names preserve UTF-8.

All enabled items and nonblank custom messages join a single news line with spaced
bullets, in follow/sub/cheer/gift/raid/daily-subs/daily-bits/custom order. Only Dynamic
blocks shows items one by one. Static, Scroll and Whole-band fade use the complete
joined line. All switches default off. Lower motion never affects Now Playing.
Without available items the custom messages remain. New event IDs restart the
sequence even for the same actor. Connection/account loss clears visible events.
The existing editable label, colors and native renderer are reused. The previous
raid-rotation switch is no longer exposed because all selected items participate.

Daily subs count new subscription notifications and gift totals once. Gift-recipient
notifications and resubscription chat messages do not add another purchase. Bits
sum observed cheers. Deduplication and normalization remain in the central engine.
Counters use the Windows local calendar day and preserve their saturation flag.
The feed exposes zero for a different day and the provider checks rollover every
30 seconds. Last events are retained at rollover. A bounded local history restores
last events and observed totals quietly after restart. Invalid files are protected
from overwrite. History is exposed only for the active authorized account.

These are recorded totals, not complete Twitch totals: offline periods and events
Twitch does not provide cannot be reconstructed. Changing the Windows timezone
does not reconstruct old observations under another calendar. Twitch's distinction
between subscription, gift and resub chat events is documented in its
[EventSub subscription types](https://dev.twitch.tv/docs/eventsub/eventsub-subscription-types/).

## Installer policy

The Windows EXE supports install/update and uninstall and requires OBS closed.
It inspects inert PE contracts without executing an installed DLL. Portable OBS
is isolated from global plugins; normal installs also inspect global consumer
folders and the shared consumer registry. ABI/version ranges must admit the chosen
Core. An existing compatible newer Core is retained. The bundled identical Core
is reused. A recognized older build or a registered older Core can be updated only
when all consumer requirements remain satisfied. Unknown Core builds, mismatched
registry hashes and same-version/different-content DLLs are preserved and rejected.

Payload hashes are verified; replacements get backups and writes roll back on
failure. Uninstall removes only matching owned files and keeps shared Core,
credentials, histories, backups, changed files and unrelated plugins. It does not
change scenes, presets or account settings. The same EXE can be reopened to uninstall;
no separate Windows Apps registration is created. The development installer is unsigned.

## Validation boundary

The tests cover ABI-6 legacy clients and owner lifetime, ticker-only authorization
scopes, typed data, anonymous actors, duplicate/sub/gift rules, saved histories,
day rollover snapshots, malformed-file preservation, the native lower-band display
and installer operations. An asInvoker test executable uses exactly the installer
code/resources; the distributed EXE requests elevation for Program Files targets.
Portable OBS confirms independent startup and the connection controls without
authorizing an account. Real Twitch delivery, reconnect during a live session and
long-running streaming remain pending. Automated events are fixtures, not live raids.

Build: `scripts/build-core.ps1`, `scripts/build.py`, `installer/build.ps1`.
Source provenance and license: `core/PROVENANCE.md`, `core/LICENSE`.


## Core 1.3.1 Hype extension

The optional `nexus_twitch_core_get_hype_api` export binds a read-only Hype snapshot to the same ABI 6 lease. It delivers level, progress, goal and expiry for live Twitch state only. The base and ticker-feed layouts are unchanged. Ticker now declares minimum Core 1.3.1; consumers compatible with ABI 6 and Core 1.x retain their existing interfaces. Installation verifies contracts and backs up recognized previous binaries. See Hype-Train.md in the installed package, or hype-train.md in the source project.
