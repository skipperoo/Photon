#!/usr/bin/env python3
"""Film-inspired Photon presets (Kodak and Fujifilm stocks).

Writes "<brand> <stock>.json" files into Photon's presets folder (or --out).
Each file carries a "__source" marker (Photon ignores "__" keys when applying
a preset); the script only overwrites files that carry it, never a preset of
yours with the same name.
Image-dependent settings (exposure, white balance, noise reduction), Creative
(grain, vignette), Detail, geometry and lens corrections are left out on
purpose: applying a preset leaves them as they are. Every other look key is
set, so a preset replaces a previous look. Camera Look is set to 50.

Usage:
    scripts/make_film_presets.py            # into Photon's presets folder
    scripts/make_film_presets.py --out DIR  # somewhere else
"""
import argparse
import json
import os
import platform

SOURCE = "photon-film-presets"
COLORS = ["Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta"]

def curve(*pts):
    return [{"x": float(x), "y": float(y)} for x, y in pts]

IDENTITY = curve((0, 0), (1, 1))

def preset(contrast=0, highlights=0, shadows=0, whites=0, blacks=0,
           vibrance=0, saturation=0, hsl=None, bw=None, cg=None, calib=None,
           luma=IDENTITY, red=IDENTITY, green=IDENTITY, blue=IDENTITY,
           clarity=0, profile="normal"):
    p = {
        "__source": SOURCE,
        "profile": profile,
        "cameraLookAmount": 50.0,
        # Contrast is stored as 1 + slider / 200 (slider -100..100).
        "contrast": 1.0 + contrast / 200.0,
        "highlights": float(highlights), "shadows": float(shadows),
        "whites": float(whites), "blacks": float(blacks),
        "vibrance": float(vibrance), "saturation": float(saturation),
        "toneCurveMode": "manual",
        "toneCurveLuma": luma, "toneCurveRed": red,
        "toneCurveGreen": green, "toneCurveBlue": blue,
        "toneCurveHighlights": 0.0, "toneCurveLights": 0.0,
        "toneCurveDarks": 0.0, "toneCurveShadows": 0.0,
        "toneCurveSplitShadows": 25.0, "toneCurveSplitMid": 50.0,
        "toneCurveSplitHighlights": 75.0,
        "clarity": float(clarity), "dehaze": 0.0, "structure": 0.0, "centre": 0.0,
        "cgBalance": 0.0, "cgBlending": 50.0,
    }
    hsl = hsl or {}
    for c in COLORS:
        h, s, l = hsl.get(c, (0, 0, 0))
        p[f"hsl{c}Hue"], p[f"hsl{c}Saturation"], p[f"hsl{c}Luminance"] = float(h), float(s), float(l)
    bw = bw or {}
    for c in COLORS:
        p[f"bwMix{c}"] = float(bw.get(c, 0))
    cg = cg or {}
    for region in ("Shadows", "Midtones", "Highlights"):
        h, s, l = cg.get(region, (0, 0, 0))
        p[f"cg{region}Hue"], p[f"cg{region}Saturation"], p[f"cg{region}Luminance"] = float(h), float(s), float(l)
    calib = calib or {}
    p["calibShadowsTint"] = float(calib.get("shadowsTint", 0))
    for prim in ("Red", "Green", "Blue"):
        p[f"calib{prim}Hue"] = float(calib.get(prim.lower() + "Hue", 0))
        p[f"calib{prim}Sat"] = float(calib.get(prim.lower() + "Sat", 0))
    return p

