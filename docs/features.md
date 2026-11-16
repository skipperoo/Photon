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
- Placement: pre-WB (preview stage 0), post-develop on export. **Superseded 2026-11-15:** export denoises pre-edit like the preview, and the live NLM only stands in until the BM3D result is shown (see "Export parity" and "AI denoise" below).
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

- Purpose: Reference-style calibration — per-primary hue/saturation trims and a shadows tint, for fine colour-model adjustments without touching WB.
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

- Purpose: Reference-style parametric tone curve (Highlights/Lights/Darks/Shadows with movable region splits), mutually exclusive with the free-hand point curves; curves now run where they visually belong (display-encoded values).
- Algorithm: parametric LUT built per node: for each region `(a,b,amt)` with `ctr=(a+b)/2`, `half=(b-a)*0.75+0.05`, `win=0.5+0.5*cos(clamp((x-ctr)/half,-1,1)*PI)`, `d += (amt/100)*0.22*win`, then `clamp(x + d*4x(1-x), 0, 1)` and a monotone prefix-max pass. Composition `parametric → master (Luma) → R/G/B` with linear-sampled LUT composition; the luma stage uses the existing min/max reprojection, so the per-channel LUTs stay pure (identity when unused). Parametric and point curves are mutually exclusive via `toneCurveMode` ("manual" default / "parametric"); the inactive set stays stored in the edit stack/sidecar and is not applied.
- Pipeline placement: stage 8 of the final pass, now **after** the linear→sRGB encode; same order on the CPU export and histogram (histogram also bins display-encoded values now).
- Key constants: region amplitude 0.22; split defaults 25/50/75 (ranges 10–70/20–80/30–90); endpoint drag limits `next.x − 0.01` / `prev.x + 0.01`.
- Files: `src/engine/RawEngine.{h,cpp}`, `src/engine/ImageDeveloper.cpp`, `src/components/RawViewport.{h,cpp,frag}`, `content/components/ToneCurve.qml`, `content/views/DevelopView.qml`, `content/components/SettingsSelectionDialog.qml`, `tests/auto/tst_RawEngine.cpp`.
- Commit: `e60731e`.

---

## History panel + process version (2026-10-12, Phase 40.5)

- Purpose: a persistent per-photo edit timeline (Reference-style) with meaningful labels, jump-to-step, a non-destructive Original entry, and per-photo process versioning for future rendering retunes.
- Algorithm: every commit stores `__step` (label) and `__delta` (signed hint) inside the state object; labels come from a key→entity mapping (HSL band/channel, grading region, calibration, parametric curve, …) and deltas from the numeric change of the first changed key (contrast converted to UI units ×200). Legacy steps without metadata get labels/deltas derived by diffing consecutive states when the history is read. The sidecar keeps the full stack plus a trailing `{"__index": N}` marker; the list is capped at `kHistoryLimit = 100`. Reset appends a single "Reset" step and keeps all entries; Clear History keeps only the current state; a virtual "Original" entry (index −1) always sits last and resets non-destructively.
- Pipeline placement: state management only; no rendering effect (processVersion is storage-only until a retune ships).
- Key constants: `kHistoryLimit = 100`, `kCurrentProcessVersion = 1`.
- Files: `src/engine/RawEngine.{h,cpp}`, `src/components/RawViewport.{h,cpp}`, `content/components/HistoryPanel.qml`, `content/views/App.qml`, `assets/icons/history.svg`, `tests/auto/tst_RawEngine.cpp`.
- Commit: `2fbdc2c`.

---

## Lens corrections (lensfun) (2026-10-12, Phase 40.6)

- Purpose: automatic lens corrections (distortion, lateral CA, vignetting) from a bundled lensfun database, with manual Reference-style profile selection.
- Algorithm: `LensFun` parses the DB XML (cameras, lenses, calibration), matches the lens by normalized name similarity (longest common substring ≥ 0.5), interpolates distortion/TCA between bracketing focals and picks nearest focal+aperture for vignetting. Corrections: `poly3`/`poly5`/`ptlens` distortion (`rd/ru`), `linear`/`poly3` TCA channel scales, `pa` vignetting as the inverse response `1/(1+k1r²+k2r⁴+k3r⁶)` (r = 1 at the corner), all in the Hugin normalization (r = 1 at half the short side) with an auto-scale so no correction samples outside the source. Preview runs in the final shader in real time (radial in baked-image space; the original centre maps through the rigid geometry, so radii transfer), export on the developed pixels before geometry.
- Pipeline placement: preview stage 0.25 (per-channel base sampling + vignetting gain); CPU export before the geometry transforms.
- Key constants: name-score threshold 0.5; amounts default 50 (0-100); auto-scale computed per photo.
- Files: `assets/lensfun/`, `src/engine/LensFun.{h,cpp}`, `src/engine/RawEngine.{h,cpp}`, `src/engine/ImageDeveloper.cpp`, `src/managers/ExportManager.cpp`, `src/components/RawViewport.{h,cpp,frag}`, `content/components/LensPanel.qml`, `content/views/App.qml`, `tests/auto/tst_RawEngine.cpp`.
- Commit: `9f1df25`.

