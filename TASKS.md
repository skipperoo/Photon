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

## Backlog / Future
- [ ] Tone Curve (Spline UI).
- [ ] Export functionality (Save to JPEG/TIFF).
- [ ] Multi-image batch processing.
