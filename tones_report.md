# Tone/HSL Research Report: Photon vs darktable (and RawTherapee availability)

## Scope

I inspected the following code in `tmp/` and Photon:

- `tmp/darktable/src/iop/toneequal.c`
- `tmp/darktable/src/iop/shadhi.c`
- `tmp/darktable/src/iop/colorzones.c`
- `tmp/darktable/src/iop/filmicrgb.c`
- `src/components/RawViewport.frag`
- `src/engine/ImageDeveloper.cpp`

### RawTherapee note

I searched `tmp/` for a RawTherapee source tree (`rawtherapee`, `RawTherapee`, `therapee`) and did not find one in this workspace, so the comparison below is darktable vs Photon with explicit Photon-focused recommendations.

---

## 1) How Photon currently handles tone transitions and HSL

## Tone controls (Whites/Blacks/Shadows/Highlights)

Photon currently uses direct, mostly per-pixel formulas in linear RGB (shader preview and C++ export):

- **Whites**: global division by a scalar (`white_level`)  
  - `RawViewport.frag:605-608`
  - `ImageDeveloper.cpp:388-393`
- **Blacks**: shadow-only mask + multiplicative boost  
  - `RawViewport.frag:609-613`
  - `ImageDeveloper.cpp:394-403`
- **Shadows**: mask `1 - smoothstep(0.0, 0.25, luma)` + multiplicative gain  
  - `RawViewport.frag:619-623`
  - `ImageDeveloper.cpp:408-415`
- **Highlights**: mask from `smoothstep(0.3, 0.95, tanh(luma * 1.5))` + custom luma transform  
  - `RawViewport.frag:626-654`
  - `ImageDeveloper.cpp:416-437`

Observed characteristics from this design:

1. Transition thresholds are fixed and relatively tight, so tonal crossover can feel abrupt on some images.
2. Strong positive/negative highlight/shadow moves can push values aggressively and increase visible noise/artifacts.
3. Final clipping happens late (`RawViewport.frag:749`, `ImageDeveloper.cpp:551-553`), so overshoot can collapse into hard white/black regions.

## HSL controls

Photon HSL is HSV-based with 8 fixed hue bands and Gaussian influence:

- Band influence function: `exp(-1.5 * falloff^2)` (`RawViewport.frag:346-350`)
- Fixed centers/widths (`RawViewport.frag:664-665`, `ImageDeveloper.cpp:345-350`)
- Accumulated hue/sat/luma deltas (`RawViewport.frag:670-675`, `ImageDeveloper.cpp:444-453`)
- Luminance change is a direct RGB multiplier: `color *= (1.0 + lum_adj)` (`RawViewport.frag:680`, `ImageDeveloper.cpp:457-459`)

Observed characteristics:

1. Selection can feel too narrow or too “banded” depending on hue neighborhood.
2. Luminance is not adjusted in a perceptual lightness space; large values can create clipping “blisters” (white/black spot artifacts).
3. No specific low-chroma protection path is applied before hue/lightness remap (gray/near-gray colors can be unstable).

---

## 2) What darktable does differently (relevant to your issue)

## A) Tone Equalizer: EV-domain, smooth interpolation, edge-aware masking

From `toneequal.c`:

- Works as an **exposure-octave equalizer** in scene-linear domain (`toneequal.c:21-58`).
- Uses **Gaussian radial-basis interpolation** over EV channels for smooth transitions (`toneequal.c:45-53`, `760-768`, `1224-1242`).
- Builds a luminance mask and optionally runs **guided filter / EIGF** to preserve local contrast while smoothing masks (`toneequal.c:61-69`, `865-930`).
- Has controls for **blending diameter, feathering, quantization, contrast/exposure boost** (`toneequal.c:180-187`, `3388-3396`).
- Applies bounded correction factors (LUT correction clamped to `[0.25, 4.0]`) to reduce instability (`toneequal.c:795-800`, `1239-1242`).

Why this helps:

- Tonal transitions are intentionally smooth and continuous in EV space.
- Local details are preserved better because mask smoothing is edge-aware rather than purely global.

## B) Shadows/Highlights module: base-layer separation + compression controls

From `shadhi.c`:

- Builds a softened base layer with **Gaussian or bilateral filter** (`shadhi.c:370-398`).
- Uses dedicated **compress** and chroma-correction controls (`shadhi.c:359-364`).
- Applies transformations in controlled chunks for shadows/highlights overlays (`shadhi.c:424-487`).

