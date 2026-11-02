"""Build visual QA pairs from unchanged reference pixels and real GPU captures."""
from pathlib import Path
import re
import json
from PIL import Image, ImageDraw

project = Path(__file__).resolve().parents[1]
out = project / "validation"
sheets = [Image.open(project / "data/graphics/broadcast-designs.png").convert("RGB"),
          Image.open(project / "data/graphics/compact-reference.png").convert("RGB")]
profiles = {}
source = (project / "src/reference-profiles.h").read_text(encoding="utf-8")
for name, values in re.findall(r"\[STYLE_(\w+)\]=\{([^}]+)\}", source):
    profiles[name.lower()] = list(map(int, values.split(",")[:6]))
groups = {
    "normal": ["news", "world", "breaking", "business", "sports", "clean", "map", "red", "light"],
    "compact": ["prime", "glass", "neon", "sport", "magazine", "dark", "ultra"],
}
measurements = []
for group, names in groups.items():
    pairs = []
    for name in names:
        x, y, width, top, bottom, above = profiles[name]
        sheet = sheets[0 if group == "normal" or name == "ultra" else 1]
        target = sheet.crop((x, y-above, x+width, y+top+bottom))
        native = Image.open(out / f"style-{name}.png").convert("RGBA")
        inset = 1 if group == "compact" else 5
        native = native.crop((15, inset, native.width-15, native.height-inset))
        target = target.resize((native.width, round(target.height*native.width/target.width)), Image.Resampling.LANCZOS)
        target.save(out / f"reference-{name}.png")
        height = 54+target.height+native.height
        pair = Image.new("RGB", (1400, height), (25, 29, 35))
        draw = ImageDraw.Draw(pair)
        draw.text((15, 4), f"{name} | REFERENCIA", fill="white")
        pair.paste(target, (30, 20))
        draw.text((15, 26+target.height), "RENDER NATIVO (datos de ejemplo)", fill="white")
        pair.paste(native, (30, 44+target.height), native)
        pair.save(out / f"reference-review-{name}.png")
        pairs.append(pair)
        measurements.append({"style": name, "group": group, "reference_bounds": [x,y-above,width,top+bottom+above],
                             "scaled_reference_height": target.height, "native_height": native.height})
    total = Image.new("RGB", (1400, sum(p.height for p in pairs)), (25,29,35))
    y = 0
    for pair in pairs:
        total.paste(pair, (0,y)); y += pair.height
    total.save(out / f"reference-review-{group}.png")
(out / "reference-review-measurements.json").write_text(json.dumps(measurements, indent=2), encoding="utf-8")
print("Saved 16 reference/native QA pairs; height measurements are not pixel-similarity scores.")
