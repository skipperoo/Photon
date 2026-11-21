# Photon Develop Pipeline

Living reference for how the develop pipeline works, where each feature sits, and what changed.
Last updated: 2026-11-16.

## 1. Overview

Photon renders an edited image through **three implementations that must stay in parity**:

| Path | Implementation | Entry point |
|---|---|---|
| GPU preview | QML `ShaderEffect` chain + GLSL | `content/views/App.qml` (chain), `src/components/RawViewport.frag` (final pass) |
| CPU export / develop | C++ | `src/engine/ImageDeveloper.cpp` |
| Live histogram | C++ | `RawEngine::requestHistogramUpdate` (`src/engine/RawEngine.cpp`) |

Rule (see `AGENTS.md`): any pipeline change must be applied to all three, with the same constants and formulas. The QML chain and `RawViewport.frag` share uniforms via `App.qml`; the histogram duplicates the tone stage and must be checked against preview/export.

### GPU preview chain (`App.qml`)

0. `toneMaskImage` — edge-aware tone mask (`image://tonemask`, computed by `RawEngine::updateToneMask` on a worker from the displayed source; see `features.md` "Local tone mapping"). The viewport source is uploaded as RGBA16F and every `ShaderEffectSource` capture of it is RGBA16F.
1. `photon001LogPass` — WB + exposure → working log-luma (.x), Rec.709 log-luma (.y), log2 of the brightest channel (.z) (`Photon001Log.frag`)
2. `photon001GaussianSmall/Big` H/V — separable blurs of log-luma
3. `photon001ColorFine/Coarse` H/V — separable linear-RGB blurs (`Photon001ColorGaussian.frag`)
4. `photon001DeltaPass` — per-range tone deltas → RG16F texture (`Photon001Delta.frag`); the band position comes from the tone mask (binding 4)
5. `shaderFx` — final tone/color/output (`RawViewport.frag`)

### Final pass stage order (`RawViewport.frag`)

| # | Stage | Notes |
|---|---|---|
| 0 | GPU NLM denoise | Classic-mode stand-in only while no BM3D result is shown (`liveDenoiseAmount`); off when denoise is disabled or AI denoise is active |
| 0.5 | Local-contrast masks + Sharpness / Structure / Dehaze / Centrè | |
| 1 | White balance | temp/tint multipliers; RAW adds the Kelvin gains (`wbGain`) |
| 2 | Exposure | `ToneMath` exposure: tapered gain + headroom roll-off to white (continuous at 0 EV) |
| 2.5 | Photon001 tone ranges | Highlights/Shadows/Whites/Blacks/Clarity from the delta texture (display-referred zones, lift roll-off and white anchor already applied in the delta pass; no shoulder) |
| 2.75 | Calibration | white-preserving primary matrix + shadows tint (host-computed rows) |
| 3 | Contrast | S-curve in stops around mid-grey (`ToneMath::contrastLuma`) |
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

Before any edit stage, the develop source is LibRaw's 16-bit sRGB mem image (or the decoded bitmap). With **AI denoise** (Settings engine "ai", Bayer RAW) the source is the two-slider blend of plain, AI product and AI + colour-pass product (`Y = lerp(Yp, Yai, NR)`, `C = lerp(lerp(Cp, Cai, NR), Ccol, Color)`), where the products are the cached RawNIND result (Bayer model on the raw mosaic for Bayer sensors; linear model on the linear Rec.2020 develop source for X-Trans, Foveon, linear DNGs) and its chroma BM3D + guided-filter pass in exactly LibRaw's output space/orientation (`photon::ai`, see `features.md`). The engine blends it into `m_processedImage` in place (texture, histogram and geometry bake all see it); `ExportManager` / `PreviewManager` blend it into their mem image (`applyToDevelopSource`). Scene white is measured on the plain source in every path.

### CPU export order (`ImageDeveloper.cpp`)