Why this helps:

- Strong highlight/shadow moves are constrained through compression and base/detail separation, reducing harsh transitions and color damage.

## C) Color Zones (HSL-like): curve/LUT in Lab/LCh with smoother targeting options

From `colorzones.c`:

- Operates in **Lab/LCh-like space** and allows selection by lightness/chroma/hue (`colorzones.c:453-467`, `499-513`, `542-555`).
- Uses curve-generated LUTs with interpolation options (Catmull, monotonic Hermite, etc.) (`colorzones.c:78`, `2732-2738`, `2869-2898`).
- “Smooth mode” blends hue/lightness influence toward neutral for low-chroma pixels (`colorzones.c:555-563`).

Why this helps:

- Color targeting transitions are smoother and more controllable.
- Low-saturation regions are protected from hue/lightness artifacts.

## D) Filmic RGB: toe/shoulder shaping + desaturation/reconstruction near clipping

From `filmicrgb.c`:

- Parametric toe/latitude/shoulder spline for controlled dynamic-range compression (`filmicrgb.c:946-1009`).
- Dedicated desaturation shaping near extremes (`filmicrgb.c:1011-1035`).
- Highlight mask/reconstruction with soft weighting and optional inpainted noise in clipping regions (`filmicrgb.c:1048-1089`).

Why this helps:

- Reduces hard clipping and preserves natural highlight roll-off under extreme edits.

---

## 3) Direct comparison summary

| Area | Photon (current) | darktable approach | Practical impact |
|---|---|---|---|
| Tone targeting | Fixed masks + direct multipliers | EV-channel equalization + smooth interpolation | Photon can feel harsher at crossover points |
| Local detail preservation | No dedicated edge-aware luminance mask in tone sliders | Guided/EIGF/bilateral mask smoothing | Better detail retention and fewer halos/noise bursts in darktable |
| Extreme edits handling | Late clamp, limited protection | Bounded correction + filmic/reconstruction strategies | Photon more prone to blown/blocked artifact spots |
| HSL selection smoothness | 8 fixed Gaussian hue bands in HSV | Curve/LUT with selectable interpolation, smooth/strong modes | darktable offers smoother color transitions |
| HSL luminance behavior | RGB multiply by `(1 + lum_adj)` | Lightness/chroma/hue remap in LCh-like model | Photon more likely to produce white/black blisters at extremes |
| Low-chroma safety | No explicit low-chroma blend protection | Explicit blend-to-neutral in smooth mode | darktable avoids gray-area hue/lightness artifacts better |

---

## 4) Suggestions for Photon (proposed implementation direction)

## Priority 1 — Tone transition quality and artifact resistance

1. **Move tone targeting to EV-domain interpolation**  
   Implement a tone-equalizer-like mapping (multi-band EV controls + Gaussian/RBF interpolation) instead of hard fixed tonal masks.

2. **Add edge-aware luminance-mask smoothing path**  
   Add guided-filter/EIGF-style smoothing on the luminance mask for large highlight/shadow moves (with feathering + quantization controls).

3. **Add bounded correction and soft roll-off**  
   Bound correction factors and add a dedicated soft shoulder/toe rolloff before final output clamp to reduce blister artifacts.

## Priority 2 — HSL smoothness and luminance safety

4. **Migrate HSL processing from HSV RGB-multiply to perceptual space (LCh/OKLCh-style)**  
   Keep hue/chroma/lightness edits in a perceptual model; avoid direct RGB luminance scaling for large adjustments.

5. **Introduce low-chroma protection blend**  
   Fade hue/lightness adjustments toward neutral when chroma is low (similar to `colorzones` smooth mode).

6. **Add interpolation mode for color targeting**  
   Keep current behavior as “strong”, add a “smooth” mode with monotonic or centripetal interpolation to prevent cusps/oscillation.

## Priority 3 — Robustness and parity

7. **Unify shader and `ImageDeveloper` tone/HSL math exactly**  
   Keep one formula set to avoid preview/export divergence when edge cases are hit.

8. **Add regression tests for extreme controls**  
   Add automated tests for:
   - highlight/shadow extremes,
   - HSL luminance ±100 on saturated and near-gray samples,
   - continuity checks (no step discontinuities across tonal boundaries).

---

If you want, next step I can convert this into an implementation checklist with exact code touchpoints (`RawViewport.frag` + `ImageDeveloper.cpp` + UI controls) so we can start iterating safely.
