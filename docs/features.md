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

*Phase 40 features are appended here as they complete.*
