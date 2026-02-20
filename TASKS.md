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

## Phase 10: Enhanced Demosaicing [DONE]

Based on analysis of darktable's demosaicing implementation at `tmp/darktable/src/iop/demosaic.c` and `tmp/darktable/src/iop/demosaicing/`.

- [x] **Demosaic Engine**
  - [x] Create DemosaicEngine class architecture in `src/engine/DemosaicEngine.h`
  - [x] Implement PPG (Patterned Pixel Grouping) algorithm - fast, good quality
  - [x] Implement RCD (Ratio Corrected Demosaicing) algorithm stub - full implementation requires tiling support
  - [x] Add demosaicing method selection property to RawEngine
  - [x] Integrate with RawEngine to bypass LibRaw's dcraw_process when using custom algorithms
  - [x] Expose demosaicing method selection in QML UI
- [ ] **Algorithm Research**
  - [ ] Research darktable's RCD full implementation with tiling for memory efficiency
  - [ ] Research AMaZE (Aliasing Minimization and Zipper Elimination) algorithm
  - [ ] Research VNG4 (Variable Number of Gradients) for special cases
  - [ ] Compare quality/speed tradeoffs of different algorithms

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
