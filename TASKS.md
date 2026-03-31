# 📋 Project Photon Task List

## Phase 0: Infrastructure & Setup [DONE]

- [x] **Test Environment Setup**
- [x] **CI/Lint Configuration**

## Phase 1: The "Real" Image (Core Engine) [DONE]

- [x] **RawEngine Implementation (C++)**
- [x] **RawViewport Integration**

## Phase 2: User Interface (Shadcn/Zinc Theme) [DONE]

- [x] **Theme System**
- [x] **Core Components**
- [x] **Layout Implementation**

## Phase 3: Processing Pipeline & Library [DONE]

- [x] **Basic Image Processing** (Exposure on CPU - to be optimized)
- [x] **Asynchronous Loading**
- [x] **Real Thumbnails** (LibRaw extraction + .PhotonData cache)
- [x] **Library Integration** (Double-click to open, filmstrip sync)
- [x] **Interactive Viewport** (Pan & Zoom)

## Phase 4: Advanced Tool Panel & GPU Pipeline [DONE]

- [x] **GPU-Accelerated Edits**
  - [x] Move Exposure/Contrast calculation to Fragment Shader.
  - [x] Implement Vibrance/Saturation in shader.
- [x] **Advanced Tool Panel (DevelopView.qml)**
  - [x] Implement `Collapsible.qml` component for accordions.
  - [x] **Light Section:** Highlights, Shadows, Whites, Blacks sliders.
  - [x] **Presence Section:** Vibrance, Saturation sliders.
  - [x] **Color (HSL) Section:** 8-band targeted adjustments.
  - [x] **Color Grading Section:** Shadows/Midtones/Highlights tinting.
  - [x] **Creative Section:** Film Grain and Advanced Vignette.
- [x] **Non-Destructive Edit Stack**
  - [x] Store edits in JSON sidecar files.
  - [x] Implement persistent Undo/Redo history.

## Phase 5: Advanced Features & Refinement [DONE]

- [x] **Histogram Component**
  - [x] Compute RGB/Luma distribution in C++ (Async).
  - [x] Render histogram overlay in QML/C++.
- [x] **Technical Polish**
  - [x] Implement Dithering for high-precision output.
  - [x] Optimize GPU pipeline performance.

## Phase 6: UX Enhancements [DONE]

- [x] **EXIF Metadata**
  - [x] Show EXIF metadata.
  - [x] Orient thumbnails using EXIF orientation.
- [x] **Stability & Polish**
  - [x] Fix viewport aspect ratio and stretching issues.
  - [x] Synchronize Histogram math with GPU pipeline.

## Phase 7: Advanced Edit Management [DONE]

- [x] **Restore to Original**
  - [x] Implement global reset logic in C++.
  - [x] Add "Restore" button with Lucide icon to viewport toolbar.
  - [x] Implement "isDefault" check to enable/disable reset action.

## Phase 8: Preset Foundations [DONE]

- [x] **Preset System**
  - [x] Implement `PresetManager` (C++) with cross-platform storage.
  - [x] Add `applySettings` batch logic to `RawEngine`.
  - [x] Create core `PresetPanel.qml` logic.

## Phase 9: Advanced Sidebar & Tool Selection [DONE]

- [x] **Sidebar Refactoring**
  - [x] Implement right-side Section Switcher (Icon-sized navigation).
  - [x] Move Presets to the right sidebar stack.
  - [x] Implement tabbed layout for Metadata, Edit, Crop, Lens, and Presets.
- [x] **Presets Refinement**
  - [x] Replace 'x' delete button with trash icon.
  - [x] Implement delete confirmation dialog.
- [x] **Tool Panels**
  - [x] Add Export panel placeholder.
