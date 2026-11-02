# Artwork provenance and editable captions

All supplied PNG sheets remain unchanged. `broadcast-reference.png` is the supplied
1370x73 news example; `broadcast-designs.png` is the 1536x1024 normal sheet;
`compact-reference.png` is an unchanged copy of the supplied `Seis Compact.png`
(SHA256 2973ce697e740a18a9057acb2bbf6400405ea1acaa72cdbd79ce009c00334911).

Normal styles sample blue-only map/particle areas. Compact/normal music, clock,
weather and event icons sample the documented small rectangles. Artist names,
usernames, news, clock values and weather values are never copied from the sheets.

The six larger Compact headers use individually measured clipping polygons and
retain the actual reference material, borders, reflections and facets. With the
exact default LIVE/ON AIR caption, default Arial reference size/flags and default
opaque caption/panel colors, the reference lettering is retained too. This avoids
replacing the supplied lettering with a different font approximation. Editing
text, font, size, flags, caption color or opacity switches to native editable GDI+
lettering. The previous caption and its shadow are removed from the full caption
rectangle using a four-boundary Coons patch before native text is composited.
An empty caption therefore contains no original lettering. A custom panel color
uses the native material fallback; no brand name is imposed by the artwork.

Glass/Neon right-end materials use the original text-free top/bottom margins and
final twenty pixels, recovering their colored light and rounded frame. Interior
light is reconstructed without sampling artist/event lettering. Dark uses a single
painted end with a gradual fade across the last 120 pixels; in the caption-height
portion only saturated green paint is retained, replacing neutral legacy lettering
with native dark material. A blank-news GPU check verifies no old raid amount
remains. Sport retains its text-free final twenty-pixel sample and angled mask.
All outputs use premultiplied alpha. Header text opacity remains independent of
the panel opacity; these body corrections leave all six header crops unchanged.

The three artwork textures are shared across active source instances and released
after the last instance. No browser, runtime file conversion or external image
service is used. Caption reconstruction occurs in the source shader. The reference
sheets have flattened backdrops, so reconstructed hidden pixels and native alpha
boundaries are not certified pixel-identical to a layered original.
