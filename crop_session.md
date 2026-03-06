# Crop & Geometry Session Context Dump

**Session ID:** 4c50a3f7-cbab-420d-840c-0afec4ca1cab  
**Date:** 2026-03-05  
**Branch:** `feature/crop-and-geometry`  
**Base:** `develop` (commit `2fe04a8`)

---

## 1. Session Overview

This session spans multiple phases of Photon development, focused primarily on:

1. **Enhanced Denoiser** (Phase 24) — Multi-Scale Guided Filter for chroma, SIMD-optimized
2. **GPU Chroma Filter** — Vulkan compute-based chroma denoising
3. **Edge Masking** for selective sharpening
4. **Phase 28** — Histogram, library improvements, masking groundwork
5. **Tone Curve, Library Sort, Auto-Scan**
6. **Preview Serialization, Spline Hardening, Filmstrip Sort**
7. **Crop & Geometry Panel** (Phase 30) — The primary ongoing work

---

## 2. Crop & Geometry Architecture

### 2.1 Data Model (Q_PROPERTYs on RawEngine → RawViewport)

| Property           | Type     | Default     | Description                       |
| ------------------ | -------- | ----------- | --------------------------------- |
| `cropRect`         | `QRectF` | `(0,0,1,1)` | Normalized crop (0–1 for x,y,w,h) |
| `cropAspectRatio`  | `float`  | `-1` (free) | Locked ratio, 0=original, -1=free |
| `straightenAngle`  | `float`  | `0.0`       | Fine rotation ±45°                |
| `orientationSteps` | `int`    | `0`         | 90° increments (0–3)              |
| `flipHorizontal`   | `bool`   | `false`     | Mirror H                          |
| `flipVertical`     | `bool`   | `false`     | Mirror V                          |

### 2.2 Two-Phase Architecture: Edit vs Display

**Edit Mode (crop mode active):**

- `enterCropMode()` unbakes geometry → shows original image
- QML visual transforms (`Rotation`, `Scale`) applied on ShaderEffect in App.qml
- CropOverlay sits on top, OUTSIDE the transform group
- CropOverlay computes `displayRect` by transforming `imageRect` through orientation rotation + inscribed shrinkage
- Crop coordinates are [0,1] normalized to displayRect
- User edits crop interactively

**Display Mode (after pressing Enter):**

- `exitCropMode()` → `reloadWithGeometry()` re-decodes RAW and applies all transforms at pixel level
- `applyGeometryTransforms()` pipeline: orientation → flip → straighten+autocrop → crop
- Result stored in `m_geometryBuffer` (pixel buffer)
- `m_geometryBaked = true` → QML transforms become neutral (angle=0, scale=1)
- Viewport displays the baked result directly

### 2.3 Key Coordinate Systems

**Normalized crop coords** `[0,1]`:

- `(0,0)` = top-left of displayRect, `(1,1)` = bottom-right
- Stored in `cropRect` Q_PROPERTY
- Applied identically in QML (displayRect mapping) and C++ (pixel mapping on inscribed image)

**displayRect** (CropOverlay.qml):

1. Start with C++ `imageRect` (viewport pixel coords, aspect-ratio fitted)
2. Apply 90° orientation rotation around viewport center
3. Apply flip
4. Shrink for straighten inscribed rectangle

**Inscribed rectangle formula** (identical in QML and C++):

```
s = min(W / (W*cosθ + H*sinθ), H / (W*sinθ + H*cosθ))
inscribedW = W*s, inscribedH = H*s
```

### 2.4 Geometry Bake Pipeline (C++ `applyGeometryTransforms`)

Location: `src/engine/RawEngine.cpp` ~line 2462

```
Input: QImage (from LibRaw decode)
  ↓
1. Orientation (90° pixel rotation via QImage::transformed)
  ↓
2. Flip H/V (QImage::mirrored)
  ↓
3. Straighten: QTransform::rotate(θ) → autocrop to inscribed rect
  ↓
4. Crop: apply normalized [0,1] rect on autocropped image
  ↓
Output: Final QImage stored in m_geometryBuffer
```

### 2.5 Crop Mode Flow

```
User clicks Crop tab (sidebar index 2)
  → App.qml calls rawViewport.enterCropMode()
  → RawEngine: m_inCropMode = true, unbake geometry, show original
  → QML transforms active, CropOverlay visible

User adjusts crop/straighten/orientation
  → Properties update in real-time (QML visual feedback)
  → commitEdit() called on release (saves to sidecar JSON)

User presses Enter
  → App.qml calls rawViewport.exitCropMode()
  → RawEngine: reloadWithGeometry() → re-decode + applyGeometryTransforms
  → Baked result in m_geometryBuffer, m_geometryBaked = true
  → QML transforms neutral, viewport shows baked pixels

User presses Escape
  → Discard changes, restore saved state
```