---

## HDR merge (2026-10-13, Phase 40.7)

- Purpose: merge bracketed exposures into one linear DNG with recovered highlights and clean shadows, with alignment and optional deghosting.
- Algorithm: frames decode to 16-bit linear sRGB via LibRaw/ImageDecoder (RGB order, camera→sRGB through `rgb_cam`; JPEG/TIFF brackets use the shared decode path). Debevec-style radiance merge: `merged = Σ w_i·v_i·gain_i / Σ w_i` with `w_i = (1/gain_i)·(1 − smoothstep(0.80·clip, 0.96·clip, max(v_i)))`, a weight floor on the longest exposure and the middle frame as the all-clipped fallback. Gains: EXIF `t·ISO/N²` between adjacent-brightness frames validated against the median log-ratio of well-exposed pixels (measured gains when EXIF is absent). Alignment: SIFT + `estimateAffinePartial2D` RANSAC on a ≤1200 px gamma-encoded luma image, rejected when corners move >12 % of the diagonal. Deghost: a first no-mask pass predicts the fusion; per-frame log-ratio masks (thresholds 1.0/0.6/0.35 for Low/Medium/High, computed at ≤1024 px) are dilated + Gaussian-feathered (radius 2/3/5) and stretched to full resolution.
- Output: linear DNG through the panorama writer — ColorMatrix1 = sRGB→XYZ D65, illuminant 21, AsShotNeutral from the reference frame, peak-normalised 16-bit U16 with a BaselineExposure tag — written to a `.part` file and atomically renamed; 256 px thumbnail stored uncompressed (libtiff's TIFF-JPEG keeps DQT/DHT in the JPEGTables tag that LibRaw/Qt never merge back).
- Pipeline placement: a tool before the develop pipeline; the merged DNG enters the library as a normal RAW file (no develop-stage change).
- Key constants: clip 0.99; weight window 0.80–0.96·clip; align work edge 1200 px; deghost work edge 1024 px, dilate/feather radii 2/3/5; plausibility gate 12 % diagonal; preview render 1024 px; thumbnail 256 px.
- Files: `src/engine/HdrMerge.{h,cpp}`, `src/engine/HdrMergeMath.h`, `content/components/HdrDialog.qml`, `src/engine/Panorama.cpp` (shared writer + thumbnail fix), `content/views/App.qml`, `content/views/LibraryView.qml`, `content/components/PhotoContextMenu.qml`, `src/main.cpp`, `tests/auto/tst_RawEngine.cpp`.
- Commit: `d538d7f`. Follow-up fixes along the way: merge crash (reference deghost mask never resized to full resolution), RGB order + ColorMatrix convention, atomic `.part` writes (auto-scan picked up half-written files), uncompressed DNG thumbnails (panorama writer too), JPEG/TIFF bracket support, dialog preview + window-level busy pill, filmstrip wheel bounds after auto-refresh.

---

## Camera look from the embedded camera JPEG (2026-10-13, Phase 40.8)

- Purpose: an optional per-photo tone curve fitted from the camera's own JPEG, so the classic sliders can start from a rendering close to the camera's look (opt-in: per-photo Camera Look → Amount 0–100, default 0).
- Algorithm: the raw is decoded with the render's exact LibRaw parameters at half size and paired with the embedded camera JPEG (`unpack_thumb`/`dcraw_make_mem_thumb`, same orientation mapping as the filmstrip); fixed 192 px proxies, 2 % aspect gate, midtone filters (scene 0.001–1.5, JPEG 0.004–0.98, luma 0.015–0.85). `fitCameraToneCurve` builds a 32-knot monotone curve by quantile matching (median of the i-th 1/32 of the sorted scene luminances against the sorted JPEG luminances) and enforces submission gates: ties-aware rank correlation ≥ 0.5 and a ≥ 30 % cut of the held-out error vs the current rendering, in linear and gamma-encoded values. Above the last fitted knot the curve runs straight to display white at scene white (highlight protection — divergence from the reference's exponential shoulder, which crushed highlight detail and removed slider headroom). Application blends the curve with the current rendering by the amount and composes it into the display-encoded tone LUT as a pre-curve on luminance (`decode → curve → encode`), so the existing GPU LUT stage, CPU export and histogram all agree without shader changes; skipped for the AgX profile.
- Pipeline placement: inside the existing tone-curve LUT stage (luminance plane only), before the user curves; the fit itself is a tool before the pipeline, cached per photo in `.PhotonData/cache/looks/<file>.json` (`kCameraLookVersion`) and run lazily on the first Amount raise.
- Key constants: proxy 192 px; 32 knots; gates 0.5 / 0.7; amount default 0 (opt-in), range 0–100; look version 1.
- Files: `src/engine/CameraLook.{h,cpp}`, `src/engine/CameraLookMath.h`, `src/engine/RawEngine.{h,cpp}`, `src/engine/ImageDeveloper.cpp`, `src/managers/ExportManager.cpp`, `src/components/RawViewport.{h,cpp}`, `content/views/DevelopView.qml`, `content/components/SettingsSelectionDialog.qml`, `tests/auto/tst_RawEngine.cpp`.
- Commit: `8531c59`. Scope note: the chroma curve and colour fitting (per-photo matrix, per-model hue/sat tables, pooled profile store) are deferred to Future work — the tone curve was the dominant ingredient of the look.