- [x] **Develop UI Polish**
  - [x] Remove floating top bar in Development view (exclusive to Library/Settings).
  - [x] Add sidebar-based navigation for Library and Settings.

  ## Phase 11: Advanced Settings & Diagnostics [DONE]

  - [x] **GPU Management**
    - [x] Detect available Vulkan physical devices in C++.
    - [x] Implement robust GPU selection logic in `main.cpp` using multiple env vars.
  - [x] **Aesthetics & Theme**
    - [x] Implement Light/Dark mode toggle.
    - [x] Implement Accent color selection.
    - [x] Synchronize `Theme.qml` with persistent settings.
  - [x] **Cache & Maintenance**
    - [x] Implement "Clear Thumbnail Cache" logic.
    - [x] Context-aware enabling/disabling of cache maintenance.
  - [x] **Logging System**
    - [x] Implement `LogManager` C++ class with log level filtering.
    - [x] Add log file location selection to UI.
    - [x] Set default log location based on OS standards.

  ## Phase 12: Hybrid Denoising Pipeline [DONE]

  - [x] **Step 1: CPU Algorithm Refinement**
    - [x] Refactor `Denoiser.cpp` to use **Luma-only Block Matching**.
    - [x] Implement **Spatial Tiling** (256x256 tiles) for cache locality.
    - [x] Implement fully unrolled 16-point Walsh-Hadamard transform.
  - [x] **Step 2: Advanced GPU Preview (NLM)**
    - [x] Implement **Non-Local Means (NLM)** shader in `RawViewport.frag`.
    - [x] Optimize search radius and patch size for 60fps performance.

- [x] **Step 3: Vulkan-Native Search Offload [DONE]**
  - [x] Implement initial **Patch Search shaders** (3x3 patch SSD).
  - [x] Refactor `GpuSearcher` to use **plain Vulkan** (independent compute queue).
  - [x] Implement **Vulkan-to-CPU transfer** of search indices and SSD values.
  - [x] Integrate search results into the **BM3D aggregation phase** in `Denoiser.cpp`.
  - [x] Implement **CPU fallback** (handled automatically by checking Vulkan availability).
- [x] **Asynchronous Workflow**
  - [x] Run heavy denoising in background thread.
  - [x] Implement "Applying denoise..." UI indicator with rotating loader.
  - [x] Implement automatic abort logic when switching photos.
  - [x] Implement dynamic proxy scaling based on viewport size and zoom level.

## Phase 13: High-Performance Denoise Control [DONE]

- [x] **Explicit Execution Control**
  - [x] Add "Denoise" checkbox to UI.
  - [x] Implement immediate abort logic when unchecking.
- [x] **ROI-Driven Proxy Refinement**
  - [x] Update zoom logic to re-render visible crop in high-fidelity first.
  - [x] Apply BM3D only to the high-quality visible region.
- [x] **Lifecycle Management**
  - [x] Ensure `RawEngine` destructor clean-joins all background workers.
  - [x] Prevent segfaults on application close while denoising (Fixed race in `ThumbnailProvider` and resource cleanup).
- [x] **Viewport Constraints & Polish**
  - [x] Lock panning when zoom <= 100% (center image).
  - [x] Constrain pan offset to image boundaries when zoomed in.
  - [x] Implement double-click zoom cycle (100% -> 200% -> 400% -> 100%).
- [x] Fix pan/zoom interaction bug where double-click zoom triggered during pan.
- [x] Fix "Zoom Increases" bug during panning (Race condition in ROI calculation).
- [x] Fix rendering artifacts during zoom/pan with active denoising.

## Phase 18: Preview Rendering Optimization [DONE]

- [x] **Property Exposure**
  - [x] Expose `showingPreview` boolean property in `RawViewport` (C++).
- [x] **Shader Bypass Logic**
  - [x] Add `float isPreview` to uniform block in `RawViewport.frag`.
  - [x] Implement conditional branch in `main()` to bypass RAW pipeline when `isPreview > 0.5`.
- [x] **UI Integration**
  - [x] Update `ShaderEffect` in `App.qml` to pass `rawViewport.showingPreview` to the shader.

## Phase 19: Basic export functionality [DONE]

- [x] Export functionality (Save to JPEG/TIFF).

## Phase 20: Full Vulkan BM3D Denoising [SUPERSEDED by Phase 24]

- [x] **Architecture & Settings**
  - [x] Design `VulkanDenoiser` class architecture (plain Vulkan, compute shaders).
  - [x] Create compute shader infrastructure (grouping, transform, filter, aggregate).
