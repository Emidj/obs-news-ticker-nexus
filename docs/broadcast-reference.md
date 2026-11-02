# Broadcast reference and independent band behavior

## Style groups

### Finishes and original icon artwork â€” 2026-10-25

Reference presets now derive their own heights and font scale from measured
profiles in `src/reference-profiles.h`; a single 42/31 split is no longer applied
to every design. Existing manually configured sizes remain editable. Compact
text uses a constant per-style width ratio, independent of song length.

Music, clock and weather silhouettes are sampled from the supplied sheets;
Compact heart, star, gift and raid icons retain their original outlines and
colored shading. These samples contain no song, actor, clock or weather text.
Monochrome icons follow the selected text color, while colored icons retain
their supplied colors. Event opacity follows the lower text band separately.
All three reference textures are shared by active ticker sources and freed
after the final source is destroyed.

Steel and colored panels have separate finish profiles, with small bevels and
opacity-aware shadows. Business has actual chevron bands instead of filled play
triangles; Breaking has its two red separators before the clock. Map shading
preserves the supplied blue detail instead of filling every shape uniformly.

`scripts/render-reference-review.py` builds 16 reference/native pairs from
unchanged reference pixels and real GPU frames. The catalog fixes its test clock
to 15:04 and uses each reference's example data; production still reads local
time. These comparisons show the remaining differences and are not a visual
acceptance certificate. The reference includes its original backdrop; native
transparency is presented over a neutral background.

The selector separates Normal and Compact. Normal contains News Channel, World
News, Breaking News, Business News, Sports News, Clean Modern, Map, Red Accent and
Light, plus the preserved legacy Basic layout. Compact contains Prime, Glass,
Neon, Sport, Magazine, Dark and Ultra Compact. Changing the group chooses a design
in that group and preserves text, connections and current colors. Explicit palette
and reference actions remain separate. Scene loading derives the group from the
stored style ID, including older scenes without a group setting.

Layouts follow the supplied broadcast and Seis Compact sheets. Light has no map,
subtitle, lower label or glow behind Now Playing. Compact styles use one visual
row: fixed song, clock/weather, then a separate clipped news/event stream. Heart,
star, gift and raid marks in the six Compact layouts use their original colored
artwork; bits and Ultra event marks retain native vector fallbacks. Their text and
amounts come from the same shared Twitch feed. Custom messages remain editable.
Map artwork is restricted to designs that contain it. Static, continuous scroll,
whole-stream fade and dynamic blocks remain available for the news stream.

The native GPU captures and group/serialization checks are in
`validation/styles-catalog-test.log`; the contact sheet is
`validation/styles-catalog.png`. Captures use demonstration content and a real
local clock; weather is manual. Automated functional checks do not establish
pixel identity with the flattened artwork. Source fonts, gloss, shadow contours
and several compact plates still require visual calibration against the sheets.

## Light Style

The design selector also offers Light Style, based on reference 9 of the supplied
broadcast sheet. It uses pale blue/white bands, dark song/news text, blue header
lettering and an angled header end. It shares the native render engine, song
fade and lower motion modes with News Channel. Light Style has no map; its map controls are hidden and saved map preferences are ignored until News Channel is selected. This is an
native layout with editable text.

Selecting the design preserves custom colors; Apply style colors applies its
palette. Apply reference layout retains Light Style when selected and applies
the common reference dimensions and its own palette, preserving editable text
and connections. All colors remain editable. Basic and News Channel retain their
existing IDs and appearance.

The full 1370 x 73 reference supplied on 2026-10-24 is the visual target for
News Channel. Header and subtitle stay editable; NEXUS is only the preview text,
never forced branding. Heights remain separately configurable.

Select News Channel and click **Apply reference layout** to set the reference's
1370 px width, reference-specific band areas, 350 px Now Playing viewport, font/palette,
continuous lower scroll, clock and animated map. Texts and connections are preserved.
This action is explicit; the color-only palette button remains independent.

- Dark upper panel; red raised header, centered light footer and soft side/text shadows.
- Music symbols and feathered red Now Playing light, fixed song viewport and clock.
- Configurable Now Playing width, 600 px default, 80Ã¢â‚¬â€œ7680 px requested width, clipped
  to the remaining layout. Letters do not shrink to fit a long title.
