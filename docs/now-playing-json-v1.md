# Local Now Playing JSON contract â€” v1

This is the input contract implemented by News Ticker 0.7.1. The native OBS Playing development module now publishes the same schema through an OBS source procedure; the legacy Companion is unchanged.

## Payload

```json
{
  "schema": "nexus.now-playing.v1",
  "state": "playing",
  "updated_at": 1790000000.0,
  "artist": "Example artist",
  "title": "Example track"
}
```

The timestamp above is illustrative and will be rejected as expired. A producer must use the **current Unix UTC time in seconds**, updating it with a heartbeat even while the track remains unchanged.

- Encoding: UTF-8 JSON, without BOM, maximum 16384 bytes.
- `schema`: exact string `nexus.now-playing.v1`.
- `state`: `playing`, `paused`, `stopped`, `waiting` or `unavailable`.
- `updated_at`: positive finite number in Unix seconds. More than 5 seconds ahead of the receiving computer is rejected.
- In `playing`, `title` must be a non-blank string of at most 512 UTF-8 bytes. Optional `artist` must be a string of at most 256 bytes. ASCII control characters are rejected. Empty/whitespace-only artist produces title-only output.
- Active output: `artist â€” title` or just `title`.
- `paused`, `stopped`, `waiting` and `unavailable` have no active track text; any supplied title/artist is ignored.
- Unknown fields are ignored. There is no artwork, duration, deck selection or audible/master inference in this contract.

## Publishing

Write a temporary file in the destination directory, flush/close it, then replace the destination atomically. Publish explicit playback changes promptly. A heartbeat every 2 seconds is suitable for the default 15-second expiry; choose a period comfortably below the configured expiry. If the producer stops refreshing the file, the receiver removes the active title automatically.

The included `examples/publish-now-playing.py` is a **one-shot development publisher**, not a DJ detector or Companion connector. Example, run manually with a chosen local path:

```powershell
python examples/publish-now-playing.py --output "C:\MyLocalFolder\track.json" --state playing --artist "Demo" --title "[TEST] JSON receiver"
```

One write naturally expires. To send an explicit pause:

```powershell
python examples/publish-now-playing.py --output "C:\MyLocalFolder\track.json" --state paused
```

## Receiver

In the source properties choose **Upper band content â†’ Now Playing â€” local JSON file** and select an absolute local-disk path. Relative and UNC paths are rejected; mapped network drives are not supported. Unicode paths are supported up to the receiver's 1023-wide-character path limit.

The plugin uses one background worker, sharing a parsed snapshot for consumers of the same normalized absolute path. Up to 64 distinct configured paths are supported. It reads active files approximately every 500 ms, with overlapped reads and cancellation; rendering does not open/read/parse files. With no subscriptions the worker waits without polling files. This is an implementation description, not a latency or resource benchmark.

The source checks the snapshot approximately every 250 ms and applies its own 3â€“300 second expiry (15 seconds by default). Missing, unreadable, invalid, future-dated, expired or inactive input clears the track. Manual fallback is **off by default** and must be enabled explicitly. Status is visible in the properties, without inserting diagnostic messages into the broadcast.

Changing an external track restarts only the upper text's animation. The lower message sequence and clock remain independent. Manual text is preserved and restored when returning to Manual mode. Source settings retain only the input configuration, not an external track as permanent source content.

## Automatic native OBS Playing integration

Choose the OBS Playing automatic upper-band mode. The updated native development source registers `void nexus_now_playing_v1(out string json)`, which returns an owned snapshot using this schema. The receiver queries at 4 Hz outside rendering. One active native source is required; multiple sources produce an explicit ambiguity status. Older modules without the procedure report that an update is needed.

The publisher reads the existing immutable publication; it adds no DJ detector, credentials or private test hooks. The current Playing development project contains the publisher and a CTest regression. The stable Playing installer and normal installed OBS have not been replaced.

Both actual modules passed seven lifecycle/track checks in portable OBS with fictitious demo tracks on 2026-10-25. This does not certify a real DJ application session.