---

## RAW Kelvin white balance (2026-10-28, post-40.8)

- Purpose: a real colour-temperature slider for RAW photos (2000–50000 K on a logarithmic track — 5500 K ≈ 31 %, 10000 K at the middle — blue→amber Planckian track) while JPEG/TIFF keep the relative temperature slider; the value is stored per photo in the edit stack.
- Algorithm: blackbody (Planckian locus, Kang et al.) → linear sRGB, normalized to green; the applied gains are the target temperature's gains relative to the **5500 K neutral anchor** (the as-shot rendering), green = 1. LibRaw's camera matrices are already white-balanced, so the as-shot Kelvin is not recoverable — 5500 K is the fixed anchor, and slider values are true Planckian corrections around it. The tint slider stays relative on top.
- Pipeline placement: white-balance stage — final shader (`wbGain` vec3) and the Photon001 log pass (so the tone-range stats see the same balance), CPU export (`ImageDeveloper`, gains injected by `ExportManager`) and the histogram. Legacy relative temperature values migrate on load (±100 ≈ ±3000 K) and RAW rendering ignores the relative value.
- Key constants: range 2000–50000 K (`kMinKelvin`/`kMaxKelvin`; Kang et al. high-temperature branch extrapolated above 25000 K, converging to the locus limit); neutral 5500 K; migration ±100 → ±3000 K.
- Files: `src/engine/WhiteBalance.h`, `src/engine/RawEngine.{h,cpp}`, `src/components/RawViewport.{h,cpp}`, `src/components/RawViewport.frag`, `src/components/Photon001Log.frag`, `src/engine/ImageDeveloper.cpp`, `src/managers/ExportManager.cpp`, `content/views/App.qml`, `content/views/DevelopView.qml`, `content/components/SliderRamps.qml`, `content/components/SettingsSelectionDialog.qml`, `tests/auto/tst_RawEngine.cpp`.
- Commit: `7bc48f4`.

---

## Editable slider values (2026-10-28, post-40.8)

- Purpose: click any slider's numeric readout to type a value directly; Enter or clicking outside commits, Escape cancels.
- Algorithm: `EditableValueLabel` (Text + a themed Popup with a TextField, accent border while active) right-aligned to the number so it never overflows the panel card; typed values are clamped to the slider range, snapped to `stepSize` and rounded to the displayed precision before emitting. The host applies them through the slider's `moved`/`released` path, so edits, history and presets behave exactly like dragging.
- Pipeline placement: UI only.
- Files: `content/components/EditableValueLabel.qml`, `ControlGroup.qml`, `CropPanel.qml`, `ExportPanel.qml`, `content/views/App.qml`, `CMakeLists.txt`.
- Commit: `7bc48f4`.