- Song change: 250 ms fade out, hidden GDI texture preparation, 250 ms fade in.
  Latest pending song wins. Paused/stopped/invalid/stale data clears immediately.
- All lower Twitch items and nonblank custom messages join with a spaced bullet.
  Static, Scroll and Whole-band fade use this complete line; only Dynamic blocks
  selects individual items. The previous raid-rotation toggle is no longer needed.
- News motion never applies to Now Playing. Clock and map animation are independent.
- Blue map decor uses the original supplied bitmap's map area, sampled by GPU.
  The bitmap is not used as a complete ticker or for text. A small brightness pulse
  and light sweep animate it without moving the text. Animation can be disabled.
- Weather text is explicitly manual and empty by default. The preview's 23Ã‚Â° Madrid
  is demonstration text, not a live weather observation. Automatic single-city weather is available through Open-Meteo; see weather.md. The native OBS Playing development producer
  is implemented; see now-playing-setup.md for setup and validation boundaries.

Tests use real libobs/D3D11 pixels for fixed upper behavior in all four lower modes,
exact clipping and unchanged letter scale for long titles, both song fade phases,
lower-band independence, joined separators and dynamic-block selection. Reference
frames and timing are captured as `broadcast-preview.rgba` and packaged into GIF.
Earlier phase reports describe the previous behavior and are historical evidence.
# Map animation controls

News Channel exposes animation speed (25Ã¢â‚¬â€œ200%) and intensity (0Ã¢â‚¬â€œ200%). Both
default to 100%, preserving the accepted subtle pulse and light sweep in existing
scenes. Zero intensity leaves a static visible map; the map visibility switch
removes it. Disabling animation freezes its current phase. Speed and intensity
are saved with the source, and changing them preserves the current phase and
both bands' content. Apply reference layout preserves these two preferences.


## Both-category reconstruction review (2026-10-25)
Normal lower text now has a separate size and per-design weights; colored separators are rendered independently using lower-band opacity. Cached segments preserve the joined news metadata and four existing movement modes. Compact badge slopes, placement and body silhouettes are distinct, including Sport's detached panels and Dark's angled split. Original one-pixel Compact frame highlights are sampled only outside the lettering area; a generic PNG-interior extension experiment was discarded because it distorted the artwork. Current comparison pairs remain visual review evidence, not a declaration of matching or completion. See validation/both-categories-reconstruction-report.md.

Ultra now samples all five small event icons from the original normal sheet, formats typed events as FOLLOW/GIFTED/SUB/RAID/CHEER in reference order and retains editable custom messages. Normal stream segmentation shares the existing cache and motion engine with Compact while leaving the full joined text in source metadata.

## Marked screenshot corrections (2026-10-25)
World News intentionally omits the original left extension/globe at the user's request: its measured crop is now X=422, width=1082. Breaking's tall header and two diagonal right-end bars, Sports' asymmetric LIVE trapezoid, Clean's widened slash, Red's lower-band fade and Light's under-header backing were revised. Compact headers have separate inset plates and carriers. The latest paired captures are review artifacts; both groups still have unresolved visual differences.

## Lines, cuts and overlapping outlines (2026-10-25)
Sports accents are parallel with navy lower fading. Map's header and sampled geography share an arrow-shaped clipping polygon, with continuous navy backing below. Compact body frames are occluded by the header carrier, preventing the former ghost capsule outline in Prime/Glass/Neon. Prime accent spacing and Dark's wider carrier and soft right bloom were revised. See validation/lines-cuts-overlap-report.md and regenerated paired captures for scope and remaining visual differences.

## Replacement of the six Compact badge implementations (2026-10-25)
The old Compact badge shader has been removed. Default reference captions retain
the actual art lettering only under the exact reference font/color/opacity settings;
editing any of those switches to native text. Source caption rectangles and shadows
are removed with a Coons patch before edited text is drawn. All other stream data
is native/cached. Header geometry/material clipping is per design, with long-body
frames occluded by the carrier. See data/graphics/README.md for conditions and
validation/header-rebuild-final-report.md for the new review evidence.
