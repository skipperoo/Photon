# 🎨 Photon UI/UX Specification

**Vision:** A high-performance, native C++/Qt implementation of a modern RAW editor.
**Visual Reference:** The UI should closely mirror the clean, dark aesthetic of [RapidRAW](https://github.com/CyberTimon/RapidRAW).

**Theme:** "Zinc Dark" (Shadcn). Flat, borderless inputs, subtle gradients on hover, rigorous spacing.

---

## 1. 📂 Data & Project Structure

To ensure non-destructive editing and high performance, Photon manages a sidecar directory automatically.

**Logic:**

- When a folder is opened, Photon checks for/creates a `.PhotonData` folder (hidden on Linux/Mac, standard folder on Win).
- **Edit Stack:** Edits are stored as a **JSON array of objects**. Each object represents a complete state of the adjustment parameters.
- **Commit Logic:** While a slider is moving, the GPU updates in real-time (60fps). A new state is only appended to the JSON array when the user **releases** the slider, provided the value is different from the previous state.
- **Cache:** Stores generated thumbnails to avoid re-parsing RAW files on every launch.

**Directory Tree:**

```text
/User/Pictures/Vacation2024/
├── IMG_001.ARW
├── IMG_002.ARW
└── .PhotonData/
    ├── edits/
    │   ├── IMG_001.json  # Contains [ {exposure: 0.0, ...}, {exposure: 0.5, ...} ]
    │   └── IMG_002.json
    └── cache/
        ├── thumbnails/
        │   ├── IMG_001.jpg
        │   └── IMG_002.jpg
```

---

## 2. 🏠 The Welcome Screen

**Layout:** Split View (50% / 50%).

- **Left Pane:** A high-quality, random aesthetic photograph (abstract/landscape). Fills height, cropped to cover.
- **Right Pane:** Minimalist interaction zone. Centered vertically.

**Elements:**

1. **"Continue Session" Button:**

- _Condition:_ Only visible if a `last_opened_folder` exists in global config.
- _Action:_ Immediately loads the last directory and jumps to Library View.
- _Style:_ Primary (White/Bright) Shadcn Button.

1. **"Open Folder" Button:**

- _Action:_ Opens system file picker.
- _Style:_ Secondary (Dark Grey/Outline) Shadcn Button.

1. **Settings Icon (⚙️):**

- _Position:_ Top-right or below buttons.
- _Action:_ Opens a modal overlay with Application Preferences.

### Application Settings (Phase 11)

Photon provides advanced control over performance and aesthetics:

1.  **GPU Selection:**
    *   **Finding:** On Linux/NVIDIA systems, forcing the GPU requires setting `QSG_RHI_DEVICE_INDEX`, `QT_VULKAN_DEVICE_INDEX`, and `MESA_VK_DEVICE_SELECT` environment variables *before* the graphics driver initializes. For NVIDIA Prime, `__NV_PRIME_RENDER_OFFLOAD=1` and `__GLX_VENDOR_LIBRARY_NAME=nvidia` are mandatory.
    *   Dynamically detects available Vulkan-compatible physical devices.
    *   Allows users to select a specific GPU for RHI rendering.
    *   Changes may require an application restart.
2.  **Aesthetics:**
    *   **Theme:** Toggle between "Zinc Dark" and "Zinc Light".
    *   **Accent Color:** Choose from a predefined palette of high-contrast colors (Blue, Rose, Green, Orange).
3.  **Cache Management:**
    *   Option to clear the `.PhotonData/cache/thumbnails` directory.
    *   Only active when a workspace is currently loaded.
4.  **Logging:**
    *   Integrated `Logger` class for system diagnostics.
    *   User-definable log file location with sensible defaults.

---

## 3. 📚 Library View (Grid Mode)

**Goal:** Culling and organization.
**Layout:**

- **Top Bar:**
  - **Filter Strip:** Toggle buttons for "★ 1" to "★ 5" and Flags (Picked/Rejected).
  - **Sort:** Dropdown (Filename, Date, Rating).

- **Central Grid:**
  - Responsive Grid of Image Cards.
  - Each Card displays: Thumbnail, Filename, Rating Stars (overlay on hover).

- **Interaction:**
  - **Hover:** Shows metadata overlay (ISO, Shutter, Aperture).
  - **Single Click:** Selects image (Blue border).
  - **Double Click:** Transitions to **Develop View**.
  - **0-5 Keys:** Sets rating for selected photo(s).

---

## 4. 🎛️ Develop View (Edit Mode)

**Goal:** Precision editing with a unified workflow.
**Layout:** Unified Right-Stack Layout.

### A. The Viewport (Center/Left - Dominant)

- **Content:** The `RawViewport` (C++ Vulkan Widget).
- **Behavior:**
  - Pan (Space + Drag) & Zoom (Scroll Wheel).
  - Floating toolbar at the bottom for Zoom, Undo/Redo, and **Restore to Original**.
  - **Note:** The floating top navigation bar is **disabled** in this view to maximize vertical space.

### B. The Tool Stack & Switcher (Right)

The right panel is divided into two parts: a **Tool Stack** (320px) and a **Section Switcher** (48px).

1.  **Section Switcher (Vertical Rail):**
    - A slim vertical bar on the far right containing Lucide icons for high-level mode switching.
    - **Metadata** (Info icon): Extensive EXIF and image info.
    - **Edit** (Settings icon): The primary adjustment sliders.
    - **Crop** (Crop icon): Aspect ratio and rotation tools.
    - **Lens** (Telescope icon): Lens correction and distortion management.
    - **Presets** (Bookmark icon): User-saved adjustment states.
    - **Export** (Download icon): High-quality export options (JPEG/TIFF).
    - **Library** (Books icon): One-click jump back to grid mode.
    - **Settings** (Gear icon): Application preferences.

2.  **Tool Stack (Dynamic Panel):**
    - A `StackLayout` that displays the selected mode's controls.
    - **Histogram:** (Pinned at the top of the stack). Professional real-time visualization of RGB and Luma distribution.

### Multi-Selection & Asset Management (Phase 14)

Photon supports professional asset management workflows:

1.  **Selection Logic:**
    - **Single Click:** Selects an image and clears previous selection (unless Ctrl/Shift held).
    - **Ctrl + Click:** Toggles selection of an individual image.
    - **Shift + Click:** Selects a range of images from the last selected to the current.
    - **Ctrl + A:** Selects all visible images in the current view.
2.  **Rating:**
    - Images can be assigned a rating from 0 to 5 stars.
    - Ratings are stored in the `.PhotonData/edits/` JSON sidecar.
    - Key 0-5 assigns rating to ALL currently selected images.
3.  **Filtering:**
    - The Library View features a filter strip to show only images matching a specific rating (e.g., ">= 3 stars").

### High-Quality Export (Phase 15)

Non-destructive edits are applied during the export process:

1.  **Engine:** A dedicated `ExportManager` handles background processing without blocking the UI.
2.  **Pipeline:** RAW -> Apply Kelvin WB -> Linear Exposure -> Processing Stack -> AgX Tonemapping -> Dithering -> Format Conversion.
3.  **Formats:**
    *   **JPEG:** 8-bit, configurable quality (1-100).
    *   **TIFF:** 8-bit or 16-bit for maximum archival quality.
4.  **Batching:** Multiple selected images can be exported in parallel using a worker thread pool.

### Responsive Preview System (Phase 16)

To ensure zero-latency feedback when switching photos, Photon implements a background proxy system:

1.  **Background Precomputation:** Upon opening a folder, a `PreviewManager` scans all images and begins generating 1080p JPEG proxies in `.PhotonData/cache/previews/`.
2.  **Instant Loading:** When a photo is selected, the UI immediately displays the cached JPEG proxy (if available) while the `RawEngine` develops the full-resolution RAW in the background.
3.  **Hybrid Rendering:** Once the RAW development is complete, the viewport seamlessly swaps the proxy for the real GPU-processed image.
4.  **Smart Invalidation:** Previews are automatically regenerated when:
    *   Edits are committed to an image.
    *   The sidecar JSON timestamp is newer than the cached preview.

### GPU Processing Pipeline (Phase 5)

To achieve professional-grade results, Photon employs a high-fidelity GPU pipeline:

1.  **Linear Workflow:** Input textures are converted from sRGB to **Linear Space** for all mathematical operations. This ensures correct light addition and blending.
2.  **White Balance:** Handled in linear space using a kelvin-based temperature shift and a magenta/green tint adjustment.
3.  **Demosaicing:** While Photon explored custom implementations of various demosaicing algorithms (including PPG and RCD), rigorous testing concluded that **LibRaw's native implementation** remains the superior choice. It provides the best balance of image reconstruction quality, artifact suppression, and computational performance for this project.
4.  **Tonemapping:** **AgX Sigmoid transform** is implemented to provide a filmic highlight roll-off and natural color compression, preventing "digital" clipping of bright highlights.
4.  **Grain:** High-quality **Film Grain** is implemented using a gradient noise algorithm, with controls for amount, size, and roughness. It is applied in linear-to-srgb space with a luma-based mask to protect shadows and highlights.
5.  **Vignette:** An **Advanced Vignette** system is implemented with midpoint, roundness, and feathering controls, allowing for precise artistic framing.
6.  **HSL Panel:** An **8-band HSL system** (Red, Orange, Yellow, Green, Aqua, Blue, Purple, Magenta) is implemented in the fragment shader. It uses weighted influence curves to allow targeted Hue, Saturation, and Luminance adjustments without causing artifacts.
7.  **Color Grading:** A professional **3-Way Color Grading** system is implemented, allowing independent tinting of **Shadows, Midtones, and Highlights**. It features global **Balance** and **Blending** controls to precisely manage tonal transitions.
8.  **Dithering:** High-quality dithering is implemented using a sine-based pseudo-random noise generator. It is applied to the final RGB output at a precision of 1/255 to mask banding artifacts and ensure smooth gradients on 8-bit displays.
9.  **Denoising Pipeline (Phase 12):** Photon employs a hybrid GPU/CPU architecture designed for professional performance:
    *   **GPU Preview (NLM):** A real-time **Non-Local Means (NLM)** filter runs in the fragment shader. It uses 3x3 patch comparisons within a 7x7 search window, providing high-fidelity spatial denoising at 60fps.
    *   **Full Quality Toggle:** A user preference in settings allows forcing the high-fidelity 2-step denoiser even during the preview phase.
    *   **GPU-Accelerated Search:** The computationally expensive patch-matching phase of the BM3D algorithm is offloaded to the GPU. An RHI-based offscreen pass computes the Sum of Squared Differences (SSD) using 3x3 patches across the search window and generates a spatial similarity index texture. This texture encodes the best-match offset and SSD value, which is then read back and used as a search seed for the CPU BM3D aggregation phase, ensuring high-quality clustering with minimal CPU overhead.
    *   **CPU Transform & Filter (SIMD):** The collaborative filtering is performed on the CPU using **AVX2 and FMA** instructions, protected by a `QMutex` to ensure thread safety with the LibRaw processor.
    *   **Adaptive Proxy Scaling:** Preview denoising resolution dynamically adjusts based on the viewport size and zoom level (`viewport * zoom * 1.5`), ensuring zero pixelation even at 400% zoom.
    *   **Asynchronous UX:** Background tasks are managed by a `QFutureWatcher`. Adjustment sliders remain interactive, and tasks are automatically aborted/restarted upon photo switching or parameter refinement.
    *   **ROI-Driven Refinement (Phase 13):** When zoomed in, the engine prioritizes high-quality re-rendering of the visible Region of Interest (ROI) before triggering the background denoiser on that specific area, ensuring maximum sharpness and speed.
    *   **Explicit Activation:** Denoising must be explicitly enabled via a checkbox. Disabling it immediately halts any background processing.
    *   **Lifecycle Safety:** To prevent segmentation faults during application teardown, Photon implements a strict ownership and cleanup hierarchy:
        *   **Thread Joining:** All background workers (LibRaw, BM3D Denoiser, Thumbnail generator) use dedicated thread pools that are explicitly joined in their respective destructors.
        *   **Deterministic Teardown:** Singletons (`LogManager`, `AppStateManager`) are parented to the `QGuiApplication` instance and reset their internal static pointers to `nullptr` upon destruction to prevent dangling pointer access during final process cleanup.
        *   **GPU Resource Release:** `RawViewport` implements the `releaseResources()` protocol to ensure all RHI-allocated textures and buffers are freed on the render thread while the graphics context is still valid.
        *   **Instance Scoping:** The `QVulkanInstance` is managed as a local variable in `main()` to ensure it persists until all QML-related teardown is complete but is destroyed before the application exits.

**Accordion Sections:**

1. **Light:**
- _Sliders:_ Exposure, Contrast.
- _Tone:_ Highlights, Shadows, Whites, Blacks.
- _Divider Line_
- _Presence:_ Vibrance, Saturation.

2. **Color (HSL):**
- 8-band selector (Red, Orange, Yellow, Green, Aqua, Blue, Purple, Magenta).
- Targeted Hue, Saturation, and Luminance sliders.

3. **Color Grading:**
- 3-way region selector (Shadows, Midtones, Highlights).
- Cinematic tinting (Hue, Saturation, Luminance) per region.
- Global Balance and Blending sliders.

4. **Tone Curve:**
- Editable Parametric Curve (Highlights, Lights, Darks, Shadows) + Point Curve UI.

5. **Effects:**
- Clarity (Mid-tone contrast).
- Dehaze (Atmospheric removal).
- Structure (Local detail).
- Vignette (Midpoint, Roundness, Feather).

6. **Detail:**
- Sharpening (Amount, Radius, Masking).
- Noise Reduction (Luminance, Color).

### C. The Filmstrip (Bottom)

- [x] **Content:** Horizontal scrollable list of thumbnails from the current folder.
- [x] **Sync:** Highlighted thumbnail matches the main Viewport image.
- [x] **Navigation:** Left/Right Arrow keys move selection.

### D. Presets Panel (Left - Collapsible)

- **Storage:** Presets are stored as individual JSON files in the user's local data directory (`QStandardPaths::AppLocalDataLocation`).
  - **Linux:** `~/.local/share/photon/presets`
  - **macOS:** `~/Library/Application Support/photon/presets`
  - **Windows:** `%LOCALAPPDATA%/photon/presets`
- **Content:** A vertical list of user-saved preset names.
- **Behavior:**
  - Clicking a preset applies all contained adjustment parameters to the active image.
  - Applying a preset is a non-destructive action and adds a single step to the undo/redo stack.
  - Hovering over a preset name provides a "Delete" option.
  - A "Save Current" button at the top of the panel captures the current tool panel state into a new preset file.

---

## 5. 🖱️ Contextual Actions & Shortcuts

**Mouse Interactions:**

- **Right-Click on Filmstrip/Library:**
- _Context Menu:_
- "Copy Settings" (Ctrl+Shift+C)
- "Paste Settings" (Ctrl+Shift+V)
- "Reset to Original"
- "Export..."

- **Multi-Select:**
- Shift+Click to select a range.
- Ctrl+Click to toggle individual selection.
- _Paste Settings_ applies to ALL selected images.

**Keyboard Shortcuts:**

- **Arrows:** Navigate images.
- **0-5:** Set Star Rating.
- **P / U:** Pick / Unpick (Flag).
- **X:** Reject.
- **Ctrl+Z / Ctrl+Y:** Undo/Redo edit steps.

---

## 6. 👨‍💻 Reference Instruction

> **CRITICAL TASK FOR THE AGENT:**
>
> 1. **Clone & Inspect:** Clone the repository [https://github.com/CyberTimon/RapidRAW]().
> 2. **Analyze:** strictly analyze its UI layout. Look at how they handle the histogram and slider density.
> 3. **Replicate & Optimize:** We want that exact "Look & Feel"—the spacing, the font weights, the dark grey palette—but implemented using **Qt Quick (QML) + C++** instead of Web technologies.
> 4. **Performance:** Unlike Electron apps, our sliders must handle 60fps updates via the C++ `RawEngine`.