---

## Export parity with the preview (2026-11-15, post-40.8)

- Purpose: exports looked clearly different from the preview. Export now runs the same stages, in the same order and space, as the GPU preview.
- Fixes:
  - **Scene white:** export read `sceneWhite` from the edit stack, which never contained it, so it always used 1.0; the preview uses the measured 97th-percentile linear luma. Export now measures it the same way (`photon::computeSceneWhite`, shared with `RawEngine`), which changes the exposure highlight knee and every Photon001 tone range (Highlights/Shadows/Whites/Blacks/Clarity).
  - **GPU-only effects:** Sharpness (Scharr edge mask, feather, focus gate), Structure, Dehaze, Centrè, Film grain and Vignette are now ported 1:1 to `ImageDeveloper` (same constants as `RawViewport.frag`).
  - **Order and frame:** denoise (pre-edit), lens corrections and geometry now run before the tone/colour pipeline, as in the baked preview, so Centrè/Vignette/Grain sit relative to the cropped frame. Classic denoise previously ran on the finished 8-bit image.
  - **Lens vignetting:** the gain is applied in linear light (it was multiplied into display-encoded values, over-correcting the corners).
  - **Live NLM:** the shader's NLM stand-in no longer runs with denoise switched off, nor stacks on top of the finished BM3D result (`liveDenoiseAmount`).
  - **Grain coordinates:** image-relative (long edge = 2000 units) in both paths, so grain no longer changes with zoom or output size.
- Known remaining difference: the preview's texel-sized radii follow the displayed texture (half size at fit zoom); export matches the 100 % view.
- Files: `src/engine/ImageDeveloper.cpp`, `src/engine/SceneWhite.h`, `src/engine/RawEngine.cpp`, `src/engine/LensFun.cpp`, `src/components/RawViewport.{h,cpp,frag}`, `content/views/App.qml`, `tests/auto/tst_RawEngine.cpp`.

---

## AI denoise — RawNIND via ncnn (2026-11-15, post-40.8)

- Purpose: learned denoising of real sensor noise. **Settings ▸ Denoise ▸ Engine** picks AI (default) or Classic for every photo; Classic is used automatically for photos AI cannot handle. Detail ▸ Denoise keeps three controls: Denoise on/off, **Noise Reduction** (luma) and **Color** (chroma).
- Model: RawNIND UtNet2, Bayer variant (Brummer & De Vleeschouwer, arXiv:2501.08924), GPL-3.0 weights as published by darktable-ai (`model_bayer.onnx`, SHA-256 `da27509d…`). It is a 5-level U-Net (32→512 channels, LeakyReLU 0.2, ~7.8 M parameters, ~97 GFLOP per 512-cell tile). Input: 4 planes (R, G1, G2, B) of 512×512 cells, black-subtracted, normalised to the white level, not white-balanced. Output: camera RGB, 1024×1024 (joint denoise + demosaic), scaled ~1e6 relative to the input.
- Runtime: ncnn 20261020 (BSD-3), built from pinned source with Vulkan. The model is converted at configure time with pnnx (`cmake/AiDenoiseModel.cmake`; download and conversion pinned by SHA-256, fp16 weights) and bundled in `bin/models` (not stored in git). GPU fp16 *storage* overflows on the 1e6 output, so all blobs stay fp32. The device is chosen once per session: the best Vulkan GPU (discrete first) is kept only if a synthetic check tile matches the CPU (relative error < 1e-3) and runs faster. Override with `PHOTON_AI_DENOISE_DEVICE=cpu|gpu`; model folder override `PHOTON_AI_MODEL_DIR`.
- Algorithm (once per photo, worker thread):
  1. LibRaw unpack. Supported: standard Bayer (`filters ≥ 1000`, 3 colours, square pixels, not Fuji-rotated). Everything else (X-Trans, Foveon, bitmaps, linear DNGs) falls back to Classic.
  2. Normalised mosaic `(raw − black[c] − cblack pattern) / (maximum − black)`, cropped to an RGGB phase; cells ≥ 0.98 are marked clipped.
  3. Tiles of 512 cells with 64-cell overlap (cell-wise mirror padding keeps the CFA phase). Each tile's channel means are matched back to the input (gain clamped to 1e6 ± 15 %), then tiles are blended with smoothstep partition-of-unity weights.
  4. LibRaw `dcraw_process()` of the same file gives the plain linear image. A 3×3 least-squares fit maps the model's camera RGB onto LibRaw's linear output (white balance + colour matrix + scale in one step, on unclipped samples; analytic `rgb_cam × pre_mul` fallback).
  5. The fitted linear RGB is written back into LibRaw's buffer (clipped areas keep LibRaw's own values), then `dcraw_make_mem_image()` applies LibRaw's own output curve and orientation. The product is therefore pixel-aligned with, and in the same space as, the plain develop source.
  6. Cached as `.PhotonData/denoise/<file>.ai-<key>.pdn` (12-bit samples, LOCO-I/MED predictor, byte-shuffled deflate strips of 64 rows).