---

## 3. Key Files and Their Roles

### C++ Backend

| File                             | Role                                                                                                                                                |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/engine/RawEngine.h`         | Q_PROPERTYs, geometry state (m_geometryBuffer, m_geometryBaked, m_inCropMode), method declarations                                                  |
| `src/engine/RawEngine.cpp`       | `applyGeometryTransforms()`, `reloadWithGeometry()`, `enterCropMode()`, `exitCropMode()`, `getProcessedData()` (returns geometry buffer when baked) |
| `src/components/RawViewport.h`   | Q_INVOKABLE proxies, geometryBaked Q_PROPERTY                                                                                                       |
| `src/components/RawViewport.cpp` | `calculateTargetRect()`, `updatePaintNode()` (syncs dimensions from buffer)                                                                         |
| `src/engine/ImageDeveloper.cpp`  | Export pipeline with same geometry transforms                                                                                                       |

### QML Frontend

| File                                 | Role                                                                                                                                  |
| ------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------- |
| `content/views/App.qml`              | ShaderEffect + QML Rotation/Scale transforms, CropOverlay placement, Enter/Escape shortcuts, sidebar tab switching                    |
| `content/components/CropOverlay.qml` | Interactive crop overlay: displayRect computation, mask rects, 8 drag handles, center drag, straighten tool, aspect ratio enforcement |
| `content/components/CropPanel.qml`   | Sidebar panel: aspect ratio presets, straighten slider, orientation buttons, reset                                                    |

### Key Line References (approximate, may shift)

- **App.qml ShaderEffect transforms:** ~line 292-309
- **App.qml CropOverlay placement:** ~line 404-413
- **App.qml Enter/Escape shortcuts:** ~line 149-164
- **CropOverlay displayRect:** lines 27-77
- **CropOverlay handle drag:** lines 182-310
- **CropOverlay center drag:** lines 312-350
- **CropPanel straighten slider:** lines 278-301
- **CropPanel orientation buttons:** lines 320-390
- **RawEngine applyGeometryTransforms:** ~line 2462
- **RawEngine reloadWithGeometry:** ~line 2539
- **RawEngine enterCropMode:** ~line 2631
- **RawEngine exitCropMode:** ~line 2664
- **RawEngine geometry load watcher:** ~line 168

---

## 4. Chronological History of Crop Work

### Phase 1: Foundation (commit `07cca7f`)

- Added 6 Q_PROPERTYs to RawEngine/RawViewport
- Created CropPanel.qml sidebar panel
- Created CropOverlay.qml with interactive handles
- Added QML visual transforms on ShaderEffect
- Added straighten tool (draw-line mode)
- Export integration in ImageDeveloper

### Phase 2: Aspect Ratio Fixes (commit `023dd69`)

- Fixed aspect ratio enforcement during handle drag
- Fixed `computeCropForRatio()` to account for orientation steps
- Fixed landscape/portrait toggle

### Phase 3: Baked Geometry Pipeline (commit `90caa85`)

- Implemented `applyGeometryTransforms()` reusing ImageDeveloper logic
- Added `reloadWithGeometry()` with async QtConcurrent worker
- Added `enterCropMode()` / `exitCropMode()`
- Modified `getProcessedData()` to return geometry buffer when baked
- Made QML transforms conditional on `geometryBaked`
- Wired Enter/Escape to commit/discard

### Phase 4: Crop Boundary Fixes (commits `90caa85` → `65eff82`)

- Fixed crop overlay bounds to respect the inscribed rectangle after straighten
- Fixed aspect ratio enforcement for all presets (1:1, 5:4, 4:3, etc.)
- Multiple iterations on the boundary computation
- User tested extensively with screenshots

### Phase 5: Crop-Straighten Visual Mismatch (checkpoint 015)

- **Problem:** When straighten + crop applied, baked result appeared "zoomed in" vs preview
- **Root cause:** QML Rotation tilts the image content, but the axis-aligned crop rect captures a diagonal slice. The bake produces level content, so same crop coords yield visually different results.
- **Math verified correct** — QML displayRect and C++ inscribed rect produce identical proportional coordinates (within 1-2 pixels)

### Phase 6: Bake-on-Enter Approach (checkpoint 016, then reverted)

- Attempted: pre-bake straightened image when entering crop mode
- Added `forCropMode` parameter to `reloadWithGeometry()`
- **User rejected:** "the image should not be baked during crop mode — user should be able to move around the full area"
- **Reverted:** Removed `forCropMode`, simplified `enterCropMode()` back to unbake-only

### Phase 7: Current State (latest uncommitted changes)

- `enterCropMode()` simply unbakes and shows original with QML transforms
- CropPanel actions call `commitEdit()` instead of `enterCropMode()`
- Reset straighten button now properly calls `commitEdit()`
- Fixed `straightenSlider` reference (was `parent.value`, now `straightenSlider.value`)
- Debug logging still present in RawEngine.cpp and CropOverlay.qml

---

## 5. Known Issues & Open Problems

1. When the user tilts/straighten the image WITHOUT cropping further, everything works and the crop selection is resized to fit in the image boundaries (inscribed). The problem is that the crop selection cannot move left and right even if there is room to do so.
2. If the image is cropped more than the inscribed resizing, the baked geometry is off: the image gets zoomed in and the aspect ratio is not preserved.

This signals a mismatch between the QML coordinates computed in the preview and the actual baking logic.

---

## 6. Uncommitted Changes (diff from HEAD `a9171eb`)

### Files changed

- `content/components/CropOverlay.qml` — Added debug logging (+31 lines)
- `content/components/CropPanel.qml` — Fixed slider reference, added `commitEdit()` calls (+13/-2)
- `content/views/App.qml` — `onStraightenFinished` calls `commitEdit()` (+5/-1)
- `src/engine/RawEngine.cpp` — Simplified `enterCropMode()`, debug logging (+20/-2)
- `src/main.cpp` — Commented out QSG debug env vars (+2/-2)

### Specific changes

1. **CropOverlay.qml:** `onActiveChanged` logs inscribed scale computation; `onCropRectChanged` logs displayRect and cropRect
2. **CropPanel.qml:**
   - `straightenSlider` id added, `parent.value` → `straightenSlider.value`
   - Reset straighten button: added `commitEdit("straighten")`
   - Straighten slider `onReleased` / `onDoubleClicked`: `commitEdit("straighten")`
   - All 4 orientation/flip buttons: `commitEdit("orientation")` / `commitEdit("flip")`
   - `resetCropGeometry()`: `commitEdit("geometry_reset")`
   - `computeCropForRatio()`: simplified (removed `geometryBaked` check)
3. **App.qml:** `onStraightenFinished` calls `commitEdit("straighten")` instead of `enterCropMode()`
4. **RawEngine.cpp:** `enterCropMode()` simplified — no longer attempts to bake, just unbakes and shows original. Debug logging in `applyGeometryTransforms`.
5. **RawEngine.h:** `reloadWithGeometry()` signature simplified (no `forCropMode` param)

---

## 7. Build & Test Status

```bash
# Build (from project root)
cd build && cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j$(nproc)