0. Scene white (`photon::computeSceneWhite`, same as the preview's load-time value) + Photon001 scene stats, on the unmodified source
0a. Classic denoise (BM3D, pre-edit, like the preview) unless the source is AI-denoised
0b. Lens corrections (16-bit; vignetting gain in linear light, like the shader)
0c. Geometry: orientation / flip / straighten / crop (identical to the preview bake) — the pipeline below runs on the cropped frame
1. sRGB16 → linear (LUT)
2. Two-scale Gaussian precompute; Scharr edge mask (sharpen mask) on linear luma
2b. Sharpness (edge mask × focus gate) / Structure / Dehaze / Centrè (shader stage 0.5)
2c. Tone mask (`photon::tone::computeToneMask`, WB applied, 0 EV) when a tone slider ≠ 0
2d. Detail for Clarity / +Shadows: working log luminance after WB + exposure (the GPU log pass), blurred in log with the GPU taps (`photon001_blur_log_cpp`); the tone math reads the pixel after WB + exposure but before the stage-0.5 effects, like the GPU
3. WB + Exposure (same formulas)
4. Photon001 tone ranges (`apply_photon001_tone_ranges_cpp`) with host-computed scene stats; band position = `maskLogExposed` of the tone mask
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

**Known gaps:** the preview's blur passes (log blurs for Clarity, colour blurs for Sharpness/Structure) step in image pixels at any zoom (`rawViewport.blurUnitSize`, since 2026-11-16), like the export; the final pass's sharpen mask and focus gate still use texel-sized offsets. The live histogram measures Clarity detail on linear-RGB blurs (an estimate). Export clamps the lens vignetting gain at white (16-bit storage).

### Histogram

`RawEngine::requestHistogramUpdate` duplicates the tone pipeline on the processed image (AI-denoised when active; the blend invalidates its blur cache); it currently stops at color grading and omits saturation/vibrance/tonemap/curve. Blur results are cached per processed image (`ensureHistogramBlurCache`), and so is the tone mask (per processed image and WB multipliers). Any stage added/retuned must be mirrored here.

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
| Camera look (40.8) | display-domain tone LUT (luminance pre-curve, per-photo amount); AgX uses its own curve fitted on AgX's output (40.22) | DONE (`8531c59`) |
| RAW Kelvin WB (post-release) | white-balance gains in the final shader + Photon001 log pass, CPU export and histogram (per photo, 2000–50000 K since 2026-11-15) | DONE (`7bc48f4`) |
| Editable slider values (post-release) | UI only (click-to-edit number labels) | DONE (`7bc48f4`) |
| Export parity (post-release) | export: scene white, pre-edit denoise, linear lens vignetting, geometry before the pipeline, + Sharpness/Structure/Dehaze/Centrè/Grain/Vignette | DONE (`8d9156e`) |
| AI denoise (post-release) | develop source (before every stage): cached RawNIND product blended by Amount, all three paths | DONE (`8d9156e`) |
| Local masks / adjustments | per-pixel stage | future TBD |
| Tone-slider rewrite (filmic LUT + edge-aware HL/SHD) | replaces Photon001 delta for those ranges | future TBD |

## 3. Data model

- Non-destructive edits: JSON edit stacks in `.PhotonData/edits/<image>.json` (undo/redo in memory).
- Planned: `processVersion` per photo (40.5) for future rendering retunes; persisted per-photo history snapshots (40.5). Phase 40.4 applies globally without a gate (decided 2026-10-12).
- Classic denoise is recomputed per view/export (no product cache).
- AI denoise products are cached per photo in `.PhotonData/denoise/<file>.ai-<key>.pdn` and the colour pass in `<file>.aic-<key>.pdn` (key = model fingerprint + algorithm/colour version + develop decode version + file size/mtime; lossless 16-bit, MED-predicted, deflated strips; ~63 MB each per 24 MP). Camera looks are cached in `.PhotonData/cache/looks` with the decode version. Settings ▸ Clear Cache deletes `.PhotonData/cache` and `.PhotonData/denoise`.
- Classic denoise and the AI colour pass run in the denoise domain (source × RAW base-exposure gain, sRGB curve, unclipped) at σ derived from the noise measured on the image (`Denoiser::estimateNoiseSigma`).. The edit stack stores `denoiseAmount` / `denoiseColor`; the engine is a global setting (`denoise/engine`, `denoise/aiDevice`).
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
| 2026-11-15 | Export parity: scene white measured like the preview, Classic denoise pre-edit, lens vignetting in linear light, geometry before the pipeline, Sharpness/Structure/Dehaze/Centrè/Grain/Vignette on export; live NLM no longer stacks on BM3D or runs with denoise off; grain in image-relative coordinates | exports match the preview | commit `8d9156e` |
| 2026-11-15 | Denoise panel reduced to Noise Reduction + Color; AI → colour pass (chroma BM3D 1/4 + guided filter 1/2) cached per photo; engine (AI/Classic) and AI device (Auto/CPU/GPU) in Settings | simpler controls, AI-quality chroma | commit `8d9156e` |
| 2026-11-15 | Edit-stack reads fixed (export/previews/batch edits used the `__index` marker instead of the current state) | exports render the edits | commit `8d9156e` |
| 2026-11-15 | AI denoise (RawNIND UtNet2 via ncnn, Vulkan/CPU): Bayer → camera RGB once per photo, pushed through LibRaw's colour/curve, cached, Amount = blend; Classic BM3D kept as engine choice/fallback | ML denoise | commit `8d9156e` |
| 2026-11-15 | RAW temperature range extended to 50000 K with a logarithmic slider (`ControlGroup` toSlider/fromSlider mapping) | Reference-like range | commit `8d9156e` |
| 2026-11-15 | Light sliders: continuous/monotone exposure, contrast and tone ranges in a shared `ToneMath.h` (GPU mirrors it; log pass outputs Rec.709 log luma in .y, delta pass gets `toneBandScale`) | Reference-like sliders | branch `develop` |
| 2026-11-16 | Local tone mapping for Highlights/Shadows/Whites/Blacks: edge-aware guided-filter tone mask (`ToneMask.{h,cpp}`, 3-plane GPU texture sampled by the delta pass) + detail shrinkage; Highlights/Whites bands retuned; 16-bit (RGBA16F) preview source and captures; integer Contrast | no wash-out/posterization, no halos | branch `develop` |
| 2026-11-16 | Tone bands display-referred (fixed zones, no scene white), Blacks per pixel, Shadows/Highlights/Whites on the mask; per-pixel lift roll-off on the brightest channel (replaces the scene-white shoulder) and −Highlights white anchor; mask `a = v²/(v²+ε²)`; Clarity faded in deep shadows; log pass `.z` = log2 max channel | no clipped whites, whites stay white, LR-like zones | branch `develop` |
| 2026-11-16 | Highlights centre follows the photo (scene white after Exposure); −Highlights white anchor on luminance; Whites = pixel white point; Shadows zone deepened (peak ~12 %, from ~3 %) + detail compensation; mask ε 0.7; export detail blurred in log from the log-pass colour (matches the GPU); preview blur steps in image pixels at any zoom | HDR-aware highlights, whites stay white, export = preview | branch `develop` |
| 2026-11-16 | Tone sliders fitted to Reference (adb-driven exports of three photos, Nelder–Mead through the CPU export): asymmetric gains, broad fixed-centre Highlights, deep strong Shadows, per-pixel Whites (+ in the lift roll-off, − quadratic to white), Blacks as levels on the display-encoded luminance; no white anchor; lift cap from 0.2 %; delta pass `.y` unused | Reference-matched sliders | branch `develop` |
| 2026-11-16 | Full-range RAW decode (shared `RawDecode.h`: no auto-bright, highlight blend) + `baseExposure` added to the slider through the exposure roll-off, with shoulder desaturation | highlight recovery | branch `feature/color-pipeline` |
| 2026-11-16 | Rec.2020 working space: RAW/JPEG/TIFF sources in Rec.2020 primaries (sRGB curve); luma/OKLab/ProPhoto constants in `ColorSpace.h` (GLSL mirrors); output conversion (sRGB or Adobe RGB, luminance-preserving gamut mapping) at the start of the display-encoded stages | wide gamut kept to the output | branch `feature/color-pipeline` |
| 2026-11-16 | Clarity matched to Reference: local contrast of the pixel against the edge-aware tone mask (~2 % of the long edge), + on shadows/midtones, − (weaker) on midtones/highlights | Reference-like Clarity | branch `feature/color-pipeline` |
| 2026-11-16 | Denoise after the decode change: AI cache keyed on the decode version (stale products brightened/saturated) and stored in 16 bit; Classic BM3D at the measured noise with reference constants (was σ = 0.8 × amount → 8×8 blocks); denoisers work at the RAW base exposure with Rec.2020 Y'CbCr; camera look in library previews; Settings ▸ Clear Cache | denoise correct on full-range sources | branch `feature/color-pipeline` |
| 2026-11-17 | AI denoise for non-Bayer RAWs: RawNIND linear model on the linear Rec.2020 develop source (signed per-tile gain match), per-model engine/cache; short AI status text | AI for X-Trans/Foveon/linear DNG | branch `feature/color-pipeline` |
| 2026-11-18 | Edit video export: the CPU export path renders one step per Develop group (`stepSettings`), composited into an H.264 MP4 (no pipeline change) | shareable edit clip | branch `feature/edit-video` |
| 2026-11-18 | Preset library: folders, favorites, project presets, .json/.zip import/export (presets apply through `RawEngine::applySettings`; no pipeline change) | organized, shareable presets | branch `feature/presets` |
| 2026-11-18 | Remote mode: the UI (and its GPU preview chain) rendered offscreen at the device's size and streamed to a browser; no pipeline change | edit from an iPad | branch `feature/remote-ui` |
| 2026-11-18 | Remote QoL: upload from the device, Library photos per row, Crop Apply/Cancel buttons; no pipeline change | tablet workflow | branch `feature/remote-qol` |
| 2026-11-19 | Camera look for AgX: a second fitted curve (AgX rendering → camera luminance) composed into the tone LUT when the profile is AgX (preview, export, histogram); remote mode streams H.264 | Camera Look works with AgX; fast remote | branch `feature/remote-h264` |
| 2026-11-19 | Histogram computed when a photo opens (requested once the processed image exists, built on first paint; it waited for the first edit); responsive layout and remote toolbox (no pipeline change) | correct histogram on open; phones and tablets | branch `feature/responsive` |
| 2026-11-21 | Camera look gate: a fit no worse than the current rendering is accepted (was ≥ 30 % better); photos without a usable look cached; stage order unchanged | Camera Look works on photos whose exposure already matches the camera | branch `fix/camera-look-gate` |
