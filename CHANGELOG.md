# Changelog

All notable changes to Photon. Each release's section is published as its GitHub release notes.

## [v0.5.1] - 2026-11-18

v0.5.1 is the first published release of this feature set: v0.5.0 was tagged but never released, because its Windows build failed.

### Highlights

- **AI denoise.** A neural network (RawNIND) removes luminance noise from RAW files (a Bayer model, and a linear model for X-Trans, Foveon, linear DNGs and other sensors), then a colour pass (chroma BM3D followed by a guided filter) cleans up colour noise. Both run once per photo in the background and are cached, so the **Noise Reduction** and **Color** sliders respond instantly. On a 24 MP photo it takes about 5–8 s on a mid-range GPU and about 25 s on the CPU.
- **Exports now match the preview.** Exports of photos that had used the History panel were rendered without any of their edits. That's fixed, along with several smaller differences between preview and export.
- **White balance up to 50,000 K** on a logarithmic temperature slider.
- **Light sliders matched to Reference.** Highlights, Shadows, Whites and Blacks were fitted to Reference's measured response on three photos. They're smooth from −100 to +100, always move in one direction, and lifts never clip.
- **Highlights and Shadows adapt locally** (edge-aware local tone mapping). Each slider moves whole regions, so texture keeps its contrast: no flat midtones or posterization, and no halos along edges.

### Added

- **Settings ▸ Denoise:**
  - **Engine:** AI (default) or Classic (BM3D). Bayer RAWs use RawNIND's Bayer model; every other RAW (Fujifilm X-Trans, Foveon, linear DNGs) uses its linear model. JPEG/TIFF files use Classic automatically: their noise has already been through the camera's processing and compression, which the AI models weren't trained on.
  - **AI denoise device:** Auto (the GPU when it gives the same result as the CPU and is faster), CPU, or a specific GPU. Takes effect without a restart.
- Detail ▸ Denoise is simplified to **Denoise**, **Noise Reduction** (luminance) and **Color** (chroma), with a progress bar while the AI runs.
- Results are cached per photo in `.PhotonData/denoise`, about 45 MB each for a 24 MP photo. Library previews reuse the cached result and never run the model.
- Bundled model: RawNIND UtNet2 by Benoit Brummer and Christophe De Vleeschouwer (UCLouvain), GPL-3.0 weights, as published by darktable-ai. Its licence and notice ship in `models/`.
- RAW temperature in Kelvin with a Planckian blue → amber track: 2000–50,000 K, with 5500 K at about a third of the track and 10,000 K at the middle.
- Click any slider's number to type a value.
- **16-bit TIFF export** (Export ▸ TIFF ▸ Bit depth).
- **Adobe RGB export:** Export ▸ Color space (sRGB / Adobe RGB). Exports embed the matching ICC profile.

### Changed

- **Light sliders** (Exposure, Contrast, Highlights, Shadows, Whites, Blacks, Clarity), measured on synthetic test images:
  - **Exposure:** no jump when crossing 0. Previously +0.01 EV darkened whites. Highlights now roll off smoothly towards white.
  - **Contrast:** an S-curve that pivots at mid-grey, in whole steps. Previously +Contrast darkened almost the whole photo.
  - **Shadows:** strongest in the deep shadows (~3 %), fading out by mid-grey. About −1.9 / +2.2 stops at ±100, so it really recovers dark areas, and lifted regions keep their local contrast.
  - **Highlights:** broad, from about 25 % brightness up to white, peaking around 57 %. About ±1 stop at ±100. Near-white areas darken with their surroundings (no white blobs).
  - **Whites:** + brightens progressively more towards the top and accelerates near +100 without blowing out. − pulls the brightest tones down progressively (about −0.55 stops at white).
  - **Blacks** is a black point: − crushes the darkest tones to black, + lifts the output black.
  - **Highlights and Shadows adapt locally:** they follow an edge-aware map of the photo's brightness, not each pixel's own value. Texture inside a region keeps its detail (fabric folds included), and the effect stops at edges without halos.
  - **No clipping from lifts:** Highlights, Shadows, Whites and Clarity lifts roll off towards white per pixel, so bright whites no longer blow out, including at the first step of the slider.
  - Deep shadows can now be lifted further (the noise-protection cap starts at 0.2 % brightness instead of 2 %).
  - **Clarity** only changes local detail, so flat areas stay untouched. It no longer clips whites, and it fades out in the deepest shadows.
  - Moving neighbouring sliders in opposite directions can no longer invert tones.
  - Existing edits that use these sliders will look somewhat different.
