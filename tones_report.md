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

---

## 5) Addendum — Tone curve banding (Photon vs darktable)

### Photon current behavior (banding-relevant)

- Tone curve LUT is rebuilt at **256 samples/channel** and quantized to **8-bit RGBA**:
  - `RawEngine.cpp:1314-1317`, `1335-1342`
  - `ToneLutProvider.h:24-31`
- Shader tone-curve sampling uses the 256×4 LUT rows (`toneLUT`) in the processing pass:
  - `RawViewport.frag:721-738`
  - `App.qml:348-353`
- Photon dithering is currently a single pseudo-random per-pixel add at amplitude `1/255`:
  - `RawViewport.frag:538-540`, `769-770`
  - `ImageDeveloper.cpp:127-131`, `584-588`
- In `ImageDeveloper`, dithering is applied **before** denoise (`579-589` then `596-637`), so part of anti-banding noise can be removed again by denoising.

### darktable references

- darktable tone curve uses float processing with a **0x10000 LUT** (65536 entries), not 256:
  - `tmp/darktable/src/iop/tonecurve.c:136`, `755`
- darktable has a dedicated **dither/posterize** module for output quantization control:
  - module intent: reduce output banding/posterization (`dither.c:114-115`)
  - auto bit-depth-aware mode (`dither.c:344-383`)
  - methods include Floyd-Steinberg error diffusion and random TPDF (`dither.c:393-400`, `574-607`)

### Why Photon shows more banding after strong tone-curve edits

1. LUT precision is effectively 8-bit/256-sample in the GPU path, so steep/curvy segments can staircase.
2. Dither strategy is fixed-amplitude and not export bit-depth aware.
3. In CPU preview/export path, dither can be attenuated by subsequent denoise.

### Suggested direction (engine-side)

1. Raise tone-LUT precision (e.g., 4096+ samples or 65536 table, plus higher-precision LUT texture/storage).
2. Keep curve application in high precision until final output quantization.
3. Move/export dithering to the **final step** only (after denoise and all tone/color operations), with bit-depth aware amplitude.
4. Prefer TPDF/blue-noise dithering for raster output; optional FS diffusion for 8-bit export paths.

---

## 6) Addendum — Denoise softness / detail loss (Photon vs darktable)

### Photon current behavior (detail-relevant)

- BM3D strength maps linearly from slider to sigma (`sigma = intensity * 80`):
  - `Denoiser.h:19-22`
- Pipeline is BM3D on luminance + chroma BM3D + multi-scale guided filter on chroma:
  - `Denoiser.cpp:127-176`, `1059-1081`
- Full denoised buffers are returned by the engine when available:
  - `RawEngine.cpp:1804-1823`
- Shader pass still applies real-time denoise from slider value unconditionally:
  - `RawViewport.frag:567-568`
- CPU developer path applies denoise after linear->sRGB conversion and after dithering:
  - `ImageDeveloper.cpp:579-589`, `596-637`

### darktable references

- `raw denoise` is explicitly early, scene-linear/raw pipeline:
  - `tmp/darktable/src/iop/rawdenoise.c:139-143`
- raw denoise uses variance-stabilizing transform + wavelet denoise:
  - `rawdenoise.c:219-233`, `449-450`
- profiled denoise has camera/ISO-driven model + controls for preserving detail:
  - modes (NLMeans/wavelets): `denoiseprofile.c:68-72`
  - parameters (`shadows`, `central pixel weight`, `overshooting`): `99-114`
  - adaptive preconditioning with shadows/WB and scaling: `1682-1705`
  - noise-profile-driven auto inference (`radius/scattering/shadows/bias`): `2650-2668`
- darktable NLMeans implementation includes scattering pattern to avoid grid artifacts and central-pixel weighting:
  - `nlmeans_core.c:84-90`, `135-140`, `432-435`

### Why Photon can look over-soft

1. Strength mapping is global and not noise-profile adaptive (can oversmooth clean files).
2. Denoise placement in `ImageDeveloper` is late (after gamma/8-bit conversion path), which is suboptimal for detail retention.
3. Engine can provide denoised buffers while shader still applies denoise logic, increasing perceived softness.

### Suggested direction (engine-side, no UI changes required)

1. Ensure denoise is applied once in viewport path (skip shader denoise when `m_hasDenoisedResult` is active).
2. Move CPU denoise earlier in `ImageDeveloper` (before sRGB quantization/dither).
3. Replace fixed sigma scaling with profile/adaptive scaling (ISO/noise model + luma-aware strength).
4. Keep denoise/detail separation explicit (edge/detail protection mask or blend-back strategy for high frequencies).

---

## 7) Practical implementation touchpoints for discussion

- Tone-curve precision/banding:
  - `src/engine/RawEngine.cpp` (`rebuildToneLut`)
  - `src/components/ToneLutProvider.h`
  - `content/views/App.qml` (tone LUT source path)
  - `src/components/RawViewport.frag` (tone-LUT sample + final dither)
  - `src/engine/ImageDeveloper.cpp` (CPU LUT + final dithering stage)
- Denoise softness:
  - `src/engine/Denoiser.h/.cpp` (strength mapping and BM3D/chroma strategy)
  - `src/engine/RawEngine.cpp` (`startAsyncDenoise`, `getProcessedData`)
  - `src/components/RawViewport.frag` (real-time denoise pass placement)
  - `src/engine/ImageDeveloper.cpp` (denoise ordering in export/preview path)
