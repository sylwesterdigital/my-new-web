#!/usr/bin/env python3
"""
make_app_assets.py
- Creates assets/icon.png (1024x1024) from a source image (PNG/JPG), or auto-generates a placeholder.
- Builds assets/AppIcon.iconset/* PNGs and assets/icon.icns (requires macOS 'iconutil').
Usage:
  python3 scripts/make_app_assets.py [path/to/source.png|jpg]
"""
import sys, os, io, subprocess
from pathlib import Path

# Pillow is bundled in your venv by the build script
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"
ICON_PNG = ASSETS / "icon.png"
ICONSET = ASSETS / "AppIcon.iconset"
ICNS = ASSETS / "icon.icns"

SIZES = [
    (16, 16, "icon_16x16.png"),
    (32, 32, "icon_16x16@2x.png"),
    (32, 32, "icon_32x32.png"),
    (64, 64, "icon_32x32@2x.png"),
    (128, 128, "icon_128x128.png"),
    (256, 256, "icon_128x128@2x.png"),
    (256, 256, "icon_256x256.png"),
    (512, 512, "icon_256x256@2x.png"),
    (512, 512, "icon_512x512.png"),
    (1024, 1024, "icon_512x512@2x.png"),
]

def ensure_dir(p: Path):
    p.mkdir(parents=True, exist_ok=True)

def load_or_generate_icon(src: Path | None) -> Image.Image:
    W = H = 1024
    canvas = Image.new("RGBA", (W, H), (20, 22, 26, 255))

    if src and src.exists():
        try:
            img = Image.open(src).convert("RGBA")
            # fit inside 85% of canvas while keeping aspect
            max_side = int(W * 0.85)
            w, h = img.size
            scale = min(max_side / w, max_side / h)
            new = img.resize((int(w*scale), int(h*scale)), Image.LANCZOS)
            # center
            ox = (W - new.size[0]) // 2
            oy = (H - new.size[1]) // 2
            canvas.alpha_composite(new, (ox, oy))
            return canvas
        except Exception:
            pass  # fall back to placeholder

    # placeholder: gradient + “TTS”
    for y in range(H):
        c = int(40 + 40 * y / H)
        ImageDraw.Draw(canvas).line([(0, y), (W, y)], fill=(c, c, c, 255))

    draw = ImageDraw.Draw(canvas)
    text = "TTS"
    # Try a reasonable system font; fallback if missing
    font = None
    for name in ["SFNS.ttf", "Arial.ttf", "Helvetica.ttf"]:
        try:
            font = ImageFont.truetype(name, 380)
            break
        except Exception:
            continue
    if font is None:
        font = ImageFont.load_default()

    tw, th = draw.textbbox((0,0), text, font=font)[2:]
    draw.text(((W-tw)//2, (H-th)//2), text, fill=(240,240,240,255), font=font)
    return canvas

def main():
    ensure_dir(ASSETS)
    src = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else None
    if src and not src.exists():
        print(f"Source not found: {src} — using placeholder", file=sys.stderr)
        src = None

    icon = load_or_generate_icon(src)
    icon.save(ICON_PNG)

    ensure_dir(ICONSET)
    # Create all sizes from icon.png
    base = Image.open(ICON_PNG).convert("RGBA")
    for w, h, name in SIZES:
        out = base.resize((w, h), Image.LANCZOS)
        out.save(ICONSET / name)

    # Build .icns (macOS)
    try:
        subprocess.run(["iconutil", "-c", "icns", str(ICONSET), "-o", str(ICNS)], check=True)
    except Exception as e:
        print(f"Warning: failed to create .icns via iconutil: {e}", file=sys.stderr)

    print(f"Created: {ICON_PNG}")
    print(f"Iconset: {ICONSET}")
    if ICNS.exists():
        print(f"ICNS:    {ICNS}")

if __name__ == "__main__":
    main()
