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

## Phase 5: Advanced Features & Refinement [IN PROGRESS]

- [ ] **Histogram Component**
  - [ ] Compute RGB/Luma distribution in C++ (Async).
  - [ ] Render histogram overlay in QML/C++.
- [ ] **Technical Polish**
  - [ ] Implement Dithering for high-precision output.
  - [ ] Optimize GPU pipeline performance.

## Phase 6: UX Enhancements

- [ ] **EXIF Metadata**
  - [ ] Show EXIF metadata.
  - [ ] Orient thumbnails using EXIF orientation.
- [ ] **Lens Correction**
  - [ ] Integrate `lensfun` for automatic distortion/vignette removal.
  - [ ] Add lens detection and selection UI.

## Backlog / Future

- [ ] Tone Curve (Spline UI).
- [ ] Export functionality (Save to JPEG/TIFF).
- [ ] Multi-image batch processing.
