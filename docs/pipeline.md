# Photon Develop Pipeline

Living reference for how the develop pipeline works, where each feature sits, and what changed.
Last updated: 2026-11-15.

## 1. Overview

Photon renders an edited image through **three implementations that must stay in parity**:

| Path | Implementation | Entry point |
|---|---|---|
| GPU preview | QML `ShaderEffect` chain + GLSL | `content/views/App.qml` (chain), `src/components/RawViewport.frag` (final pass) |
| CPU export / develop | C++ | `src/engine/ImageDeveloper.cpp` |
| Live histogram | C++ | `RawEngine::requestHistogramUpdate` (`src/engine/RawEngine.cpp`) |

Rule (see `AGENTS.md`): any pipeline change must be applied to all three, with the same constants and formulas. The QML chain and `RawViewport.frag` share uniforms via `App.qml`; the histogram duplicates the tone stage and must be checked against preview/export.

### GPU preview chain (`App.qml`)

1. `photon001LogPass` — WB + exposure → log-luma (`Photon001Log.frag`)
2. `photon001GaussianSmall/Big` H/V — separable blurs of log-luma
3. `photon001ColorFine/Coarse` H/V — separable linear-RGB blurs (`Photon001ColorGaussian.frag`)
4. `photon001DeltaPass` — per-range tone deltas → RG16F texture (`Photon001Delta.frag`)
5. `shaderFx` — final tone/color/output (`RawViewport.frag`)

### Final pass stage order (`RawViewport.frag`)

| # | Stage | Notes |
|---|---|---|
| 0 | GPU NLM denoise | Classic-mode stand-in only while no BM3D result is shown (`liveDenoiseAmount`); off when denoise is disabled or AI denoise is active |
| 0.5 | Local-contrast masks + Sharpness / Structure / Dehaze / Centrè | |
| 1 | White balance | temp/tint multipliers; RAW adds the Kelvin gains (`wbGain`) |
| 2 | Exposure | + positive-EV highlight knee vs `sceneWhite` |
| 2.5 | Photon001 tone ranges | Highlights/Shadows/Whites/Blacks/Clarity from the delta texture |
| 2.75 | Calibration | white-preserving primary matrix + shadows tint (host-computed rows) |
| 3 | Contrast | smoothstep S-curve on luma |
| 4 | HSL 8-band | OKLab/OKLCh |
| 5 | Color grading | 3 zones, OKLab |
| 6 | Saturation / Vibrance | OKLab chroma |
| 7 | AgX | profile `agx` only |
| 8 | Tone curve LUT | display-encoded (after sRGB); parametric or point curves, mutually exclusive via `toneCurveMode` |
| 8.5 | Black & White mix | profile `black_and_white` only |
| 9 | linear → sRGB | |
| 10 | Film grain | |
| 11 | Vignette | |
| 12 | Dither + output | |

Early exits: `isPreview` (embedded/preview JPG) and `showOriginal` (before/after toggle — Phase 40.3 replaces this with modes).

### Develop source (preview, export, previews)

Before any edit stage, the develop source is LibRaw's 16-bit sRGB mem image (or the decoded bitmap). With **AI denoise** (Settings engine "ai", Bayer RAW) the source is the two-slider blend of plain, AI product and AI + colour-pass product (`Y = lerp(Yp, Yai, NR)`, `C = lerp(lerp(Cp, Cai, NR), Ccol, Color)`), where the products are the cached RawNIND result and its chroma BM3D + guided-filter pass in exactly LibRaw's output space/orientation (`photon::ai`, see `features.md`). The engine blends it into `m_processedImage` in place (texture, histogram and geometry bake all see it); `ExportManager` / `PreviewManager` blend it into their mem image (`applyToDevelopSource`). Scene white is measured on the plain source in every path.

### CPU export order (`ImageDeveloper.cpp`)

