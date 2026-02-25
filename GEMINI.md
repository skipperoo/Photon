# 🌌 Photon Project Context

Photon is a high-performance, native RAW image editor built with C++ and Qt Quick, designed for a modern, non-destructive editing workflow.

## 🏗️ Project Overview

- **Purpose:** A professional-grade RAW photo editor with a clean, dark aesthetic (inspired by RapidRAW/Shadcn).
- **Architecture:**
  - **Frontend:** Qt Quick (QML) for a responsive, modern UI.
  - **Backend:** C++17 for core logic, image processing (via LibRaw), and high-performance rendering.
  - **Rendering:** Custom `RawViewport` (C++ QQuickItem) utilizing Vulkan/Qt RHI for GPU-accelerated image display.
- **Data Model:** Non-destructive editing using `.PhotonData` sidecar directories containing JSON edit stacks and cached thumbnails.

## 🛠️ Tech Stack

- **Language:** C++17
- **Framework:** Qt 6.8+ (Quick, ShaderTools, Gui, RHI)
- **Libraries:** LibRaw (RAW decoding), Vulkan
- **Build System:** CMake 3.16+

## 🚀 Key Commands

### Build & Run

- **Build (Debug):**

  ```bash
  mkdir -p build && cd build
  cmake -DCMAKE_BUILD_TYPE=Debug ..
  make -j$(nproc)
  ```

- **Run Application:** `./build/Photon`

### Testing

- **Run All Tests:** `cd build && ctest --output-on-failure`
- **Run Single Test:** `cd build && ctest -R "test_name" -V` (or run executable directly in `./build/tests/auto/`)

### Formatting

- **C++:** `find src tests -name "*.cpp" -o -name "*.h" | xargs clang-format -i -style=file`
- **QML:** `qmlformat -i content/**/*.qml`

## 📂 Directory Structure

- `src/`: C++ source code (Engine, Viewport, Managers).
- `content/`: QML files, views, components, and UI themes.
- `cmake/`: Build configurations and modules.
- `.PhotonData/`: (Runtime) Hidden directory for sidecar metadata and cache.

## 📜 Development Conventions

### General Rules

- **GIT POLICY:** NEVER merge changes or perform git operations (commit, push, checkout) unless explicitly asked by the user.
- **STRICT SAFETY RULE:** ALWAYS build and manually RUN the application (`./build/Photon`) to verify runtime stability and UI correctness BEFORE merging any changes.
- **STRICT CODE INTEGRITY:** NEVER use ellipses (`...`) placeholders. Provide full content for all modifications.

### Logging Rule

- **Logging Prefix:** ALWAYS add logs using the prefix `[ FileName.cpp ] - message`.
- Use only `LogManager` for all trace and error information.

### C++ Style (Backend)

- **Standard:** C++17.
- **Naming:**
  - Classes: `PascalCase`.
  - Methods/Variables: `camelCase`.
  - Private Members: `m_variableName`.
- **Memory:** Strict RAII with `std::unique_ptr` for backend resources; parent-child ownership for `QObject` hierarchies.
- **Asynchrony:** Heavy IO (RAW loading) and processing MUST be handled in worker threads to maintain 60fps UI.

### QML Style (Frontend)

- **Theming:** ALWAYS use the `Theme` singleton (e.g., `color: Theme.background`). NEVER hardcode colors or spacing.
- **Structure:** Root item should use `id: root`. Properties first, then signals, then children.
- **Bindings:** Prefer declarative bindings over imperative assignments.

## 🏗️ Project Architecture

- **Integration:** MVVM-lite pattern. QML handles the View, C++ classes (exposed via `QML_ELEMENT`) handle the Logic/ViewModel.
- **Rendering:** Custom `RawViewport` (C++) renders content via Qt RHI/Vulkan.
- **Communication:** Signals flow from QML to C++ slots; properties are synchronized via `Q_PROPERTY` with notification signals.
