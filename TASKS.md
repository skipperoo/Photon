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

## Phase 9: Advanced Sidebar & Tool Selection [IN PROGRESS]

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

## Phase 20: Full Vulkan BM3D Denoising [IN PROGRESS]

- [x] **Architecture & Settings**
  - [x] Add `useGpuDenoise` toggle setting in `AppStateManager`.
  - [x] Design `VulkanDenoiser` class architecture (plain Vulkan, compute shaders).
  - [x] Create compute shader infrastructure (grouping, transform, filter, aggregate).
- [x] **Compute Shader Implementation**
  - [x] Implement `bm3d_grouping.comp` - Block matching and group formation.
  - [x] Implement `bm3d_transform.comp` - 3D DCT + Walsh-Hadamard transform.
  - [x] Implement `bm3d_filter.comp` - Hard thresholding (step 1) and Wiener filtering (step 2).
  - [x] Implement `bm3d_aggregate.comp` - Inverse transform and weighted aggregation.
- [x] **Integration & Fallback**
  - [x] Implement `GpuDenoiser` class using plain Vulkan.
  - [x] Implement wrapper `denoise()` method in `Denoiser` class with automatic Vulkan/CPU selection.
  - [x] Rename existing `denoise()` to `denoiseCpu()` for CPU-only path.
  - [x] Add `denoiseGpu()` method for Vulkan-accelerated path.
  - [x] Integrate into `RawEngine` with async execution via `QtConcurrent`.
  - [x] Implement CPU fallback when `useGpuDenoise` is false or Vulkan unavailable.
  - [x] Respect `previewDenoiseFull` setting for high-quality previews.
- [ ] **Performance & Validation**
  - [ ] Benchmark Vulkan vs CPU SIMD performance.
  - [ ] Ensure non-blocking UI (independent Vulkan compute queue).
  - [ ] Handle edge cases (memory limits, driver timeouts).

## Phase 21: Effects & Slider Refinements [IN PROGRESS]

- [ ] **UI Refinements**
  - [ ] Map Contrast slider range to [-100, 100] in UI.
  - [ ] Implement Effects section sliders: Clarity, Dehaze, Structure, Centrè.
  - [ ] Update Sharpening slider in Detail section with range [0, 100].
- [ ] **Engine & Shader Implementation**
  - [ ] Expose new adjustment properties in `RawEngine` and `RawViewport`.
  - [ ] Implement `apply_local_contrast` utility in `RawViewport.frag`.
  - [ ] Implement `apply_dehaze` in `RawViewport.frag`.
  - [ ] Implement `apply_centre` (radial tonal/color) in `RawViewport.frag`.
  - [ ] Port RapidRAW's `apply_local_contrast` logic for Clarity, Structure, and Sharpening.

## Backlog / Future

- [ ] **Crop & Transform**
  - [ ] Aspect ratio selection (1:1, 4:5, 16:9, etc.).
  - [ ] Straighten tool and arbitrary rotation.
  - [ ] Perspective correction.
- [ ] **Lens Correction**
  - [ ] Integrate `lensfun` for automatic distortion/vignette removal.
- [ ] Tone Curve (Spline UI).
- [ ] Multi-image batch processing.
