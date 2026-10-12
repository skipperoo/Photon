# Photon Feature Documentation

Algorithm/purpose history per feature. One section per feature, appended when the feature completes
(see the Phase 40 workflow in `TASKS.md` / `AGENTS.md`).

**Entry template**

```
## <Feature name> (YYYY-MM-DD)

- Purpose: ...
- Algorithm: formulas/constants, color space, references
- Pipeline placement: stage + position (GPU / CPU export / histogram)
- Key constants: ...
- Files: ...
```

---

## Baseline — existing pipeline (documented 2026-10-12)

Short entries for features that predate this document; details live in `TASKS.md` and `docs/pipeline.md`.

### Tone ranges — Photon001 multi-pass (2026-09)

- Purpose: Highlights / Shadows / Whites / Blacks / Clarity on screen (and export) with detail preservation.
- Algorithm: log-luma texture → separable Gaussians (small/big) + linear-RGB blurs → `Photon001Delta.frag` builds per-range tent-window deltas around the tone mid (Shadows `toneMid−2.4` hw 2.0, Highlights `toneMid+1.0` hw 1.9, Whites `toneMid+3.1` hw 2.2, Blacks `toneMid−4.6` hw 1.9) plus two-level Laplacian detail; applied multiplicatively as EV stops on luma with a recovery shoulder and whites endpoint split. Scene stats (`sceneDetailScale/HighlightPin/Compression`) computed on the host.
- Placement: stage 2.5 in the final pass; mirrored in `ImageDeveloper.cpp` and the histogram.
- Files: `content/views/App.qml`, `src/components/Photon001*.frag`, `src/components/RawViewport.frag`, `src/engine/ImageDeveloper.cpp`, `src/engine/RawEngine.cpp`.

### Perceptual color ops — OKLab (2026-09-16)

- Purpose: HSL 8-band panel, 3-way color grading, Saturation/Vibrance, Contrast without hue shifts or gamut speckle.
- Algorithm: OKLab/OKLCh conversions; hue rotation and chroma scaling with gamut mapping along the hue ray (bisection); relative-chroma preservation when moving lightness; Contrast = smoothstep S-curve on luma (`strength = clamp((v−1)·1.2, −0.6, 0.6)`).
- Placement: stages 3-6.
- Files: `src/components/RawViewport.frag`, `src/engine/ImageDeveloper.cpp`.

### Tone curve LUT (Phase 28/34)

- Purpose: Luma + per-channel R/G/B point curves baked to a 65536-entry LUT.
- Algorithm: monotone cubic Hermite (Fritsch–Carlson) evaluation; luma curve remaps `[fMin,fMax]` then per-channel LUTs; packed into a 256×1024 RG16 texture; identity detection skips the stage.
- Placement: stage 8 (linear, pre-sRGB). Display-domain + parametric regions planned (Phase 40.3).
- Files: `content/components/ToneCurve.qml`, `src/engine/RawEngine.cpp` (`evalMonotonicSpline`, `rebuildToneLut`), `src/components/ToneLutProvider.h`, `src/engine/ImageDeveloper.cpp`.

### AgX + Black & White profiles (v0.3.0, 2026-10-03)

- Purpose: selectable rendering profile per photo (`normal`, `agx`, `black_and_white`).
- Algorithm: AgX inset/out matrices + log2 relative to 0.18 over [−15.2, +8] EV, piecewise sigmoid toe/shoulder curve, `pow(curved, 2.4)`; B&W mix scales gray by band-weighted `bwMix` deltas in the same 8 HSV bands as the HSL panel, applied after the tone curve.
- Placement: AgX stage 7 (normal profile); B&W stage 8.5.
- Files: `src/engine/DevelopProfile.h`, `src/components/RawViewport.frag`, `src/engine/ImageDeveloper.cpp`, `content/views/DevelopView.qml`.

### Denoise — GPU NLM preview + BM3D (Phases 13-27)

