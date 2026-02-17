# Photon Developer Guide

**Welcome to the Photon Development Team.**  
This document outlines the standards for contributing to Photon. As a high-performance graphics application, strict adherence to memory management, threading safety, and architectural boundaries is required.

## 1. 🐙 Git Workflow & Repository Rules

We follow a **Feature-Branch Workflow**. Direct commits to main are forbidden.

### Branching Strategy

- **main**: The stable production branch. Must always compile and launch.
- **feat/\<name\>**: For new features (e.g., feat/histogram, feat/raw-loader).
- **fix/\<name\>**: For bug fixes (e.g., fix/memory-leak-raw).
- **refactor/\<name\>**: For code cleanup without behavior changes.

### Commit Conventions

We use Conventional Commits to generate changelogs automatically.

- feat: ... -> Adds functionality (e.g., feat: implement exposure slider logic)
- fix: ... -> Fixes a bug (e.g., fix: resolve segfault on app exit)
- chore: ... -> Maintenance (e.g., chore: update CMake version)
- perf: ... -> Performance improvements (e.g., perf: move raw unpacking to worker thread)

### The .gitignore Standard

Ensure your local environment does not pollute the repo.

```.gitignore

# Build directories
build/
bin/
*.o
*.obj

# Qt Generated
moc_*
qrc_*
ui_*
Makefile
cmake_install.cmake
CMakeCache.txt

# IDEs
.vscode/
.idea/
*.user
```

## 2. Testing Strategy

Since Photon is a hybrid C++/QML application, we test the "Brains" (C++) and the "Face" (QML) separately.

### A. C++ Unit Tests (The Engine)

- **Framework:** QtTest (Native integration with QObjects).
- **Location:** tests/auto/
- **Rule:** Every major Engine feature (Raw Loading, Math operations) must have a unit test.
- **Command:** ctest (via CMake).

**Example Test Case (TestRawEngine.cpp):**

```c++

void TestRawEngine::testLoadInvalidFile() {
 RawEngine engine;
 bool success \= engine.loadFile("non_existent.arw");
 QVERIFY(success \== false);
}
```

### B. QML/UI Tests (The Interface)

- **Framework:** Qt Quick Test
- **Location:** tests/qml/
- **Rule:** Test standard user flows (e.g., "Clicking Export emits the export signal").

### C. Visual/Sanity Checks

- Since this is a graphics app, automated tests cannot easily verify "Does the image look correct?".
- **Rule:** Before merging a PR, run the app and visually confirm:
  1. The UI scales correctly (resize window).
  2. No visual artifacts in the Viewport.
  3. Console output is clean of ReferenceError or Binding loop detected.

## 3. 🎨 Coding Standards

### C++ (Back-End)

- **Standard:** C++17.
- **Memory Management:**
  - **Strict RAII:** Never use new without an immediate owner. Use std::unique_ptr or std::shared_ptr.
  - **Qt Ownership:** If a generic QObject has a parent, standard pointers are acceptable (Qt deletes children automatically).
- **Pointers:** Always initialize raw pointers to nullptr.
- **Headers:** Use #pragma once.
- **Includes:** Order: Local ("RawEngine.h") -> Qt (\<QImage\>) -> Std (\<vector\>).
- **Formatting:** Use clang-format (Google style or Qt style).

### QML (Front-End)

- **Ids:** Use id: root for the top-level item of a component. Use descriptive ids for children (id: submitButton).
- **Property Bindings:** PREFER bindings (width: parent.width \* 0.5) over imperative assignment (Component.onCompleted: width \= ...).
- **Signal Handlers:** Use arrow functions for short logic: onClicked: (mouse) \=\> root.process(mouse)
- **No Magic Numbers:** Use Theme.spacing, Theme.color, etc. Do not hardcode 10px or \#ffffff.

## 4. Performance Best Practices

Photon is a **Real-Time** application. Latency is the enemy.

1. **The "16ms" Rule:** Never block the Main Thread (GUI Thread) for more than 16ms.
   - _Bad:_ rawProcessor.process() (runs on main thread).
   - _Good:_ QtConcurrent::run(...) or a dedicated WorkerThread.
2. **Texture Uploads:**
   - Upload textures to GPU only when changed.
   - Reuse QSGTexture objects; do not delete and new them every frame.
3. **QML Complexity:**
   - Avoid complex JavaScript in onPaint or property bindings that trigger every frame.
   - Use Item.visible: false instead of opacity: 0 (save rendering cost).

## 5. Definition of Done (DoD)

A feature is considered "Done" only when:

1. [ ] It builds without warnings on Linux (GCC).
2. [ ] Unit tests pass (ctest).
3. [ ] No memory leaks detected (Run with valgrind periodically).
4. [ ] Code is formatted (clang-format).
5. [ ] Commit history is clean and squashed if necessary.
