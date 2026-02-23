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
- [ ] **Presets Refinement**
  - [ ] Replace 'x' delete button with trash icon.
  - [ ] Implement delete confirmation dialog.
- [ ] **Tool Panels**
  - [ ] Add Export panel placeholder.
- [ ] **Develop UI Polish**
  - [x] Remove floating top bar in Development view (exclusive to Library/Settings).
  - [ ] Add sidebar-based navigation for Library and Settings.

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

  ## Phase 12: Hybrid Denoising Pipeline [IN PROGRESS]

  - [x] **Step 1: CPU Algorithm Refinement**
    - [x] Refactor `Denoiser.cpp` to use **Luma-only Block Matching**.
    - [x] Implement **Spatial Tiling** (256x256 tiles) for cache locality.
    - [x] Implement fully unrolled 16-point Walsh-Hadamard transform.
  - [x] **Step 2: Advanced GPU Preview (NLM)**
    - [x] Implement **Non-Local Means (NLM)** shader in `RawViewport.frag`.
    - [x] Optimize search radius and patch size for 60fps performance.
- [x] **Step 3: GPU Search Offload [DONE]**
  - [x] Implement initial **Patch Search shaders** (3x3 patch SSD).
  - [x] Refactor `GpuSearcher` to handle **RHI readback** (blocking worker thread).
  - [x] Implement **GPU-to-CPU transfer** of search indices and SSD values.
  - [x] Integrate GPU search results into the **BM3D aggregation phase** in `Denoiser.cpp` as a search seed.
  - [x] Implement **CPU fallback** (handled automatically by checking RHI availability).
- [x] **Asynchronous Workflow**
    - [x] Run heavy denoising in background thread.
    - [x] Implement "Applying denoise..." UI indicator with rotating loader.
    - [x] Implement automatic abort logic when switching photos.
    - [x] Implement dynamic proxy scaling based on viewport size and zoom level.

## Phase 13: High-Performance Denoise Control [IN PROGRESS]

- [ ] **Explicit Execution Control**
  - [ ] Add "Denoise" checkbox to UI.
  - [ ] Implement immediate abort logic when unchecking.
- [ ] **ROI-Driven Proxy Refinement**
  - [ ] Update zoom logic to re-render visible crop in high-fidelity first.
  - [ ] Apply BM3D only to the high-quality visible region.
- [x] **Lifecycle Management**
  - [x] Ensure `RawEngine` destructor clean-joins all background workers.
  - [x] Prevent segfaults on application close while denoising (Fixed race in `ThumbnailProvider` and RHI resource cleanup).
- [x] **Viewport Constraints & Polish**
  - [x] Lock panning when zoom <= 100% (center image).
  - [x] Constrain pan offset to image boundaries when zoomed in.
  - [x] Implement double-click zoom cycle (100% -> 200% -> 400% -> 100%).
  - [x] Fix pan/zoom interaction bug where double-click zoom triggered during pan.
- [x] Fix "Zoom Increases" bug during panning (Race condition in ROI calculation).
  - [ ] Fix rendering artifacts during zoom/pan with active denoising.

## Phase 16: Responsive Preview System [DONE]

- [x] **Preview Engine (C++)**
  - [x] Implement `PreviewManager` for background 1080p proxy generation.
  - [x] Support intelligent cache invalidation based on sidecar timestamps.
  - [x] Integrate `ImageDeveloper` for applying edits to background previews.
  - [x] **UI Integration**
  - [x] Update `RawViewport` to support "Proxy-First" loading.
  - [x] Implement seamless cross-fade/swap between JPEG proxy and developed RAW.
  - [x] Trigger background refresh when edits are committed in Develop view.
  - [x] Add arrow key navigation for filmstrip.

## Phase 17: Async Preview Loading & Image Swap Fix [DONE]

- [x] **Fix Preview Display Glitch**
  - [x] Clear preview image immediately when switching photos in `RawEngine::setSource`.
  - [x] Update `RawViewport::setSource` to force immediate clear of texture node.
  - [x] Modify `RawViewport::updatePaintNode` to return nullptr when no current image data available.
  - [x] Add `m_showingPreview` flag to track preview vs full-res state.
- [x] **Async Preview Loading**
  - [x] Preview loads instantly from cache while RAW develops in background.
  - [x] Seamless swap from preview to full-resolution when RAW is ready.
  - [x] Show loading state (blank/empty) when no preview available for current image.

## Backlog / Future

- [ ] **Crop & Transform**
  - [ ] Aspect ratio selection (1:1, 4:5, 16:9, etc.).
  - [ ] Straighten tool and arbitrary rotation.
  - [ ] Perspective correction.
- [ ] **Lens Correction**
  - [ ] Integrate `lensfun` for automatic distortion/vignette removal.
- [ ] Tone Curve (Spline UI).
- [ ] Export functionality (Save to JPEG/TIFF).
- [ ] Multi-image batch processing.