- Purpose: noise reduction for preview (fast) and final/export (quality).
- Algorithm: preview = approximate non-local means in the final shader (3×3 patch SSD over 7×7, spatial Gaussian, `h = 0.01 + amount/100·0.1`); final = BM3D on YCbCr (8×8 DCT + Kaiser, hard-threshold + Wiener), GPU block matching (`patch_search.comp`) and GPU multi-scale guided chroma filter with CPU fallbacks; ROI/proxy preview scaling, stride 4 final / 8 preview.
- Placement: pre-WB (preview stage 0), post-develop on export.
- Files: `src/engine/Denoiser.{h,cpp}`, `src/engine/GpuSearcher.*`, `src/engine/GpuChromaFilter.*`, `src/engine/patch_search.comp`, `src/components/RawViewport.frag`, `src/engine/ImageDeveloper.cpp`.

### Panorama stitching (Phase 38)

- Purpose: merge overlapping photos into a `.pano.dng` sidecar.
- Algorithm: OpenCV SIFT → `BestOf2NearestMatcher` → `HomographyBasedEstimator` + `BundleAdjusterRay` → `CylindricalWarper` → `VoronoiSeamFinder` → `MultiBandBlender`; writes a 16-bit linear DNG (thumbnail JPEG in IFD0, LINEARRAW in IFD1) via libtiff, plus optional gain compensation (currently disabled).
- Placement: preprocessing; output re-enters the RAW pipeline.
- Files: `src/engine/Panorama.{h,cpp}`, `content/components/PhotoContextMenu.qml`.

### Bitmap decoding + export (v0.3.0)

- Purpose: JPEG/TIFF import alongside RAW; JPG/TIFF export.
- Algorithm: libjpeg-turbo + libtiff decode to 16-bit sRGB, EXIF/TIFF parsing (orientation, ICC), lcms2 ICC→sRGB conversion; bitmaps bypass RAW develop and use the same GPU/CPU edit chain via a custom pixel buffer.
- Files: `src/engine/ImageDecoder.{h,cpp}`, `src/managers/ExportManager.cpp`.

---

## Calibration panel (2026-10-12, Phase 40.1)

- Purpose: Lightroom-Classic calibration — per-primary hue/saturation trims and a shadows tint, for fine colour-model adjustments without touching WB.
- Algorithm: build a white-preserving 3×3 matrix in linear sRGB: each primary `(1,0,0)/(0,1,0)/(0,0,1)` → OkLab → OkLCh, hue rotated by `(hue/100)·0.5 rad`, chroma scaled by `1+(sat/100)·0.6`, back to linear; the matrix columns are then rescaled by `k = M⁻¹·(1,1,1)` so neutral maps to itself (`K=0.5`, `0.6` at ±100). Shadows tint: `Y0 = luma(c)`, `w = 1 − smoothstep(−5, −0.5, log2(Y0/0.18))`, `c.g *= 1 − 0.3·tint·w`, then luminance restored `c *= Y0/Y1`. Identity/neutral skips all work.
- Pipeline placement: scene-linear, after the Photon001 tone-ranges pass and before Contrast, on all three paths (GPU matrix rows via uniforms, CPU export, live histogram).
- Key constants: `kCalibHueRadians = 0.5`, `kCalibSatFactor = 0.6`, `kShadowTintFactor = 0.3`, middle grey 0.18; slider range ±100.
- Files: `src/engine/Calibration.h`, `src/engine/RawEngine.{h,cpp}`, `src/engine/ImageDeveloper.cpp`, `src/components/RawViewport.{h,cpp,frag}`, `content/views/App.qml`, `content/views/DevelopView.qml`, `content/components/SettingsSelectionDialog.qml`, `tests/auto/tst_RawEngine.cpp` (`testCalibrationReferenceBehavior`).
- Commit: `c116e1c`.

---