PRESETS = {
    "Kodak Portra 400": preset(
        contrast=-8, highlights=-20, shadows=15, whites=-5, blacks=5,
        vibrance=5, saturation=-10, clarity=-5,
        luma=curve((0, 0.035), (0.25, 0.23), (0.5, 0.5), (0.75, 0.77), (1, 0.97)),
        red=curve((0, 0), (0.5, 0.51), (1, 1)),
        blue=curve((0, 0.02), (0.5, 0.49), (1, 0.97)),
        hsl={"Red": (4, -5, 0), "Orange": (0, -8, 6), "Yellow": (-6, -10, 0),
             "Green": (-15, -25, 5), "Aqua": (-10, -15, 0), "Blue": (-8, -15, 0),
             "Magenta": (0, -15, 0)},
        cg={"Shadows": (200, 6, 0), "Highlights": (40, 8, 0)},
        calib={"redHue": 5, "blueHue": -5}),
    "Kodak Portra 160": preset(
        contrast=-12, highlights=-15, shadows=10, blacks=4,
        saturation=-15, clarity=-5,
        luma=curve((0, 0.03), (0.25, 0.235), (0.5, 0.5), (0.75, 0.765), (1, 0.975)),
        blue=curve((0, 0.015), (0.5, 0.5), (1, 0.985)),
        hsl={"Orange": (0, -10, 5), "Yellow": (-4, -12, 0), "Green": (-10, -30, 4),
             "Aqua": (-8, -15, 0), "Blue": (-6, -20, 0), "Magenta": (0, -10, 0)},
        cg={"Shadows": (210, 5, 0), "Highlights": (45, 4, 0)}),
    "Kodak Ektar 100": preset(
        contrast=12, highlights=-10, shadows=-5, whites=5, blacks=-8,
        vibrance=10, saturation=15,
        luma=curve((0, 0), (0.25, 0.21), (0.5, 0.5), (0.75, 0.8), (1, 1)),
        red=curve((0, 0), (0.5, 0.515), (1, 1)),
        blue=curve((0, 0), (0.5, 0.49), (1, 1)),
        hsl={"Red": (3, 15, 0), "Orange": (0, 5, 0), "Yellow": (-4, 5, 0),
             "Green": (-5, 10, -5), "Aqua": (0, 5, 0), "Blue": (4, 15, -10)},
        cg={"Shadows": (220, 6, 0), "Highlights": (35, 8, 0)}),
    "Kodak Gold 200": preset(
        contrast=4, highlights=-15, shadows=8,
        vibrance=8, saturation=5,
        luma=curve((0, 0.02), (0.25, 0.235), (0.5, 0.51), (0.75, 0.78), (1, 0.98)),
        red=curve((0, 0), (0.5, 0.52), (1, 1)),
        green=curve((0, 0), (0.5, 0.505), (1, 1)),
        blue=curve((0, 0), (0.5, 0.47), (1, 0.95)),
        hsl={"Red": (0, 5, 0), "Orange": (0, 8, 4), "Yellow": (-8, 10, 5),
             "Green": (-20, -15, 0), "Aqua": (0, -10, 0), "Blue": (-5, -10, 0)},
        cg={"Shadows": (30, 6, 0), "Midtones": (40, 6, 0), "Highlights": (45, 15, 0)}),
    "Kodak Ektachrome E100": preset(
        contrast=15, highlights=-15, shadows=-5, whites=5, blacks=-12,
        vibrance=10, saturation=8,
        luma=curve((0, 0), (0.25, 0.2), (0.5, 0.5), (0.75, 0.8), (1, 1)),
        red=curve((0, 0), (0.5, 0.495), (1, 1)),
        blue=curve((0, 0), (0.5, 0.51), (1, 1)),
        hsl={"Red": (0, 8, 0), "Green": (4, 5, 0), "Aqua": (0, 12, 0), "Blue": (-3, 15, 0)},
        cg={"Shadows": (215, 10, 0), "Highlights": (200, 3, 0)},
        calib={"blueSat": 10}),
    "Kodak Tri-X 400": preset(
        profile="black_and_white", contrast=20, highlights=-10, shadows=-5,
        whites=10, blacks=-15, clarity=10,
        luma=curve((0, 0), (0.2, 0.15), (0.5, 0.5), (0.8, 0.86), (1, 1)),
        bw={"Red": 15, "Orange": 10, "Yellow": 10, "Green": -5, "Aqua": -10,
            "Blue": -20, "Purple": -5}),
    "Fujifilm Superia 400": preset(
        contrast=6, highlights=-15, shadows=5,
        vibrance=5, saturation=5,
        luma=curve((0, 0.02), (0.25, 0.235), (0.5, 0.5), (0.75, 0.78), (1, 0.985)),
        green=curve((0, 0.015), (0.5, 0.505), (1, 1)),
        blue=curve((0, 0.01), (0.5, 0.5), (1, 1)),
        hsl={"Red": (-3, 5, 0), "Yellow": (5, 0, 0), "Green": (10, 10, -5),
             "Blue": (-5, 5, 0), "Magenta": (10, 0, 0)},
        cg={"Shadows": (150, 10, 0), "Highlights": (190, 4, 0)}),
    "Fujifilm Pro 400H": preset(
        contrast=-12, highlights=-20, shadows=15, whites=-5, blacks=8,
        saturation=-12, clarity=-5,
        luma=curve((0, 0.05), (0.25, 0.25), (0.5, 0.51), (0.75, 0.78), (1, 0.97)),
        green=curve((0, 0.02), (0.5, 0.5), (1, 1)),
        blue=curve((0, 0.03), (0.5, 0.5), (1, 0.99)),
        hsl={"Red": (-5, 0, 0), "Orange": (-4, -10, 8), "Yellow": (8, -20, 0),
             "Green": (20, -20, 5), "Aqua": (0, -10, 0), "Blue": (-10, -15, 5),
             "Magenta": (0, -10, 0)},
        cg={"Shadows": (175, 12, 0), "Midtones": (160, 4, 0), "Highlights": (200, 3, 0)}),
    "Fujifilm Velvia 50": preset(
        contrast=18, highlights=-10, shadows=-10, whites=5, blacks=-15,
        vibrance=15, saturation=22,
        luma=curve((0, 0), (0.25, 0.19), (0.5, 0.5), (0.75, 0.82), (1, 1)),
        hsl={"Red": (0, 15, -5), "Orange": (0, 10, 0), "Yellow": (0, 10, 0),
             "Green": (-5, 15, -8), "Aqua": (0, 15, 0), "Blue": (0, 20, -15),
             "Magenta": (0, 15, 0)},
        cg={"Shadows": (260, 8, 0), "Highlights": (30, 8, 0)},
        calib={"redHue": 5, "redSat": 10, "greenSat": 10, "blueSat": 15}),
    "Fujifilm Provia 100F": preset(
        contrast=8, highlights=-10, blacks=-5,
        vibrance=6, saturation=6,
        luma=curve((0, 0), (0.25, 0.22), (0.5, 0.5), (0.75, 0.79), (1, 1)),
        hsl={"Red": (0, 3, 0), "Green": (0, 5, 0), "Blue": (0, 8, 0)},
        cg={"Shadows": (210, 4, 0)}),
    "Fujifilm Acros 100": preset(
        profile="black_and_white", contrast=10, highlights=-15, shadows=5, blacks=-8,
        luma=curve((0, 0), (0.25, 0.22), (0.5, 0.5), (0.75, 0.8), (1, 1)),
        bw={"Red": 5, "Orange": 5, "Yellow": 5, "Green": 5, "Blue": -10}),
    "Fujifilm C200": preset(
        contrast=-5, highlights=-15, shadows=10,
        saturation=-5,
        luma=curve((0, 0.035), (0.25, 0.24), (0.5, 0.505), (0.75, 0.77), (1, 0.975)),
        green=curve((0, 0), (0.5, 0.51), (1, 1)),
        hsl={"Orange": (0, -5, 0), "Yellow": (6, -10, 0), "Green": (12, -5, 0),
             "Blue": (-6, -10, 0)},
        cg={"Shadows": (165, 10, 0), "Highlights": (55, 6, 0)}),
}

