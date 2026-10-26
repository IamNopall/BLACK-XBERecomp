import math
from PIL import Image, ImageDraw, ImageFilter

def create_premium_tactical_cursor(accent_rgb=(255, 175, 20), style_name="amber"):
    # 1024x1024 master canvas for 4x supersampling -> 256x256
    S = 1024
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Hotspot is exactly at (0, 0)
    outer = [
        (0, 0),              # Tip (Click Hotspot)
        (0, 730),            # Left blade edge
        (210, 550),          # Inner left notch
        (370, 930),          # Tail bottom-left
        (500, 870),          # Tail bottom-right
        (340, 490),          # Inner right notch
        (650, 490),          # Right wingtip
    ]

    # Drop Shadow (offset +32, +32, clamped so it never covers the (0,0) tip)
    shadow_img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sdraw = ImageDraw.Draw(shadow_img)
    s_pts = [(x + 32, y + 32) for x, y in outer]
    sdraw.polygon(s_pts, fill=(0, 0, 0, 180))
    shadow_img = shadow_img.filter(ImageFilter.GaussianBlur(24))
    img.alpha_composite(shadow_img)

    # 1. Outer Border (Crisp Pure White / Diamond Polished Edge)
    draw.polygon(outer, fill=(255, 255, 255, 255))

    # 2. Outer Bevel Trim (Dark Steel Titanium)
    b_pts = [
        (16, 30),
        (16, 680),
        (210, 535),
        (365, 895),
        (475, 845),
        (325, 475),
        (605, 475),
    ]
    draw.polygon(b_pts, fill=(45, 48, 54, 255))

    # 3. Main Body (Deep Matte Gunmetal / Black Carbon)
    c_pts = [
        (30, 56),
        (30, 630),
        (210, 518),
        (360, 860),
        (450, 820),
        (310, 460),
        (560, 460),
    ]
    draw.polygon(c_pts, fill=(18, 20, 24, 255))

    # 4. Top Spine / Metallic Highlights
    draw.line([(34, 62), (34, 610)], fill=(75, 82, 92, 255), width=6)
    draw.line([(34, 62), (540, 455)], fill=(65, 72, 80, 255), width=6)

    # 5. Central Tactical Chevrons (BLACK HUD reticle motif)
    ch_pts1 = [
        (120, 220),
        (200, 300),
        (180, 320),
        (120, 260),
        (60, 320),
        (40, 300),
    ]
    # Glow layer for chevron
    glow_img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gdraw = ImageDraw.Draw(glow_img)
    gdraw.polygon(ch_pts1, fill=(*accent_rgb, 200))
    
    ch_pts2 = [(x, y + 90) for x, y in ch_pts1]
    gdraw.polygon(ch_pts2, fill=(*accent_rgb, 200))
    glow_img = glow_img.filter(ImageFilter.GaussianBlur(12))
    img.alpha_composite(glow_img)

    # Sharp solid chevron core
    draw.polygon(ch_pts1, fill=(*accent_rgb, 255))
    draw.polygon(ch_pts2, fill=(*accent_rgb, 255))
    # Bright center pip
    draw.polygon([(120, 235), (170, 285), (120, 265), (70, 285)], fill=(255, 255, 255, 220))

    # 6. Firearm slide serrations on the tail stem
    for i in range(4):
        offset = i * 45
        p1 = (260 + i * 28, 630 + offset)
        p2 = (355 + i * 28, 585 + offset)
        # Recessed dark slot
        draw.line([p1, p2], fill=(8, 10, 12, 255), width=18)
        # Accent indicator inside slot
        draw.line([(p1[0] + 8, p1[1] - 4), (p2[0] - 8, p2[1] + 4)], fill=(*accent_rgb, 255), width=8)

    # Ensure the tip at (0, 0) is pure crisp white
    draw.point((0, 0), fill=(255, 255, 255, 255))

    # Resize to 256x256 with Lanczos
    return img.resize((256, 256), Image.Resampling.LANCZOS)

def main():
    cur_amber = create_premium_tactical_cursor(accent_rgb=(255, 175, 20), style_name="amber")
    cur_amber.save("scratch/cursor_amber.png")

    cur_crimson = create_premium_tactical_cursor(accent_rgb=(255, 40, 60), style_name="crimson")
    cur_crimson.save("scratch/cursor_crimson.png")

    cur_cyan = create_premium_tactical_cursor(accent_rgb=(30, 210, 255), style_name="cyan")
    cur_cyan.save("scratch/cursor_cyan.png")

    # High-res comparison preview board
    preview = Image.new("RGBA", (960, 400), (18, 20, 24, 255))
    pdraw = ImageDraw.Draw(preview)
    pdraw.rectangle([(480, 0), (960, 400)], fill=(230, 235, 240, 255))
    pdraw.line([(480, 0), (480, 400)], fill=(80, 85, 95, 255), width=2)

    styles = [
        (cur_amber, 40, 40, "Amber HUD"),
        (cur_crimson, 180, 40, "Black Ops Crimson"),
        (cur_cyan, 320, 40, "Ghost Tactical"),
    ]

    for cur, x, y, label in styles:
        s_preview = cur.resize((110, 110), Image.Resampling.LANCZOS)
        preview.alpha_composite(s_preview, (x, y + 40))
        preview.alpha_composite(s_preview, (x + 480, y + 40))

        s_ingame = cur.resize((36, 36), Image.Resampling.LANCZOS)
        preview.alpha_composite(s_ingame, (x + 35, y + 210))
        preview.alpha_composite(s_ingame, (x + 480 + 35, y + 210))

    preview.save("scratch/cursor_preview_v2.png")
    print("Zero-hotspot tactical cursors created successfully!")

if __name__ == "__main__":
    main()