# Tests
cd build && ctest --output-on-failure
```

**Last verified:** All tests pass (2/2: tst_RawEngine, tst_AppStateManager)

---

## 8. Git History on Branch

```
a9171eb (HEAD) wip: crop rotation almost ok
65eff82 wip: crop rotation almost ok
9c6f3d8 wip: crop rotation ok for 1:1
90caa85 wip: baked geometry ok
023dd69 wip: aspect ratio ok
07cca7f wip: foundation ok
2fe04a8 (develop) Merge pull request #4
```

---

## 9. SQL Todo Status

| ID              | Title                              | Status      |
| --------------- | ---------------------------------- | ----------- |
| engine-state    | Add geometry state to RawEngine    | done        |
| engine-apply    | Add applyGeometryTransforms method | done        |
| engine-reload   | Add reload methods                 | done        |
| engine-getdata  | Modify getProcessedData            | done        |
| engine-denoise  | Handle denoise with geometry       | done        |
| viewport-bridge | Expose on RawViewport              | done        |
| qml-transforms  | Conditional QML transforms         | in_progress |
| qml-commit      | Wire up crop workflow              | in_progress |
| build-test      | Build and test                     | pending     |

---

## 10. Stored Repository Memories

- **Build commands:** `cd build && cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j$(nproc)`, tests: `cd build && ctest --output-on-failure`
- **Concurrency:** PreviewManager runs on QThreadPool (no QRhi there); Vulkan compute serialized via `computeMutex()`
- **GPU compute pattern:** GpuChromaFilter follows GpuSearcher pattern (raw VulkanComputeContext, HOST_VISIBLE SSBOs)
- **⚠️ OUTDATED memory:** "Visual rotation/flip/straighten is done in the fragment shader" — INCORRECT. Straighten is done via QML Rotation transform on ShaderEffect item, NOT in the fragment shader.

---

## 11. Next Steps

1. **Decide on crop-straighten mismatch strategy** — implement a proper fix
2. **Clean up debug logging** from RawEngine.cpp and CropOverlay.qml
3. **Test edge cases:** Large straighten angles, all aspect ratios, orientation + straighten combos
4. **Update SPECIFICATION.md** with baked geometry pipeline documentation
5. **Update TASKS.md** with Phase 30 completion status