def default_presets_dir():
    """Photon's presets folder (QStandardPaths::AppLocalDataLocation/presets
    for organisation and application "Photon")."""
    system = platform.system()
    if system == "Windows":
        base = os.environ.get("LOCALAPPDATA", os.path.expanduser("~\\AppData\\Local"))
    elif system == "Darwin":
        base = os.path.expanduser("~/Library/Application Support")
    else:
        base = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    return os.path.join(base, "Photon", "Photon", "presets")


parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
parser.add_argument("--out", default=default_presets_dir(),
                    help="folder to write the presets to (default: %(default)s)")
out = parser.parse_args().out
os.makedirs(out, exist_ok=True)
excluded = ("exposure", "temperature", "temperatureKelvin", "tint", "denoise",
            "grain", "vignette", "sharp", "mask", "focus", "crop", "orientation",
            "straighten", "flip", "lensProfile")
def generated_by_us(path):
    try:
        with open(path) as f:
            return json.load(f).get("__source") == SOURCE
    except (OSError, ValueError, AttributeError):
        return False


for name, p in PRESETS.items():
    assert not any(k.startswith(excluded) for k in p), name
    path = os.path.join(out, name + ".json")
    if os.path.exists(path) and not generated_by_us(path):
        print("skipped", os.path.basename(path), "(a preset with this name already exists)")
        continue
    with open(path, "w") as f:
        json.dump(p, f, indent=4)
    print("wrote", os.path.basename(path), len(p) - 1, "settings")