- [x] **Compute Shader Implementation**
  - [x] Implement `bm3d_grouping.comp` - Block matching and group formation.
  - [x] Implement `bm3d_transform.comp` - 3D DCT + Walsh-Hadamard transform.
  - [x] Implement `bm3d_filter.comp` - Hard thresholding (step 1) and Wiener filtering (step 2).
  - [x] Implement `bm3d_aggregate.comp` - Inverse transform and weighted aggregation.
- [x] **Integration & Fallback**
  - [x] Implement wrapper `denoise()` method in `Denoiser` class.
  - [x] Rename existing `denoise()` to `denoiseCpu()` for CPU-only path.
  - [x] Integrate into `RawEngine` with async execution via `QtConcurrent`.
  - [x] Respect `previewDenoiseFull` setting for high-quality previews.
- _Note: GPU full-denoise was removed due to stability issues. Phase 24 replaced it with an enhanced CPU pipeline (YCbCr + Multi-Scale Guided Filter). GpuSearcher (compute-queue patch matching) is retained._

## Phase 21: Effects & Slider Refinements [DONE]

- [x] **UI Refinements**
  - [x] Map Contrast slider range to [-100, 100] in UI.
  - [x] Implement Effects section sliders: Clarity, Dehaze, Structure, Centrè.
  - [x] Update Sharpening slider in Detail section with range [0, 100].
- [x] **Engine & Shader Implementation**
  - [x] Expose new adjustment properties in `RawEngine` and `RawViewport`.
  - [x] Implement `apply_local_contrast` utility in `RawViewport.frag`.
  - [x] Implement `apply_dehaze` in `RawViewport.frag`.
  - [x] Implement `apply_centre` (radial tonal/color) in `RawViewport.frag`.
- [x] **Viewport Performance Optimization**
  - [x] Implement texture caching in `RawViewport` to decouple panning from texture uploads.
  - [x] Optimize 16-bit to 8-bit conversion/upload path.
  - [x] Throttled UI updates during pan.

## Phase 22: Deployment & Versioning [DONE]

- [x] **Versioning System**
  - [x] Create `Version.h.in` template for CMake.
  - [x] Display version string in Settings footer.
  - [x] Implement `tag_release.sh` for automated tagging.
  - [x] `tag_release.sh` now also updates `project(Photon VERSION X.Y.Z LANGUAGES CXX)` in `CMakeLists.txt`.
- [x] **CI/CD Infrastructure**
  - [x] Create GitHub Actions workflow for cross-platform builds.
  - [x] Configure Nightly builds for `develop` and Stable for `master`.
  - [x] Implement automated packaging: **Linux AppImage** (via linuxdeploy) and **Windows MSI** (via CPack/WiX).
  - [x] Fix YAML syntax and artifact collection in CI workflow.
  - [x] Resolve QML naming conflicts (renamed Slider to PhotonSlider) to fix AppImage crashes.
- [x] **Cross-Platform Compatibility**
  - [x] Make all `.PhotonData` path handling OS-agnostic using `QDir::toNativeSeparators`.
  - [x] Update CMake for vcpkg/MSVC compatibility on Windows.
- [x] **Performance Optimization**
  - [x] Implement high-performance compiler flags (-O3, LTO, AVX2/FMA) for Release builds.

## Phase 23: Adjust basic Tonemapping

