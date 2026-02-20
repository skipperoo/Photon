# 🤖 Photon Agent Guide

## 1. Build, Test & Lint Commands

**Build (Debug):**
```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)
```

**Run Application:**
```bash
./build/Photon
```

**Run All Tests:**
```bash
cd build && ctest --output-on-failure
```

**Run Single Test:**
To run a specific test case (e.g., `testLoadInvalidFile` in `tst_RawEngine`):
```bash
# Option 1: Via CTest (Recommended)
cd build && ctest -R "tst_RawEngine" -V

# Option 2: Run executable directly
./build/tests/auto/tst_RawEngine "testLoadInvalidFile"
```

**Linting & Formatting:**
```bash
# Apply clang-format to C++ files
find src tests -name "*.cpp" -o -name "*.h" | xargs clang-format -i -style=file

# Verify QML format (if qmlformat is available)
qmlformat -i content/**/*.qml
```

## 2. Code Style & Conventions

### C++ (Backend)
- **Standard:** C++17.
- **Formatting:** Follow `.clang-format` (Google/Qt style). Indentation: 4 spaces.
- **Naming:**
  - Classes: `PascalCase` (e.g., `RawEngine`).
  - Methods/Variables: `camelCase` (e.g., `loadFile`, `imageBuffer`).
  - Private Members: `m_variableName`.
  - Constants: `kConstantName` or `ALL_CAPS`.
- **Imports:**
  1. Local headers (`"RawEngine.h"`)
  2. Qt headers (`<QImage>`, `<QObject>`)
  3. Standard Lib (`<memory>`, `<vector>`)
  4. 3rd Party (`<libraw/libraw.h>`)
- **Memory:**
  - **Strict RAII:** Use `std::unique_ptr` for non-QObject resources (like LibRaw instances).
  - **Qt Parent/Child:** Use raw pointers for `QObject` hierarchies where the parent takes ownership.
- **Error Handling:**
  - Return `bool` or `std::optional<T>` for fallible operations.
  - Use `qWarning()`, `qCritical()` for logging errors.
  - **Never** use C++ exceptions across the QML/C++ boundary.
- **Comments:** Explain *why*, not *what*. Document public slots exposed to QML.

### QML (Frontend)
- **Formatting:** Declarative structure. Properties first, then signals, then child objects.
- **Ids:**
  - Root item: `id: root`.
  - Children: Descriptive `camelCase` (e.g., `submitButton`, `previewImage`).
- **Bindings:** Prefer declarative bindings (`width: parent.width * 0.5`) over imperative assignments.
- **Theme:** **ALWAYS** use the `Theme` singleton (e.g., `color: Theme.background`). **NEVER** hardcode colors/fonts.
- **Signals:** Use arrow functions for short handlers: `onClicked: () => root.process()`.

## 3. Project Architecture

**Structure:**
- `src/`: C++ Backend (Business Logic, Image Processing).
- `content/`: QML Frontend (UI, Views, Components).
- `cmake/`: Build configuration.

**Integration Pattern:**
- **MVVM-lite:** QML handles the View. C++ classes (exposed via `QML_ELEMENT`) handle the Model/ViewModel.
- **Rendering:** `RawViewport` (C++) renders the image content via Qt RHI/Vulkan.
- **UI Logic:** Signals flow up from QML components -> View -> C++ Slots.

## 4. Development Roadmap & Master Plan

### Phase 1: The "Real" Image (Priority)
**Goal:** Replace the dummy cyan box with a real rendered RAW image.
1.  **RawEngine:** Implement LibRaw wrapper (`loadFile`, `getRawData`).
2.  **RawViewport:** Connect `RawEngine` to the custom QQuickItem.
3.  **Texture:** Upload raw buffer to GPU texture.

### Phase 2: Asynchronous Loading
**Goal:** Unblock the UI during file IO.
1.  **Threading:** Move `libraw.open_file()` to a worker thread/`QRunnable`.
2.  **Signals:** Emit `imageReady()` when texture upload can proceed on the render thread.

### Phase 3: Processing Pipeline
**Goal:** Implement basic image adjustments.
1.  **Pipeline:** `Raw Data (16-bit)` -> `Apply Exposure/WB` -> `Texture`.
2.  **Sliders:** Connect QML sliders to C++ `setExposure(float)` slots.

## 5. Definition of Done
1.  Builds cleanly on Linux (GCC).
2.  Unit tests pass (`ctest`).
3.  Code is formatted.
4.  No memory leaks (check with Valgrind if possible).
