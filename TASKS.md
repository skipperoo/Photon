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
  - [x] **Effects Section:** Clarity, Dehaze, Structure (Placeholders connected).
  - [x] **Detail Section:** Sharpening, Noise Reduction (Placeholders connected).
- [ ] **Histogram Component**
  - [ ] Compute RGB/Luma distribution in C++ (Async).
  - [ ] Render histogram overlay in QML/C++.
- [ ] **Non-Destructive Edit Stack**
  - [ ] Store edits in `AppState`.
  - [ ] Save/Load edits to `.PhotonData/edits/*.json`.

## Phase 5: Advanced Shader Enhancements (RapidRAW Port)

- [ ] **Core Color Science**
  - [ ] Port sRGB <-> Linear transformations.
  - [ ] Implement AgX Tone Mapping (Full transform).
- [ ] **Advanced Color Tools**
  - [ ] Implement White Balance (Temperature & Tint).
  - [ ] Port HSL Panel (8-range Hue/Sat/Lum adjustments).
  - [ ] Port Color Grading (Shadows/Midtones/Highlights).
- [ ] **Creative & Technical Filters**
  - [ ] Implement Film Grain (Gradient Noise based).
  - [ ] Implement Advanced Vignette (Midpoint/Roundness/Feather).
  - [ ] Add Dithering for high-precision output.

## Phase 6: UX Enhancements

- [ ] **EXIF Metadata**
  - [ ] Show EXIF metadata
  - [ ] Pose the foundation to enable automatic lens correction
- [ ] **Implement Lens Correction**
  - [ ] Clone lensfun_db and integrate it
  - [ ] Add contextual menu to select lens correction (and correction amount)
  - [ ] Add button to detect lens from EXIF metadata and match it with lensfun_db

## Backlog / Future

- [ ] Tone Curve (Spline UI).
- [ ] Export functionality (Save to JPEG/TIFF).
- [ ] Multi-image batch processing.