- [x] [DaVinci Tone Mapping DCTL](https://github.com/thatcherfreeman/utility-dctls?tab=readme-ov-file#davinci-tone-mapping-dctl)

## Phase 24: Enhanced Denoiser — YCbCr + Multi-Scale Guided Filter [DONE]

- [x] **YCbCr Pipeline Refactor**
  - [x] Implement AVX2-optimized RGB↔YCbCr conversion (BT.601 coefficients).
  - [x] Refactor `denoiseCpu()` to convert to YCbCr, run BM3D on Y only, guided filter on Cb/Cr.
  - [x] Generalize `run_bm3d_step_joint()` to support arbitrary channel count (1 or 3).
- [x] **SIMD Box Filter**
  - [x] Implement O(1) separable box filter (horizontal + vertical running-sum passes).
  - [x] AVX2 vectorized vertical pass (8 columns at a time).
- [x] **Guided Filter Kernel**
  - [x] Implement full guided filter math (mean_I, mean_p, var, cov, a, b coefficients).
  - [x] All element-wise operations SIMD-optimized (AVX2/FMA).
- [x] **Multi-Scale Integration**
  - [x] Apply guided filter at 3 scales: Fine (r=2, ε=0.01), Medium (r=4, ε=0.04), Coarse (r=8, ε=0.1).
- [x] **GpuDenoiser Cleanup**
  - [x] Remove dead `denoiseGpu()` method and GPU denoise parameters from `Denoiser`.
  - [x] Remove `useGpuDenoise` setting from `AppStateManager` and QML settings toggle.
  - [x] Clean GpuDenoiser references from test CMakeLists.
  - [x] Update SPECIFICATION.md to document new chroma pipeline architecture.

## Phase 25: Configurable Denoise Parameters UI [DONE]

- [x] **DenoiseParams Struct**
  - [x] Added `DenoiseParams` struct with `searchWindow`, `groupSize`, `chromaRadius`, `chromaDenoise` fields.
  - [x] Updated `block_matching_joint` to use parameterized search window and group size.
  - [x] Updated `multiscale_guided_filter` to use parameterized radii and epsilon scaling.
- [x] **RawEngine Integration**
  - [x] Added 4 new Q_PROPERTY declarations: `denoiseSearchWindow`, `denoiseGroupSize`, `denoiseChromaRadius`, `denoiseChromaAmount`.
  - [x] Setters with validation, clamping, and denoise result invalidation.
  - [x] JSON serialization/deserialization for `.PhotonData` edit stacks.
  - [x] Updated `resetToDefaults()` and `isDefault()`.
  - [x] `startAsyncDenoise()` constructs `DenoiseParams` from member variables.
- [x] **ImageDeveloper Integration**
  - [x] Export path reads new params from JSON and passes `DenoiseParams` to `Denoiser::denoise()`.
- [x] **QML UI**
  - [x] Added "Advanced" subsection in Detail panel under Noise Reduction.
  - [x] Sliders: Search Window (9-39, step 2), Group Size (4-16, step 4), Chroma Radius (1-16), Chroma Denoise (0-100).
  - [x] Added `stepSize` property passthrough in `ControlGroup` → `PhotonSlider`.

## Phase 26: GPU Chroma Filter & Sharpness Fix [DONE]

- [x] **GPU-Accelerated Guided Filter (`GpuChromaFilter`)**
  - [x] Created `box_filter.comp` — separable horizontal/vertical box filter with coalesced memory access.
  - [x] Created `guided_ops.comp` — element-wise operations: multiply, compute a/b coefficients, final output.
  - [x] Created `GpuChromaFilter` class following `GpuSearcher` Vulkan compute pattern.
  - [x] Single command buffer records all 51 dispatches (3 passes × 17 ops) with pipeline barriers.
  - [x] Automatic CPU SIMD fallback when Vulkan is unavailable.
  - [x] Integrated into `Denoiser::denoiseCpu()` — GPU path tried first for Cb and Cr channels.
- [x] **Sharpness Slider Fix**
  - [x] Widened fragment shader blur kernel from 5-tap (1.5 texel radius) to 13-tap dual-radius (1.5 + 3.0–4.0 texels).
  - [x] Effective unsharp mask now works on BM3D-denoised images.
- [x] **Build & Infrastructure**
  - [x] Added `box_filter.comp` and `guided_ops.comp` to CMakeLists compute_shaders target.
  - [x] Added `GpuChromaFilter.cpp/.h` to main and test CMakeLists.
  - [x] Added `CmdPipelineBarrier` to `VulkanFunctions` struct and loader.
- [x] **Documentation**
  - [x] Updated SPECIFICATION.md denoising pipeline section.

## Phase 27: Edge-Selective Sharpening Mask [DONE]

- [x] **Scharr Edge Detection in Fragment Shader**
  - [x] Implemented `compute_edge_mask()` using 3×3 Scharr kernels (Gx/Gy) on luminance.
  - [x] Threshold/gain curve controlled by `sharpenMask` parameter (0–100).
  - [x] Smooth Hermite interpolation for natural mask transitions.
  - [x] Sharpening applied selectively: `mix(preSharp, sharpened, finalMask)`.
- [x] **Mask Feather (Option 1)**
  - [x] Spatial smoothing by averaging mask at 4 cardinal neighbors (radius 1–6 texels).
  - [x] `maskFeather` parameter (0–100) controls blur radius.
- [x] **Focus Detection (Option 2)**
  - [x] Local-contrast gating via `|color − blurred|` from existing unsharp mask blur (zero extra samples).
  - [x] `focusDetect` parameter (0–100) restricts sharpening to in-focus areas.
  - [x] Combined mask: `finalMask = edgeMask × focusGate`.
- [x] **Alt+Drag Mask Preview**
  - [x] `showSharpenMask` uniform renders combined mask as grayscale overlay.
  - [x] Created `KeyTracker` C++ singleton with global `QEvent` filter for reliable Alt key detection.
  - [x] `Connections` in App.qml resets preview on Alt release.
- [x] **Q_PROPERTY Chain**
  - [x] `sharpenMask`, `maskFeather`, `focusDetect`: RawEngine → RawViewport → QML ShaderEffect (persisted).
  - [x] `showSharpenMask`: RawViewport → QML ShaderEffect (transient, not persisted).
  - [x] JSON serialization, `resetToDefaults()`, `isDefault()` for all three parameters.
- [x] **UI — DevelopView**
  - [x] "Masking", "Feather", "Focus" sliders (0–100) in Detail section.
  - [x] Alt+drag activates grayscale combined mask preview on all three sliders.
- [x] **Slider Reset Fix**
  - [x] Fixed ControlGroup slider not resetting visually on photo switch (broken QML binding after user drag).
- [x] **Documentation**
  - [x] Updated SPECIFICATION.md with feather and focus detection details.
  - [x] Updated TASKS.md.

## Phase 28: Tone Curve, Library Improvements & Auto-Scan [DONE]

- [x] **Histogram Fix**
  - [x] Changed normalization from global max to P99 percentile (skip bins 0/255).
  - [x] Prevents single dominant bin from compressing entire histogram.
- [x] **Library Sorting**
  - [x] Sort dropdown (Date/Name/Rating) with ascending/descending toggle.
  - [x] Smart `refreshFiles()`: filter → sort → append pipeline.
  - [x] Home button to return to Welcome view (Lucide house icon).
- [x] **Auto-Scan**
  - [x] `scanIntervalSeconds` Q_PROPERTY on AppStateManager with QSettings persistence.
  - [x] QTimer in LibraryView: periodic scan, smart diff (append-only, no flicker).
  - [x] "Library" settings section in SettingView with interval spinner.
- [x] **Tone Curve**
  - [x] `ToneCurve.qml` Canvas component: 4 channels (L/R/G/B), tab selector.
  - [x] Click to add points, drag to move, double-click to remove interior points.
  - [x] Endpoints draggable vertically only, interior points constrained between neighbors.
  - [x] Monotonic cubic Hermite spline (Fritsch-Carlson) for smooth curves.
  - [x] 4 × QVariantList Q_PROPERTYs: `toneCurveLuma/Red/Green/Blue`.
  - [x] C++ spline→256-entry LUT computation in `RawEngine::rebuildToneLut()` (later superseded by Phase 34 full 65536 precision).
  - [x] 256×4 `QImage` LUT texture (one row per channel) via `ToneLutProvider` image provider (later superseded by Phase 34 packed 16-bit LUT texture).
  - [x] `sampler2D toneLUT` in fragment shader, applied after tonemapping.
  - [x] Luma curve applied as ratio to preserve color relationships.
  - [x] Per-channel (R/G/B) curves applied independently.
  - [x] JSON serialization of control points, `resetToDefaults()`, `isDefault()`.
  - [x] Collapsible "Tone Curve" section after "Light" in DevelopView.
- [x] **Documentation**
  - [x] Updated SPECIFICATION.md and TASKS.md.
- [x] Before/after view + keybind to `\`
- [x] Return to WelcomeView to change workspace

## Phase 29: Stability Fixes & Filmstrip Sort Sync [DONE]

- [x] **Preview Task Serialization**
  - [x] Prevent overlapping `refreshPreview()` tasks in PreviewManager (guards: `m_refreshRunning` / `m_pendingRefreshPath`).
  - [x] Fixes segfault caused by concurrent `develop()` + `denoise()` pipelines competing for global thread pool.
- [x] **Histogram Buffer Safety**
  - [x] `clearProcessedImage()` waits for in-flight histogram `QtConcurrent::run` future before freeing `m_processedImage`.
- [x] **Spline LUT Hardening**
  - [x] `evalMonotonicSpline` / `evalMonotonicSplineLut` sort control points by X and deduplicate before evaluation.
  - [x] Prevents NaN/inf from unsorted or duplicate-X points in malformed JSON.
- [x] **Filmstrip Sort Sync**
  - [x] Sort properties (`sortProperty`, `sortAscending`) moved to shared `window` root object.
  - [x] Library and filmstrip both read/write the same properties; sort order stays in sync.
  - [x] App.qml `refreshFiles()` applies identical sort logic (Name/Date/Rating, asc/desc).

## Phase 30: Crop & Geometry [DONE]

- [x] **Backend Q_PROPERTYs**
  - [x] `cropRect` (QRectF), `cropAspectRatio` (float), `straightenAngle` (float ±45°), `orientationSteps` (int 0–3), `flipHorizontal`/`flipVertical` (bool) on RawEngine → RawViewport.
  - [x] Added to `stateToJson()`, `applyJsonToState()`, `resetToDefaults()`.
- [x] **CropPanel.qml**
  - [x] Aspect ratio preset grid (Free, Original, 1:1, 5:4, 4:3, 3:2, 16:9, 21:9, 65:24).
  - [x] Landscape/portrait toggle (click active preset).
  - [x] Straighten slider ±45° with reset button.
  - [x] Straighten tool (ruler icon) — draw reference line to auto-level.
  - [x] Rotate Left/Right (90° steps), Flip Horizontal/Vertical toggles.
  - [x] Reset all crop/geometry button.
  - [x] Commit-on-Enter: changes saved to engine only on Enter; ESC discards.
- [x] **CropOverlay.qml**
  - [x] Semi-transparent dark mask outside crop rect (50% in crop mode, 100% otherwise).
  - [x] Rule of Thirds grid lines.
  - [x] 8 drag handles (corners + edges) for resize with aspect ratio lock.
  - [x] Center drag to move crop rect.
  - [x] Straighten tool Canvas overlay with dashed reference line.
- [x] **Visual Transforms**
  - [x] QML `Rotation` + `Scale` transforms on ShaderEffect for orientationSteps, straighten, flip.
- [x] **Export Integration**
  - [x] `ImageDeveloper::develop()` applies orientation steps → flip → straighten → crop rect on rotated frame.
- [x] **Icons**
  - [x] Lucide SVGs: rotate-cw, flip-horizontal, flip-vertical, ruler.
- [x] **isDefault() & resetToDefaults()**
  - [x] Crop and geometry properties included in both.
- [x] **Documentation**
  - [x] Updated SPECIFICATION.md and TASKS.md.

## Phase 31: Baked Geometry Pipeline [DONE]

- [x] **Engine Infrastructure**
  - [x] Add `m_geometryBuffer`, `m_geometryWidth`, `m_geometryHeight`, `m_geometryBaked`, `m_inCropMode` state to `RawEngine`.
  - [x] Add `Q_PROPERTY(bool geometryBaked)` exposed to QML.
  - [x] Add `QFutureWatcher<void> m_geometryLoadWatcher` for async geometry baking.
- [x] **`applyGeometryTransforms()` Static Utility**
  - [x] Reusable `QImage` transform: orientation (N×90°) → flip → straighten → crop on rotated frame.
  - [x] Transform order matches `ImageDeveloper::develop()`.
- [x] **`reloadWithGeometry()`**
  - [x] Async re-decode RAW from disk → apply geometry → store in `m_geometryBuffer`.
  - [x] Sets `geometryBaked = true`, emits `imageLoaded`.
- [x] **`enterCropMode()` / `exitCropMode()`**
  - [x] Enter: clears geometry bake, re-processes original image for live QML preview.
  - [x] Exit (ESC): re-bakes geometry if non-default; returns to develop panel.
- [x] **`getProcessedData()` Integration**
  - [x] Returns `m_geometryBuffer` when geometry is baked, otherwise existing pipeline.
- [x] **RawViewport Bridge**
  - [x] `geometryBaked` Q_PROPERTY, `geometryBakedChanged` signal.
  - [x] `Q_INVOKABLE reloadWithGeometry()`, `enterCropMode()`, `exitCropMode()`.
  - [x] `updatePaintNode()` syncs `m_imageWidth`/`m_imageHeight` when buffer dimensions change.
- [x] **QML Integration**
  - [x] ShaderEffect transforms conditional on `!geometryBaked` (neutral when baked).
  - [x] Enter → `commitEdit()` + `reloadWithGeometry()` + switch to Edit panel.
  - [x] ESC → `discardCrop()` + `exitCropMode()`.
  - [x] Sidebar button clicks call `enterCropMode()`/`exitCropMode()` appropriately.
- [x] **Undo/Redo & Reset**
  - [x] Undo/redo outside crop mode re-bakes geometry when properties changed.
  - [x] `resetToOriginal()` clears geometry bake.
- [x] **Documentation**
  - [x] Updated SPECIFICATION.md with baked geometry pipeline details.
  - [x] Updated TASKS.md with Phase 31.

## Phase 32: Crop Coordinate Parity & Domain Clamp [DONE]

- [x] **Domain-Safe Crop Interaction**
  - [x] Enforced crop validity against rotated image quadrilateral domain (not only [0,1] bounds).
  - [x] Incremental drag updates with projection from last valid rect to avoid border skips/jumps.
- [x] **Preview Behavior Refinement**
  - [x] Crop mode resets zoom/pan and auto-fits transformed bounds for full-domain visibility.
  - [x] Disabled extra non-active crop mask when geometry is already baked (avoid double-crop visual mismatch).
- [x] **Bake/Export Coordinate Parity**
  - [x] Unified deterministic normalized→pixel crop mapping in engine/export:
    - `left=floor(x*W)`, `top=floor(y*H)`, `right=ceil((x+w)*W)`, `bottom=ceil((y+h)*H)` (clamped).
  - [x] Verified parity between `RawEngine::applyGeometryTransforms()` and `ImageDeveloper::develop()`.
- [x] **Diagnostics & Verification**
  - [x] Added temporary crop debug traces in QML/C++ for preview vs bake reconciliation.
  - [x] Build + tests pass; user-validated that crop overlay domain movement and preview/bake match.

## Phase 33: Post-Crop Quality & Stability [DONE]

- [x] **Denoise + Zoom Aspect Fix**
  - [x] Fixed X-axis stretch when enabling denoise while zoomed in.
  - [x] `RawViewport::updatePaintNode()` keeps logical image dimensions stable when using partial denoise ROI textures.
- [x] **Tone/HSL Engine-Only Quality Pass (No UI Changes)**
  - [x] Added smoother tonal masks and safer luma-target remap for Whites/Blacks/Highlights/Shadows.
  - [x] Broadened/normalized HSL hue influence and added low-chroma protection to reduce harsh transitions/artifacts.
  - [x] Applied parity updates across shader (`RawViewport.frag`), CPU export (`ImageDeveloper.cpp`), and histogram simulation (`RawEngine.cpp`).
  - [x] Fixed positive-slider whiteout regression by applying brightening shoulder compression only when target luma exceeds 1.0.
- [x] **Research & Documentation**
  - [x] Added comparative analysis against darktable in `tones_report.md` (tone transitions, HSL behavior, tone-curve banding, denoise softness).
  - [x] Captured implementation touchpoints for follow-up engine changes.

## Phase 34: 65536 Tone Curve LUT Precision [DONE]

- [x] Upgraded tone-curve LUT precision from 256 to 65536 entries per channel.
- [x] Reworked LUT texture contract to 256×1024 with 4 stacked 256×256 planes (Luma/R/G/B), packing 16-bit values in RG bytes.
- [x] Updated shader tone-curve sampling to decode packed 16-bit LUT values from centered texture samples.
- [x] Updated CPU export path (`ImageDeveloper`) to use matching 65536-entry LUT indexing and 16-bit identity detection.
- [x] Updated QML tone LUT source texture sizing to `Qt.size(256, 1024)`.
- [x] Reduced black-point aggressiveness by attenuating positive low-luma tone-curve lift (shader + CPU parity).
- [x] Updated SPECIFICATION.md and TASKS.md to reflect the new precision contract and completed work.

## Phase 35: Selective Preset Save Workflow [DONE]

- [x] Added reusable `SettingsSelectionDialog.qml` component for settings-key selection (designed for preset save now, copy/paste reuse later).
- [x] Implemented hierarchical checkbox groups with cascading parent behavior (parent check/uncheck applies to all children).
- [x] Added sectioned key coverage for Light, Presence, Color, HSL, Color Grading, Effects, Creative, Detail, Denoise, Tone Curve, and Geometry.
- [x] Simplified preset selection granularity to match workflow expectations:
  - [x] HSL grouped into single **Color Correction** checkbox.
  - [x] Tone curve channels grouped into single **Tone Curve** checkbox.
- [x] Improved selection dialog usability:
  - [x] Increased dialog width and switched to plain section layout with column wrapping by visible height.
  - [x] Kept rounded dialog corners consistent (including top corners).
- [x] Updated preset save flow in `PresetPanel.qml`:
  - [x] Save button opens selection dialog first.
  - [x] After selection, user names preset.
  - [x] Only selected keys are saved to preset JSON.
- [x] Preset apply behavior remains partial-safe (`RawEngine::applyJsonToState` only applies keys present in the preset map).
- [x] Registered the new component in QML module files (`qmldir`, `CMakeLists.txt`).
- [x] Updated SPECIFICATION.md and TASKS.md.

## Phase 36: Reusable Right-Click Edit Context Menu [DONE]

- [x] Added reusable `PhotoContextMenu.qml` component for thumbnail/viewport contextual actions.
- [x] Wired right-click activation in:
  - [x] Library grid thumbnails (`LibraryView.qml`)
  - [x] Filmstrip thumbnails (`App.qml`)
  - [x] Develop viewport (`App.qml`)
- [x] Implemented contextual actions:
  - [x] Copy settings (opens reusable `SettingsSelectionDialog`, stores filtered keys only).
  - [x] Paste settings (dynamic text: "Paste settings to N photos" when multi-selection is active).
  - [x] Rating actions (No rating + 1★..5★) for selected photos.
  - [x] Filter actions (criteria cycle + star threshold) synchronized with library filter state.
  - [x] Rotate right/left and flip horizontal/vertical actions.
- [x] Extended `AppStateManager` with batch sidecar edit operations for selected photos:
  - [x] `loadSettingsForImage(...)`
  - [x] `applySettingsForSelected(...)`
  - [x] `rotateSelectedRight/Left(...)`
  - [x] `flipSelectedHorizontal/Vertical(...)`
  - [x] `editsUpdated()` signal for UI refresh.
- [x] Added missing Lucide-style icon assets (`copy`, `clipboard-paste`, `star`, `filter`) and registered resources.
- [x] Polished context-menu UX:
  - [x] Fixed menu icon contrast via dedicated high-contrast menu icon assets.
  - [x] Restored rating/filter submenu structure.
  - [x] Kept filter criteria cycling in active context-menu flow for faster iteration.
- [x] Build + tests + offscreen runtime smoke validated after integration.

---

# Taking back control of the codebase

## Phase 37: Old session Not Found and log rotation

- [x] Pop up error when continue session folder is not found, then reset it and return to WelcomeView
- [x] Auto log cleanup

## Phase 38: Panorama

- [x] Panorama Stitching
  - [x] OpenCV integration
  - [x] Stitching
- [ ] Dng export
  - [x] Implement a DNG-like export
  - [ ] Move the implementation to ExportManager

## Backlog / Future

- [ ] **Perspective Correction**
  - [ ] Keystone/perspective transform controls.
- [ ] **Lens Correction**
  - [ ] Integrate `lensfun` for automatic distortion/vignette removal.
- [ ] Let the use decide whether to use auto brightness or not (and threshold)
