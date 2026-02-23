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
- **Build (Debug):** 
  ```bash
  mkdir -p build && cd build
  cmake -DCMAKE_BUILD_TYPE=Debug ..
  make -j$(nproc)
  ```
- **Run:** `./build/Photon`
- **Test:** `cd build && ctest --output-on-failure`
- **Format:** `find src tests -name "*.cpp" -o -name "*.h" | xargs clang-format -i`

## 📂 Directory Structure
- `src/`: C++ source code (Engine, Viewport components).
- `content/`: QML files, components, and UI themes.
- `cmake/`: Build configurations and modules.
- `.PhotonData/`: (Runtime) Hidden directory for sidecar metadata and cache.

## 📜 Development Conventions
- **GIT POLICY:** NEVER merge changes or perform git operations (commit, push, checkout) unless explicitly asked by the user.
- **STRICT SAFETY RULE:** ALWAYS build and manually RUN the application (`./build/Photon`) to verify runtime stability and UI correctness BEFORE merging any changes into the `develop` branch. Unit tests alone are insufficient for UI/Graphics verification.
- **STRICT CODE INTEGRITY:** NEVER remove chunks of code and replace them with ellipses (`...`) or any other placeholder. ALWAYS provide the full, complete content when using the `write` tool or accurate, context-rich strings when using the `edit` tool. Failure to do so breaks the build and loses functionality.
- **C++ Style:** Follows `.clang-format` (Google/Qt style). Use `m_member` for private variables and `PascalCase` for classes.
- **QML Style:** Use the `Theme` singleton for all styling (colors, spacing). Avoid hardcoding values.
- **Memory:** Strict RAII with `std::unique_ptr` for backend resources; parent-child ownership for `QObject` hierarchies.
- **Asynchrony:** Heavy IO (RAW loading) must be handled in worker threads to keep the UI at 60fps.

## 🗺️ Roadmap Highlights
- **Phase 1:** Real RAW rendering (LibRaw integration).
- **Phase 2:** Asynchronous file loading and texture uploading.
- **Phase 3:** Full processing pipeline (Exposure, WB, Tone Curve).