- The preview now renders from the full 16-bit image, so gradients don't band when sliders are pushed hard.
- **Highlight recovery:** RAWs are decoded with the sensor's full range. Before, the brightest ~1 % was clipped at decode. The usual brightness is restored with a soft highlight shoulder, so near-blown areas keep their detail and can be brought back with −Exposure / −Highlights / −Whites. Very bright tinted areas fade to white instead of keeping a colour cast. Photos look slightly different, with softer highlights.
- **Wide-gamut colour:** editing happens in Rec.2020, so colours outside sRGB (saturated flowers, neon, deep skies) are no longer clipped at decode. They're mapped into the output gamut only at the end, preserving hue and brightness, and an Adobe RGB export keeps them where Adobe RGB can. JPEG/TIFF files with an embedded profile (Adobe RGB, Display P3, …) keep their gamut too.
- **Clarity** now works like Reference: it adds local contrast at a larger scale (about 1–2 % of the image) in shadows and midtones, and −Clarity smooths midtones and highlights.
- **Classic denoise adapts to each photo:** it measures the photo's noise, and the slider scales from that: 50 removes the measured noise, 100 removes twice as much. Before, it assumed far more noise than real photos have and smeared detail into blocks. The Color slider works the same way on colour noise.
- **Camera Look** sits above Light in the Develop panel.
- **Export panel:** each setting (Format, Color space, Bit depth or Quality, Output folder) has its own full-width line, so switching format no longer moves the other controls.
- **Settings ▸ Clear Cache** deletes all generated data for the workspace (thumbnails, previews, camera looks, AI denoise results), not only thumbnails. Edits and ratings are kept.
- AI denoise results are cached at full 16-bit precision (about 63 MB per 24 MP photo, up from ~45 MB), so lifted shadows stay smooth.

### Fixed

- **Export ignored edits:** export, library previews, copy/paste settings and batch rotate/flip read the history marker instead of the current edit state. Exports came out unedited, and batch edits could wipe a photo's history.
- **Export now matches the preview:**
  - Clarity and the local-contrast radii are now the same in the preview (at any zoom) and in the export. The preview's radius used to shrink when zooming in, and the export measured local detail differently, so +Clarity darkened deep shadows more in the export.
  - The highlight reference is measured the same way as in the preview. Export always used 1.0 before, which changed Exposure and the Highlights/Shadows/Whites/Blacks/Clarity sliders.
  - Sharpness (with masking, feather and focus detect), Structure, Dehaze, Centrè, Film grain and Vignette are now applied on export.
  - Denoise, lens corrections and crop/rotation run before the edits, as in the preview, so Vignette and Centrè follow the cropped frame.
  - Lens vignetting correction no longer over-brightens corners.
- **Preview denoise:** the preview no longer denoises with Denoise switched off, and no longer adds extra smoothing on top of the finished Classic result.
- **Film grain** now looks the same at any zoom level and export size.
- **AI denoise** no longer brightens and saturates photos (more so with Color): results cached under the previous decode are recomputed.
- **Library previews** now include the Camera Look.
- **Crop/rotate** no longer snaps back to the original frame after denoising.
- **Ratings** survive edits, and the rating filter applies the chosen operator (=, >, ≥, <, ≤) correctly.
- **Crashes and hangs:**
  - AI denoise on the GPU could crash Photon the first time it compiled its shaders: two different copies of the shader compiler were mixed in the build. After such a crash Photon quietly used the CPU from then on.
  - Closing Photon while library previews were rendering could crash.
  - The AppImage bundled an outdated Vulkan library that crashed GPU denoise on current NVIDIA drivers. It now uses the system's. If GPU setup ever crashes, the next session falls back to the CPU automatically.

### Build

- New dependency: ncnn 20261020, fetched from a pinned source release during configure and built with Vulkan. The model is downloaded and converted with pnnx during configure, all verified by SHA-256, so **the first configure needs network access**.
- New options: `PHOTON_ENABLE_AI_DENOISE`, `PHOTON_BUNDLE_AI_MODEL` and `PHOTON_AI_MODEL_SOURCE_DIR`. Runtime overrides: `PHOTON_AI_MODEL_DIR` and `PHOTON_AI_DENOISE_DEVICE`.
- Qt ShaderTools is now a build-time dependency only. Photon no longer links `libQt6ShaderTools` at runtime.
- Windows: the test suite builds again (two test variables were named `near` and `far`, which the Windows headers define as macros).

## [v0.4.0] - 2026-10-13

Calibration panel, coloured sliders, before/after compare modes, parametric tone curve, history panel with process version, lensfun corrections, HDR merge and camera-look fitting (Phase 40).