0. Scene white (`photon::computeSceneWhite`, same as the preview's load-time value) + Photon001 scene stats, on the unmodified source
0a. Classic denoise (BM3D, pre-edit, like the preview) unless the source is AI-denoised
0b. Lens corrections (16-bit; vignetting gain in linear light, like the shader)
0c. Geometry: orientation / flip / straighten / crop (identical to the preview bake) — the pipeline below runs on the cropped frame
1. sRGB16 → linear (LUT)
2. Two-scale Gaussian precompute; Scharr edge mask (sharpen mask) on linear luma
2b. Sharpness (edge mask × focus gate) / Structure / Dehaze / Centrè (shader stage 0.5)
3. WB + Exposure (same formulas)
4. Photon001 tone ranges (`apply_photon001_tone_ranges_cpp`) with host-computed scene stats
4b. Calibration (`photon::calibration::apply`)
5. Contrast
6. HSL 8-band
7. Color grading
8. Saturation / Vibrance
9. AgX
10. Tone curve LUT (display-encoded; parametric or point curves per `toneCurveMode`)
11. Black & White mix
12. linear → sRGB
12b. Film grain (image-relative coordinates, long edge = 2000 units) / Vignette (cropped frame)
13. Dither + 8-bit output

**Known gaps:** the preview's texel-sized radii (local-contrast blurs, sharpen mask, focus gate) follow the displayed texture, which is half size at fit zoom, so at fit the preview's Clarity/Sharpness radius is ~2× the export's (export matches the 100 % view). Export clamps the lens vignetting gain at white (16-bit storage).

### Histogram

`RawEngine::requestHistogramUpdate` duplicates the tone pipeline on the processed image (AI-denoised when active; the blend invalidates its blur cache); it currently stops at color grading and omits saturation/vibrance/tonemap/curve. Blur results are cached per processed image (`ensureHistogramBlurCache`). Any stage added/retuned must be mirrored here.

## 2. Feature hook points

| Feature | Where it plugs in | Status |
|---|---|---|
| Calibration panel (Phase 40.1) | scene-linear, before Contrast | DONE (`c116e1c`); moves in front of the tone LUT once that lands |
| Colored slider component (40.2) | develop panel UI only (no pipeline effect) | DONE (`52f709c`) |
| Before/after modes + split (40.3) | final shader (`compareMode`/`splitPos` + remapped `canvasUV`) + QML overlay | DONE (`4e07a63`) |
| Parametric tone curve (40.4) | tone-curve LUT stage, display-encoded, exclusive with point curves | DONE (`e60731e`) |
| Process version + history (40.5) | edit-stack JSON (`processVersion`, `__step`/`__delta`/`__index` metadata) | DONE (`2fbdc2c`) |
| Lensfun (40.6) | real-time GPU (`lensShader` uniforms, baked-image space) + CPU export before geometry | DONE (`9f1df25`) |
| HDR merge (40.7) | tool before the develop pipeline: merged linear DNG enters the library as a normal RAW file | DONE (`d538d7f`) |
| Camera look (40.8) | display-domain tone LUT (luminance pre-curve, per-photo amount); skipped for AgX | DONE (`8531c59`) |
| RAW Kelvin WB (post-release) | white-balance gains in the final shader + Photon001 log pass, CPU export and histogram (per photo, 2000–50000 K since 2026-11-15) | DONE (`7bc48f4`) |
| Editable slider values (post-release) | UI only (click-to-edit number labels) | DONE (`7bc48f4`) |
| Export parity (post-release) | export: scene white, pre-edit denoise, linear lens vignetting, geometry before the pipeline, + Sharpness/Structure/Dehaze/Centrè/Grain/Vignette | pending confirmation |
| AI denoise (post-release) | develop source (before every stage): cached RawNIND product blended by Amount, all three paths | pending confirmation |
| Local masks / adjustments | per-pixel stage | future TBD |
| Tone-slider rewrite (filmic LUT + edge-aware HL/SHD) | replaces Photon001 delta for those ranges | future TBD |

## 3. Data model

- Non-destructive edits: JSON edit stacks in `.PhotonData/edits/<image>.json` (undo/redo in memory).
- Planned: `processVersion` per photo (40.5) for future rendering retunes; persisted per-photo history snapshots (40.5). Phase 40.4 applies globally without a gate (decided 2026-10-12).
- Classic denoise is recomputed per view/export (no product cache).
- AI denoise products are cached per photo in `.PhotonData/denoise/<file>.ai-<key>.pdn` and the colour pass in `<file>.aic-<key>.pdn` (key = model fingerprint + algorithm/colour version + file size/mtime; 12-bit, MED-predicted, deflated strips; ~45 MB each per 24 MP). The edit stack stores `denoiseAmount` / `denoiseColor`; the engine is a global setting (`denoise/engine`, `denoise/aiDevice`).
- Sidecar format: states, then an optional `{"__index": i}` marker selecting the current state; always read through `photon::editstack` (`src/managers/EditStack.h`).

## 4. Change history

| Date | Change | Purpose | Refs |
|---|---|---|---|
| 2026-10-03 | v0.3.0 release: JPEG/TIFF support (libjpeg-turbo + libtiff + lcms2), B&W profile UI, crash fixes (use-after-free, stack overflow, crash reporter) | broaden input formats + stability | `TASKS.md` Phase 38/v0.3.0 notes |
| 2026-10-12 | Phase 40 planning + docs (`pipeline.md`, `features.md`) | set up tracked enhancement workflow | `TASKS.md` Phase 40 |
| 2026-10-12 | Calibration panel (7 sliders: shadows tint + RGB primary hue/sat), all three paths | colour-model trims (Phase 40.1) | commit `c116e1c` |
| 2026-10-12 | Colored slider component (gradient tracks, live-colour handle, centre-out bipolar fill, hue hints) | UI-only colour feedback (Phase 40.2) | commit `52f709c` |
| 2026-10-12 | Before/after compare modes: side-by-side (visible-region fit per pane), draggable split wipe, exclusive full-original toggle | visual A/B comparison (Phase 40.3) | commit `4e07a63` |
| 2026-10-12 | Parametric tone curve (4 regions + splits), mutual exclusivity with point curves, curve stage moved to the display-encoded domain, histogram bins display-encoded values | LR-like tone-curve behaviour (Phase 40.4) | commit `e60731e` |
| 2026-10-12 | History panel with labelled steps, signed deltas, non-destructive reset and a persistent Original entry; per-photo processVersion plumbing | edit timeline + future retunes (Phase 40.5) | commit `2fbdc2c` |
| 2026-10-12 | Lens corrections: bundled lensfun DB, matcher, distortion/TCA/vignetting (real-time GPU preview, CPU export) and manual profile selection | automatic + manual lens corrections (Phase 40.6) | commit `9f1df25` |
| 2026-10-13 | HDR merge tool: radiance merge with EXIF-validated gains, SIFT+RANSAC alignment, 4 deghost levels, linear DNG output (atomic `.part` write, uncompressed thumbnail) and a preview dialog | bracketed-exposure merge (Phase 40.7) | commit `d538d7f` |
| 2026-10-13 | Camera look: quantile-matched tone curve fitted from the embedded camera JPEG (white-anchored highlights, opt-in per-photo amount), composed into the tone LUT so preview/export/histogram agree | camera-look baseline (Phase 40.8) | commit `8531c59` |
| 2026-10-28 | RAW Kelvin white balance (2000–10000 K, per photo, all three paths) + click-to-edit slider values; denoise now runs in baked geometry space and stale results are discarded (fixes crops snapping back) | post-release refinements | commit `7bc48f4` |
| 2026-11-15 | Export parity: scene white measured like the preview, Classic denoise pre-edit, lens vignetting in linear light, geometry before the pipeline, Sharpness/Structure/Dehaze/Centrè/Grain/Vignette on export; live NLM no longer stacks on BM3D or runs with denoise off; grain in image-relative coordinates | exports match the preview | branch `feature/ai-denoise` |
| 2026-11-15 | Denoise panel reduced to Noise Reduction + Color; AI → colour pass (chroma BM3D 1/4 + guided filter 1/2) cached per photo; engine (AI/Classic) and AI device (Auto/CPU/GPU) in Settings | simpler controls, AI-quality chroma | branch `feature/ai-denoise` |
| 2026-11-15 | Edit-stack reads fixed (export/previews/batch edits used the `__index` marker instead of the current state) | exports render the edits | branch `feature/ai-denoise` |
| 2026-11-15 | AI denoise (RawNIND UtNet2 via ncnn, Vulkan/CPU): Bayer → camera RGB once per photo, pushed through LibRaw's colour/curve, cached, Amount = blend; Classic BM3D kept as engine choice/fallback | ML denoise | branch `feature/ai-denoise` |
| 2026-11-15 | RAW temperature range extended to 50000 K with a logarithmic slider (`ControlGroup` toSlider/fromSlider mapping) | Lightroom-like range | branch `feature/ai-denoise` |
