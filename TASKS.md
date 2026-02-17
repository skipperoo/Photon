# 📋 Project Photon Task List - COMPLETED

## Phase 0: Infrastructure & Setup

- [x] **Test Environment Setup**
  - [x] Create `tests/auto/` directory.
  - [x] Configure `tests/CMakeLists.txt` for QtTest integration.
  - [x] Create a dummy test (e.g., `tst_RawEngine_dummy.cpp`) to verify `ctest` works.
- [x] **CI/Lint Configuration**
  - [x] Verify `.clang-format` exists or create one (Google style).
  - [x] Add a `make lint` or similar target to CMake if possible, or document usage (done in AGENTS.md).

## Phase 1: The "Real" Image (Core Engine)

- [x] **RawEngine Implementation (C++)**
  - [x] Create `src/engine/RawEngine.h` and `.cpp`.
  - [x] Implement `loadRawFile(QString path)` using LibRaw.
  - [x] Implement `getThumbnail()` for fast previews.
  - [x] Implement `getRawData()` to return the unpacked 16-bit buffer.
  - [x] **Unit Test:** Write `tst_RawEngine` to verify loading valid/invalid files.
- [x] **RawViewport Integration**
  - [x] Update `RawViewport.h` to include `RawEngine`.
  - [x] Expose `source` property to QML.
  - [x] Implement texture upload in `updatePaintNode` using the data from `RawEngine`.

## Phase 2: User Interface (Shadcn/Zinc Theme)

- [x] **Theme System**
  - [x] Create `content/theme/Theme.qml` singleton.
  - [x] Define Zinc colors (Zinc-950 for bg, Zinc-50 for text, etc.).
  - [x] Define Typography constants (Inter font family).
- [x] **Core Components**
  - [x] `Button.qml`: Base button with hover states.
  - [x] `Slider.qml`: Custom slider for exposure/contrast.
  - [x] `Card.qml`: Container with subtle borders.
- [x] **Layout Implementation**
  - [x] Implement "Holy Grail" layout in `App.qml` (Sidebar, Viewport, Filmstrip).
  - [x] Create `LibraryView.qml`.
  - [x] Create `DevelopView.qml`.
  - [x] Create `SettingView.qml` with a placeholders.

## Phase 3: Processing Pipeline

- [x] **Basic Image Processing**
  - [x] Implement `setExposure(float ev)` in `RawEngine`.
  - [x] Apply exposure scaling to the 16-bit buffer before texture upload.
  - [x] Connect QML Slider -> C++ Slot.
- [x] **Asynchronous Loading**
  - [x] Move file IO to a `QRunnable` or `std::thread`.
  - [x] Implement `imageLoaded` signal to update UI when ready.

## Backlog / Future

- [ ] Histogram implementation (Compute in C++, render in QML).
- [ ] File Browser / Library Grid View.
- [ ] Export functionality (Save to JPEG/TIFF).
- [ ] GPU Compute Shaders for real-time adjustments.
