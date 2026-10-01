"""Generate checked-in Windows ICO/PNG assets. Developer-only: pip install pillow."""
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parent
scale = 16
image = Image.new("RGBA", (64 * scale, 64 * scale))
draw = ImageDraw.Draw(image)
def box(values):
    return tuple(round(v * scale) for v in values)
draw.rounded_rectangle(box((1, 1, 63, 63)), radius=14 * scale, fill="#142b43")
draw.arc(box((16, 11, 48, 43)), 180, 360, fill="#eef8ff", width=5 * scale)
for x in (16, 48):
    draw.line(box((x, 27, x, 35)), fill="#eef8ff", width=5 * scale)
for x in (11, 42):
    draw.rounded_rectangle(box((x, 29, x+11, 50)), radius=5.5 * scale, fill="#36d1dc")
draw.line(box((25, 43, 39, 43)), fill="#eef8ff", width=3 * scale)
draw.ellipse(box((27.5, 38.5, 36.5, 47.5)), fill="#ffb65c")
image = image.resize((256, 256), Image.Resampling.LANCZOS)
image.save(root / "volumeedit.png")
image.save(root / "volumeedit.ico", sizes=[(n,n) for n in (16,20,24,32,40,48,64,128,256)])
print("Generated headphone and gain-control icon at 9 Windows sizes.")