- Colour pass (after AI): the AI result gets Classic's chroma chain once per photo at full strength (`Denoiser::denoiseChroma`): chroma BM3D on Cb/Cr at 1/4 resolution (block matching guided by luma, σ = 10 on the 0..255 scale, stride 8, two passes), bilinear-upsampled to 1/2, multi-scale guided filter at 1/2 guided by the clean luma (radius 4, strength 100), upsampled to full size, luma untouched. Cached as `.aic-<key>.pdn`. CPU box filters are parallel. ~11 s per 24 MP.
- Use: `Y = lerp(Yplain, Yai, NR)`, `C = lerp(lerp(Cplain, Cai, NR), Ccolor, Color)` in a (Y, B−Y, R−Y) basis (`blendDenoised`), applied to the develop source before every stage, so preview, histogram, geometry bake, library previews (cache only) and export (computes if missing) all agree. Moving either slider re-blends and never re-runs anything. Edit stack keys: `denoiseAmount`, `denoiseColor` (older edits migrate from `denoiseChromaAmount`). A legacy per-photo `denoiseMode` key is ignored.
- Classic fallback: Noise Reduction drives the BM3D luma strength, and Color drives both chroma stages (`chromaBm3d` and `chromaDenoise`). Search window, group size and chroma radius keep their stored or default values and are no longer shown.
- Device: Settings ▸ Denoise ▸ AI denoise device = Auto (GPU only if it matches the CPU on a check tile and is faster), CPU, or a GPU by name; it applies from the next tile (`photon::ai::setDevicePreference`). A GPU initialisation that crashes leaves `ai-denoise-gpu-init.marker` in the app data folder, so the next session uses the CPU; choosing a device in Settings clears it.
- Measured (i7-9750H + GTX 1650, 24 MP Sony ARW): 4.6 s per photo on the GPU (89 ms per tile), about 20 s on the CPU (540 ms per tile); cache reload 0.2 s; cache size ~45 MB; channel means within 0.5 % of LibRaw's plain render.
- Files: `src/engine/AiDenoiser.{h,cpp}`, `src/engine/RawEngine.{h,cpp}`, `src/components/RawViewport.{h,cpp}`, `src/managers/ExportManager.cpp`, `src/managers/PreviewManager.cpp`, `src/main.cpp`, `content/views/DevelopView.qml`, `content/components/MetadataPanel.qml`, `content/components/SettingsSelectionDialog.qml`, `cmake/Ncnn.cmake`, `cmake/AiDenoiseModel.cmake`, `cmake/rawnind/*`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/auto/tst_RawEngine.cpp`.

---

## Edit-stack reads in export, previews and batch edits (2026-11-15, post-40.8)

- Purpose: since the History panel (Phase 40.5), sidecars end with a `{"__index": i}` marker. Export, library previews, copy/paste settings and batch rotate/flip read or overwrote `arr.last()`, which is that marker. As a result exports rendered photos with **no edits**, and batch edits on photos with history could replace the marker with a partial state.
- Fix: `src/managers/EditStack.h` (`currentState`, `currentSettings`, `replaceCurrentState`) mirrors `RawEngine::loadEdits`; every reader and writer uses it. Batch edits now replace the current state in place and keep the history and the marker (and its legacy rating).
- Files: `src/managers/EditStack.h`, `ExportManager.cpp`, `PreviewManager.cpp`, `AppStateManager.cpp`, `tests/auto/tst_RawEngine.cpp`.

---

## Light sliders: continuous, monotone ranges (2026-11-15)

- Purpose: Exposure, Contrast, Highlights, Shadows, Whites, Blacks and Clarity were discontinuous, non-monotone or coupled. The positive-EV knee jumped at 0; a `sign(shadows)` pin made Blacks jump; an `(h+s)/max(|h|,|s|)` offset coupled Highlights and Shadows; Contrast pivoted at 0.5 linear and clamped values above 1; Clarity switched hard at mid-grey; and the GPU log/delta passes used `2^EV` instead of the tapered gain. All paths now share `src/engine/ToneMath.h`; the GLSL mirrors it line by line.
- Exposure: gain `2^(0.85·EV)` (positive) / `2^EV`. Positive EV saturates within the headroom to white: `out = L + (1−L)·φ(q)`, `q = L(g−1)/(1−L)`, `φ = q/(1+q^p)^(1/p)` with `p = 1` up to +1 EV, rising to 2 at +3 EV. It is the identity at 0 EV, monotone in EV and in luminance, and white stays white.
- Contrast: `out = L·2^(c·0.45·s·e^(−(s/2.5)²/2))`, `s = log2(L/0.18)`, `c = (contrast−1)·2`. Monotone (slope ≥ 0.55) and symmetric about mid-grey.
- Tone ranges: **superseded 2026-11-16** by display-referred zones, local tone mapping, the white anchor and the lift roll-off (see "Tone sliders fitted to Reference" below). The first version placed the bands relative to `log2(0.18·sceneWhite)` and compressed lifts above scene white with a shoulder that jumped at slider 0.
- Safety: `bandSafetyScale()` (computed once per render; a GPU uniform) scales the band shifts so their combined slope stays ≤ 0.9, so no slider combination inverts tones.
- Lift cap near black: `1 + 9·smoothstep(0, 0.002, L)` since the Reference fit (was 0.08, then 0.02).
- Exposure and Contrast on the step-wedge test (`testLightSlidersSmoothMonotoneAndRanged`): Contrast pivots at 18 % linear, ±0.7 stops; no jumps, monotone, no inversion, including opposite-band combinations and +2 EV.
- Contrast is an integer slider (`stepSize: 1`, engine value `1 + round(v)/200`).
- Files: `src/engine/ToneMath.h`, `ImageDeveloper.cpp`, `RawEngine.cpp`, `src/components/RawViewport.{h,cpp,frag}`, `Photon001Log.frag`, `Photon001Delta.frag`, `content/views/App.qml`, `tests/auto/tst_RawEngine.cpp`.

## Tone sliders fitted to Reference + local tone mapping (2026-11-16)

- Purpose: make Highlights, Shadows, Whites and Blacks behave like Reference, the editor Photon is matched against. The earlier hand-tuned versions washed out whites, flattened midtones, posterized, clipped whites at the first step and placed their zones differently in every photo.
- Method: Reference was driven over adb in an Android emulator. Each slider was exported at −100, −50, −25, +25, +50 and +100 on three photos (`testphoton/_DSC7870`, `_DSC9064`, `_DSC9111`; 16-bit TIFF, 2048 px, sRGB).
  - The response is the mean shift in stops versus the photo's own base, binned by displayed brightness (`testphoton/reference/reference_tables.csv`, local only).
  - Each slider's parameters were fitted by Nelder–Mead through our CPU export at 768 px, minimising the pixel-share-weighted squared error over photos × values × bins (stops clamped to ±6).
  - Whites was refined from the tables (per-pixel model).
- Zones (`src/engine/ToneMath.h`): `x = log2(L / 0.18)` of the exposed, rendered Rec.709 luminance (display white is x = 2.47).

  | Slider | Shape | Gain at −100 / +100 | Reads |
  |---|---|---|---|
  | Shadows | bump at −5.44, −3.16 / +6.0: strongest ~3 % display, fading out by ~55 % | 1.89 / 2.19 stops | mask |
  | Highlights | bump at 0.83 (~57 %), −5.0 / +4.0: from ~25 % up to white | 0.99 / 1.03 stops | mask |
  | Whites | +: ramp from −6.96 over 5.82 (full at ~32 %), amount `2.65·v^2.62`; −: `0.553·v·t²`, t from −4.88 to display white | −0.55 at white / up to +2.65 | pixel |
  | Blacks | levels on the display-encoded luminance e: − `e' = max(e − bp, 0)/(1 − bp)`, `bp = 0.20·|v|^1.14`; + `e' = e + o(1 − e)`, `o = 0.064·v^1.02` | crushes to black / lifts the output black | pixel |

  Reference is asymmetric, hence the separate −/+ gains. Its Highlights is broad with a fixed centre: a centre placed from the scene white fitted no better, so `highlightsCenter()` returns the constant and the scene white no longer affects the render. Its Whites accelerates on + and keeps growing up to white on −. Its Blacks is a black point (−) and an output-black lift (+), which match its tables far better than a band (fit error 9.0 → 2.5).
- Per pixel:
  - **Lift roll-off:** every positive shift R (Highlights, Shadows, +Whites, Clarity) becomes `log2(S(2^R·S⁻¹(m)) / m)`, with `S(g) = g/(1 + g⁴)^¼` and `m` the brightest channel (the log pass's `.z`). Identity at R = 0, monotone, never reaches white for m < 1. +Whites joined the roll-off (it used to multiply past white and blow out).
  - **No white anchor:** Reference darkens near-white tones with their region. At `_DSC9064`'s tail tip it gives −0.81 stop grading to −1.4 in the fur around it. A per-pixel anchor left the clipped tip as a hard-edged white blob.
  - **Blacks is applied to the result of the other shifts** (a monotone map of a monotone map), so combinations can't invert tones.
  - Darkening fades near the flare floor (closed form). The lift cap near black is `1 + 9·smoothstep(0, 0.002, L)` (was 0.02), because Reference lifts deep shadows by 2–3 stops. Clarity and the Shadows detail fade out below ~2 % display.
- **+Shadows keeps local contrast:** in the regions it lifts it adds `0.3 · shadows · w_S · detail` (the Clarity detail, two Laplacian levels of the log luminance).
- Safety: `bandSafetyScale()` adds the worst slope of the regional bands (mask) and the pixel bands (Whites), keeping the total ≤ 0.9, so there's no tone inversion. −Shadows bakes in the slope limit the fit had reached (2.05 → 1.89), so a single slider never engages the scale, which would couple the other sliders to it.
- **Export = preview:**
  - The CPU tone stage reads the pixel after white balance and exposure but before the stage-0.5 effects (`photon001_wb_exposure_cpp`), exactly like the GPU log pass.
  - Its Clarity/Shadows detail is the working log luminance of that colour, blurred in log with the GPU taps (`photon001_blur_log_cpp`: σ 1 / 3.5 px). Before, linear-RGB blurs biased the detail dark.
  - The preview's eight blur passes step in image pixels at any zoom (`rawViewport.blurUnitSize = viewportSize · sourceSize / imageRect.size`). Before, their radius shrank when zooming in.
  - The live histogram still estimates Clarity detail from linear blurs.
- Tone mask (`src/engine/ToneMask.{h,cpp}`): fast guided filter (He & Sun) on log2 luminance.
  1. Grid: the white-balanced, unexposed Rec.709 luminance of the develop source (denoised, lens-corrected, cropped), log-averaged into a grid with a 512-cell long edge.
  2. Self-guided filter on the grid: box radius `r = 0.02 · long edge` (10 cells). The coefficient is `a = v²/(v² + ε²)` with `ε = 0.7 stops²` instead of the plain `v/(v + ε)`. That's a sharper edge/texture split: ±0.7-stop variations (fabric folds, shading) stay in the region (a < 0.3), while edges of 2+ stops are followed (a > 0.6). Then `b = mean·(1 − a)`, both box-averaged.
  3. Per cell it stores `a`, the local mean `μ = b̄/(1 − ā)` and the grid value `g`.
  4. Per pixel (bilinear): `mask = a·(E(g) + shrink(I − E(g))) + (1 − a)·E(μ)`, where `I` is the exposed pixel's log luminance and `E` the exposure map. `shrink(d) = d − T·tanh(d/T)`, `T = 0.5 stop`, drops sub-cell texture but passes hard edges, and `shrink′ < 1`.
  5. GPU texture: `(3·w) × h` RGBA8888, planes `a | μ | g` as 16-bit values in R/G (`μ, g = −16 + 20·t`), filtered bilinearly by hand in `Photon001Delta.frag`. A 1 × 1 texture is the identity.
- Pipeline placement: GPU — `RawEngine::updateToneMask` (worker thread, 80 ms debounce, rerun on source pixels / WB / geometry bake) → `ToneMaskProvider` (`image://tonemask/<version>`) → `toneMaskImage` → delta pass (binding 4, with `imageRect`/`viewportSize`/`maskRoi`). CPU export — computed once after the blur precompute (only when a tone slider ≠ 0). Histogram — cached per processed image and WB. The final pass applies the delta without a shoulder (`apply_photon001_delta_stops`).
- 16-bit preview: `RawViewport` uploads the 16-bit source as `RGBA16F`, and the four `ShaderEffectSource` captures of the viewport use `RGBA16F`, so the preview no longer bands at slider extremes.
- Validation:
  - `testToneSlidersMatchReference` renders the three photos and fails if a slider's error against the Reference tables exceeds its fitted value by more than 25 % (Highlights 1.0, Shadows 3.14, Whites 2.76, Blacks 2.48). It skips when `testphoton` is absent (CI).
  - `testLightSlidersSmoothMonotoneAndRanged`: zones of the fitted model on the step wedge, plus no jumps (a step 3× both its neighbours), monotonicity and no inversion, including opposite-slider combinations and +2 EV.
  - `testToneBandsLocalNoHaloKeepDetail`: no halo at a hard 5.3-stop edge. −Blacks crushes and Whites compresses by design, so their detail isn't checked.
  - `testToneMaskEncoding`: GPU encoding vs CPU planes, identity, shrinkage monotonicity.
