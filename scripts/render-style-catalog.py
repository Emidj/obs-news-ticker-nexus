"""Package measured native GPU frames as PNG previews, without editing references."""
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1] / "validation"
frames = {}
for line in (root / "styles-catalog.tsv").read_text().splitlines():
    name, width, height = line.split("\t")
    frame = Image.frombytes("RGBA", (int(width), int(height)),
                            (root / f"style-{name}.rgba").read_bytes())
    frame.save(root / f"style-{name}.png")
    frames[name] = frame

groups = {
    "normal": ["news", "world", "breaking", "business", "sports", "clean", "map", "red", "light"],
    "compact": ["prime", "glass", "neon", "sport", "magazine", "dark", "ultra"],
}
for group, names in {**groups, "catalog": groups["normal"] + groups["compact"]}.items():
    heights = [frames[name].height + 38 for name in names]
    canvas = Image.new("RGB", (1400, sum(heights)), (30, 34, 40))
    draw = ImageDraw.Draw(canvas)
    row_y = 0
    for index, name in enumerate(names):
        draw.text((15, row_y + 3), name, fill="white")
        canvas.paste(frames[name], (15, row_y + 24), frames[name])
        row_y += heights[index]
    canvas.save(root / f"styles-{group}.png")
