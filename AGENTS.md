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
- `docs/`: Living developer documentation (`pipeline.md` = how the develop pipeline works and its change history; `features.md` = algorithm/purpose history per feature).
- `research/`: Local-only study material (gitignored; pipeline/shader references and notes).
- `.PhotonData/`: (Runtime) Hidden directory for sidecar metadata and cache.

## 📋 Backlog & Feature Workflow

The active plan lives in `TASKS.md` under **Phase 40: Enhancements & Refinements** (plus its `Future work - To Be Discussed` list). Work is done one task at a time:

1. Pick the next task from the Phase 40 status board (default order; re-prioritize only by agreement).
2. Implement for **all pipeline paths**: GPU preview (`RawViewport.frag` + QML shader graph), CPU export (`ImageDeveloper.cpp`) and live histogram (`RawEngine`). They must agree.
3. Build, run automated tests, and smoke-run the app.
4. Ask the user for **visual confirmation** with exact photos/steps/expected result. **Never commit before confirmation.**
5. After confirmation: commit, update the status board + task notes in `TASKS.md`, add the feature to `docs/features.md`, and update `docs/pipeline.md` if the stage order changed.
6. Pick the next task.

- When requirements are ambiguous, or a hard/design decision is needed (domain, persistence, licensing, data sources), **stop and ask** instead of guessing.
- Items not selected for the current phase go to `Future work - To Be Discussed` in `TASKS.md`.

## 📚 Documentation Duties

Every completed feature must leave behind:

- **`docs/pipeline.md`** — updated stage order (GPU chain, CPU export, histogram) and a change-history entry (date, feature, one-line purpose).
- **`docs/features.md`** — a section with: purpose, algorithm/formulas, pipeline placement, key constants, files touched, date.
- **`AGENTS.md`** — only if conventions, commands or architecture changed.

Keep local study notes in `research/` as the deep reference for the current phase (gitignored).

## 📜 Development Conventions

### General Rules

- **GIT POLICY:** NEVER merge changes or perform git operations (commit, push, checkout) unless explicitly asked by the user.
- **STRICT SAFETY RULE:** ALWAYS build and manually RUN the application (`./build/Photon`) to verify runtime stability and UI correctness BEFORE merging any changes.
- **STRICT CODE INTEGRITY:** NEVER use ellipses (`...`) placeholders. Provide full content for all modifications.

### Logging Rule

- **Logging Prefix:** ALWAYS add logs using the prefix `[ FileName.cpp ] - message`.
- Use only `LogManager` for all trace and error information.

### C++ Style (Backend)

- **Standard:** C++20.
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