## Colored slider component (2026-10-12, Phase 40.2)

- Purpose: colour-aware slider feedback — gradient tracks for colour controls, a handle that previews the live colour, centre-out fill for bipolar sliders, and HSL hue hints.
- Algorithm: `SliderRamps` singleton builds ramp colours: WB Temperature/Tint use the exact shader multipliers normalised to max 1 (`(1+u·0.20, 1+u·0.05, 1−u·0.20)` / `(1+u·0.25, 1−u·0.25, 1+u·0.25)`, u = −1..1); primary Hue rotates ±28.65° (0.5 rad) around the primary hue; band Hue spans the selected band ±36° (the shader's ±0.1·360° at ±100); primary/band Saturation fades grey→colour; Luminance darkens→lightens; Grading Hue is a full rainbow; Shadows Tint interpolates green→grey→magenta. `PhotonSlider` renders a 25-stop horizontal `Gradient` when `trackRamp` is set and samples the ramp at the current value for the handle fill; a slider is bipolar (centre-out fill + neutral tick) when its default value is the exact range midpoint.
- Pipeline placement: UI only (no effect on GPU/CPU/histogram rendering).
- Key constants: 25 gradient stops; neutral tick 2 px at 30 % opacity; ramp hue shifts 28.65°/36°.
- Files: `content/components/SliderRamps.qml` (singleton, registered via `QT_QML_SINGLETON_TYPE`), `content/components/PhotonSlider.qml`, `content/components/ControlGroup.qml`, `content/views/DevelopView.qml`, `CMakeLists.txt`.
- Commit: `52f709c`.

---

## Before/after compare modes (2026-10-12, Phase 40.3)

- Purpose: compare the edited image against the unedited development — full-frame toggle, side-by-side panes, and a draggable split wipe.
- Algorithm: the unedited base texture *is* the pipeline at defaults (every stage is identity at neutral), so no second render chain is needed. The final shader gained `compareMode` (0 off, 1 wipe, 2 side-by-side) and `splitPos`; in side-by-side the currently visible region (`imageRect ∩ canvas`) is fitted into each half (left = before, right = after) and all multipass tone/colour textures are sampled through the remapped canvas position; the wipe shows the base to the left of a divider.
- Pipeline placement: final pass only (`RawViewport.frag`); the underlying develop pipeline is untouched.
- Key constants: 12 px gap between panes; divider clamped to 2%…98%, double-click recentres; wipe position is canvas-relative.
- Files: `src/components/RawViewport.frag`, `content/views/App.qml` (`compareMode`/`splitPos`, `Y`/`Shift+Y`, overlay divider + labels, toolbar), `assets/icons/compare.svg`, `assets/icons/split.svg`, `CMakeLists.txt`.
- Commit: `4e07a63`. Fix note: the first cut re-fitted the whole image at shared zoom, which sampled outside the captured canvas window (vertical stripe artifacts at zoom > 1); the panes now fit the visible region.

---

## Parametric tone curve + display-domain curve stage (2026-10-12, Phase 40.4)

- Purpose: Lightroom-style parametric tone curve (Highlights/Lights/Darks/Shadows with movable region splits), mutually exclusive with the free-hand point curves; curves now run where they visually belong (display-encoded values).
- Algorithm: parametric LUT built per node: for each region `(a,b,amt)` with `ctr=(a+b)/2`, `half=(b-a)*0.75+0.05`, `win=0.5+0.5*cos(clamp((x-ctr)/half,-1,1)*PI)`, `d += (amt/100)*0.22*win`, then `clamp(x + d*4x(1-x), 0, 1)` and a monotone prefix-max pass. Composition `parametric → master (Luma) → R/G/B` with linear-sampled LUT composition; the luma stage uses the existing min/max reprojection, so the per-channel LUTs stay pure (identity when unused). Parametric and point curves are mutually exclusive via `toneCurveMode` ("manual" default / "parametric"); the inactive set stays stored in the edit stack/sidecar and is not applied.
- Pipeline placement: stage 8 of the final pass, now **after** the linear→sRGB encode; same order on the CPU export and histogram (histogram also bins display-encoded values now).
- Key constants: region amplitude 0.22; split defaults 25/50/75 (ranges 10–70/20–80/30–90); endpoint drag limits `next.x − 0.01` / `prev.x + 0.01`.
- Files: `src/engine/RawEngine.{h,cpp}`, `src/engine/ImageDeveloper.cpp`, `src/components/RawViewport.{h,cpp,frag}`, `content/components/ToneCurve.qml`, `content/views/DevelopView.qml`, `content/components/SettingsSelectionDialog.qml`, `tests/auto/tst_RawEngine.cpp`.
- Commit: `e60731e`.

---

## History panel + process version (2026-10-12, Phase 40.5)

- Purpose: a persistent per-photo edit timeline (Lightroom-style) with meaningful labels, jump-to-step, a non-destructive Original entry, and per-photo process versioning for future rendering retunes.
- Algorithm: every commit stores `__step` (label) and `__delta` (signed hint) inside the state object; labels come from a key→entity mapping (HSL band/channel, grading region, calibration, parametric curve, …) and deltas from the numeric change of the first changed key (contrast converted to UI units ×200). Legacy steps without metadata get labels/deltas derived by diffing consecutive states when the history is read. The sidecar keeps the full stack plus a trailing `{"__index": N}` marker; the list is capped at `kHistoryLimit = 100`. Reset appends a single "Reset" step and keeps all entries; Clear History keeps only the current state; a virtual "Original" entry (index −1) always sits last and resets non-destructively.
- Pipeline placement: state management only; no rendering effect (processVersion is storage-only until a retune ships).
- Key constants: `kHistoryLimit = 100`, `kCurrentProcessVersion = 1`.
- Files: `src/engine/RawEngine.{h,cpp}`, `src/components/RawViewport.{h,cpp}`, `content/components/HistoryPanel.qml`, `content/views/App.qml`, `assets/icons/history.svg`, `tests/auto/tst_RawEngine.cpp`.
- Commit: `2fbdc2c`.

---

## Lens corrections (lensfun) (2026-10-12, Phase 40.6)

- Purpose: automatic lens corrections (distortion, lateral CA, vignetting) from a bundled lensfun database, with manual Lightroom-style profile selection.
- Algorithm: `LensFun` parses the DB XML (cameras, lenses, calibration), matches the lens by normalized name similarity (longest common substring ≥ 0.5), interpolates distortion/TCA between bracketing focals and picks nearest focal+aperture for vignetting. Corrections: `poly3`/`poly5`/`ptlens` distortion (`rd/ru`), `linear`/`poly3` TCA channel scales, `pa` vignetting as the inverse response `1/(1+k1r²+k2r⁴+k3r⁶)` (r = 1 at the corner), all in the Hugin normalization (r = 1 at half the short side) with an auto-scale so no correction samples outside the source. Preview runs in the final shader in real time (radial in baked-image space; the original centre maps through the rigid geometry, so radii transfer), export on the developed pixels before geometry.
- Pipeline placement: preview stage 0.25 (per-channel base sampling + vignetting gain); CPU export before the geometry transforms.
- Key constants: name-score threshold 0.5; amounts default 50 (0-100); auto-scale computed per photo.
- Files: `assets/lensfun/`, `src/engine/LensFun.{h,cpp}`, `src/engine/RawEngine.{h,cpp}`, `src/engine/ImageDeveloper.cpp`, `src/managers/ExportManager.cpp`, `src/components/RawViewport.{h,cpp,frag}`, `content/components/LensPanel.qml`, `content/views/App.qml`, `tests/auto/tst_RawEngine.cpp`.
- Commit: `9f1df25`.

---

*Phase 40 features are appended here as they complete.*