- Key constants: zones above; `kShadowsDetail 0.3`, `kLiftShoulder 4`, `kMaxLiftStops 4`, `kClarityBlackStart −7`, `kClarityBlackSpan 3`; mask `kGridLongEdge 512`, `kGridRadius 0.02`, `kGridEps 0.7`, `kDetailThreshold 0.5`, `kLogMin −16`, `kLogRange 20`.
- Files: `src/engine/ToneMath.h`, `src/engine/ToneMask.{h,cpp}`, `src/components/ToneMaskProvider.h`, `src/components/RawViewport.{h,cpp,frag}`, `src/components/Photon001Log.frag`, `src/components/Photon001Delta.frag`, `src/engine/RawEngine.{h,cpp}`, `src/engine/ImageDeveloper.cpp`, `src/main.cpp`, `content/views/App.qml`, `content/views/DevelopView.qml`, `content/components/ControlGroup.qml`, `tests/auto/tst_RawEngine.cpp`.

---

*Phase 40 features are appended here as they complete.*

## Full-range RAW decode + highlight recovery (2026-11-16)

- Purpose: LibRaw's auto-bright brightened every RAW until 1 % of its pixels clipped, destroying that highlight detail before editing (e.g. `_DSC9064`'s white tail tip, which Reference renders at 86 % with detail). All develop-source decodes now keep the sensor's full range.
- Shared decode (`src/engine/RawDecode.h`, used by RawEngine, ExportManager, PreviewManager, the AI denoise products, CameraLook and the Reference fixture): `photon::raw::configureDevelopDecode()` sets 16-bit output, camera WB, `no_auto_bright = 1` and `highlight = 2` (blend clipped channels).
- Base exposure: `photon::raw::baseExposure()` measures what auto-bright used to add. The brightest 1 % of each channel should reach white: `ev = log2(1 / L99) / 0.85` in exposure-slider units. It is measured on the plain source in every path (before AI denoise), and the pipeline adds it to the slider (`exposure + baseExposure`) through the exposure roll-off, so highlights get a soft shoulder instead of a hard clip. Rendered files (JPEG/TIFF) get 0.
- Shoulder desaturation (`photon::tone::exposureDesaturation`): the more the roll-off compresses a pixel (target vs its plainly gained luminance), the closer its colour moves to neutral: `s = smoothstep((1 − target/luma − 0.05) / 0.45)`, `c' = mix(c, target, s)`. Without it, bright tinted areas kept their tint (the tail tip turned cyan) or clipped their dominant channel. Mirrored in the CPU export, the histogram, `Photon001Log.frag` and `RawViewport.frag`.
- Effect on the test photos (base render): no pixel has all channels clipped any more (`_DSC9064`: 0.36 % → 0). The tail tip is near neutral (#e5ebe8, Reference #cfd7db) and recoverable with −Exposure/−Highlights. Overall brightness is within ~5–10 %.
- Files: `src/engine/RawDecode.h`, `ToneMath.h`, `ImageDeveloper.cpp`, `RawEngine.{h,cpp}`, `AiDenoiser.cpp`, `CameraLook.cpp`, `src/managers/ExportManager.cpp`, `PreviewManager.cpp`, `src/components/RawViewport.h`, `RawViewport.frag`, `Photon001Log.frag`, `content/views/App.qml`, `tests/auto/tst_RawEngine.cpp`.

