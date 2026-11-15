# Changelog

All notable changes to Photon. Each release's section is published as its GitHub release notes.

## [v0.5.0] - 2026-11-15

### Highlights

- **AI denoise.** A neural network (RawNIND) removes luminance noise from Bayer RAW files, then a colour pass (chroma BM3D followed by a guided filter) cleans up colour noise. Both run once per photo in the background and are cached, so the **Noise Reduction** and **Color** sliders respond instantly. On a 24 MP photo it takes about 5–8 s on a mid-range GPU and about 25 s on the CPU.
- **Exports now match the preview.** Exports of photos that had used the History panel were rendered without any of their edits. That's fixed, along with several smaller differences between preview and export.
- **White balance up to 50,000 K** on a logarithmic temperature slider.

### Added

- **Settings ▸ Denoise:**
  - **Engine:** AI (default) or Classic (BM3D). Photos AI can't process (X-Trans, Foveon, JPEG/TIFF, linear DNGs, or when the model is missing) use Classic automatically.
  - **AI denoise device:** Auto (the GPU when it gives the same result as the CPU and is faster), CPU, or a specific GPU. Takes effect without a restart.
- Detail ▸ Denoise is simplified to **Denoise**, **Noise Reduction** (luminance) and **Color** (chroma), with a progress bar while the AI runs.
- Results are cached per photo in `.PhotonData/denoise`, about 45 MB each for a 24 MP photo. Library previews reuse the cached result and never run the model.
- Bundled model: RawNIND UtNet2 by Benoit Brummer and Christophe De Vleeschouwer (UCLouvain), GPL-3.0 weights, as published by darktable-ai. Its licence and notice ship in `models/`.
- RAW temperature in Kelvin with a Planckian blue → amber track: 2000–50,000 K, with 5500 K at about a third of the track and 10,000 K at the middle.
- Click any slider's number to type a value.

### Fixed

- **Export ignored edits:** export, library previews, copy/paste settings and batch rotate/flip read the history marker instead of the current edit state. Exports came out unedited, and batch edits could wipe a photo's history.
- **Export now matches the preview:**
  - The highlight reference is measured the same way as in the preview. Export always used 1.0 before, which changed Exposure and the Highlights/Shadows/Whites/Blacks/Clarity sliders.
  - Sharpness (with masking, feather and focus detect), Structure, Dehaze, Centrè, Film grain and Vignette are now applied on export.
  - Denoise, lens corrections and crop/rotation run before the edits, as in the preview, so Vignette and Centrè follow the cropped frame.
  - Lens vignetting correction no longer over-brightens corners.
- **Preview denoise:** the preview no longer denoises with Denoise switched off, and no longer adds extra smoothing on top of the finished Classic result.
- **Film grain** now looks the same at any zoom level and export size.
- **Crop/rotate** no longer snaps back to the original frame after denoising.
- **Ratings** survive edits, and the rating filter applies the chosen operator (=, >, ≥, <, ≤) correctly.
- **Crashes and hangs:**
  - Closing Photon while library previews were rendering could crash.
  - The AppImage bundled an outdated Vulkan library that crashed GPU denoise on current NVIDIA drivers. It now uses the system's. If GPU setup ever crashes, the next session falls back to the CPU automatically.

### Build

- New dependency: ncnn 20261020, fetched from a pinned source release during configure and built with Vulkan. The model is downloaded and converted with pnnx during configure, all verified by SHA-256, so **the first configure needs network access**.
- New options: `PHOTON_ENABLE_AI_DENOISE`, `PHOTON_BUNDLE_AI_MODEL` and `PHOTON_AI_MODEL_SOURCE_DIR`. Runtime overrides: `PHOTON_AI_MODEL_DIR` and `PHOTON_AI_DENOISE_DEVICE`.

## [v0.4.0] - 2026-10-13

Calibration panel, coloured sliders, before/after compare modes, parametric tone curve, history panel with process version, lensfun corrections, HDR merge and camera-look fitting (Phase 40).
