# Shared Twitch receiver and lower-band latest raid

Historical phase notes below. Current implementation and installer policy are in
[Twitch Core 1.2.0](twitch-core.md); the ticker now retains the runtime independently
and receives all five supporter event types through an optional ABI-6 extension.

The native plugin optionally observes `nexus-twitch-core.dll` already loaded by
an existing owner. OBS post-load registers a frontend lifecycle callback; the
observer attaches on the UI thread at `OBS_FRONTEND_EVENT_FINISHED_LOADING`, after
every owner's post-load initialization. Module post-load order alone does not
guarantee the runtime exists. In headless libobs without a frontend, the controlled
fixture may attach directly at post-load. The observer never loads a DLL
from a search path, retains/starts the runtime, opens OAuth, disconnects Twitch,
sends chat messages or changes channel settings. No Core installer is provided yet.

The ABI 6 header is copied unchanged from OBS Dynamic Stream Title's
`shared/twitch-core-api.h` (GPL-2.0-or-later). Upstream files were not modified.
The observer checks ABI, structure size and all three required function pointers.
All provider access and callbacks stay on the UI thread; readers receive a locked
copy. Provider shutdown invalidates the lease before client teardown.

Properties show the connection state and latest accepted raid login. The source
checks its shared snapshot every 250 ms and emits OBS's properties-refresh signal
when status, account, login or event ID changes, even with raid display disabled.
An unchanged snapshot does not emit repeated refreshes. Enable **Show the latest Twitch
raid in the lower band** to replace the custom messages with the latest accepted
raid login. Disabled by default. The label remains editable and is not overwritten.
Enable **Rotate raid with custom messages** to include the raid followed by each
nonblank custom message in Blocks and Alternating modes. This is optional and
disabled by default, preserving the existing pinned latest-raid behavior.
Static and Scroll still show the latest raid while available. Empty custom slots
are skipped; a raid by itself remains a valid sequence. New raids take the first
position and restart the lower animation. Invalidating the raid restarts the
custom sequence at its first message. Both options survive settings save/load.

The translated message uses the source's lower text color/font and existing
static, scroll, dynamic-block or alternating animation. A new event ID restarts the
lower animation. Custom message rotation pauses in the default pinned mode.
Without a raid, the saved custom messages resume. The latest raid has no timeout;
it remains until replaced, disabled or invalidated by connection/channel state.
It is not an event queue and is not restored from earlier OBS sessions.
If Core was absent when OBS finished loading, restart OBS after enabling its owner.
There is no automatic retry or independent authorization window in this increment.

Accept raids only after an authorized active state for the same account and
provider instance. Reject malformed messages/logins. Deduplicate the most recent
32 event IDs. Account changes, instance changes, authorization loss and provider
shutdown clear the stored event. No historical raid is reconstructed or persisted.

ABI 6 exposes raid and Hype Train events, but no raid viewer count, follows,
subscriptions, cheers or gifts. Those require an agreed compatible Core extension.
Do not infer missing values or count fixture events as real Twitch validation.

Validation: the fake-provider executable checks isolation, borrowed-data copying,
deduplication, malformed input and lifecycle paths with mutation entry points null.
The libobs/GPU suite loads a separate fake ABI DLL inside its own test process,
invokes module post-load and verifies the real native lower text for raid input,
all four animation modes, deduplication, channel isolation, opt-out and disconnect.
The fake frontend regression checks deferred attachment, finished-loading
attachment and callback removal at unload. Properties signal delivery,
no-change suppression, same-actor new events,
opt-out updates and settings save/load are covered by the same fixture.
The fixture DLL stays under `build/twitch-fixture` and is never distributed or
copied into portable OBS. Actual shared Core callbacks in
OBS with an authorized account and a real incoming raid remain pending. A separate
portable OBS UI run verified real Core 1.1.0 ABI 6 loading with Dynamic Stream Title
as owner, successful observer attachment and the waiting-for-authorization state.
