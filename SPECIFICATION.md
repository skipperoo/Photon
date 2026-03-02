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

1. **GPU Selection:**
   - **Finding:** On Linux/NVIDIA systems, forcing the GPU requires setting `QSG_RHI_DEVICE_INDEX`, `QT_VULKAN_DEVICE_INDEX`, and `MESA_VK_DEVICE_SELECT` environment variables _before_ the graphics driver initializes. For NVIDIA Prime, `__NV_PRIME_RENDER_OFFLOAD=1` and `__GLX_VENDOR_LIBRARY_NAME=nvidia` are mandatory.
   - Dynamically detects available Vulkan-compatible physical devices.
   - Allows users to select a specific GPU for RHI rendering.
   - Changes may require an application restart.
2. **Denoising Engine:**
   - **Full Quality Toggle:** Users can force the high-fidelity 2-step BM3D denoiser during preview (otherwise single-step is used for speed).
   - **Architecture:** BM3D runs on the Y (luminance) channel only; chrominance (Cb/Cr) is denoised via a Multi-Scale Guided Filter using the denoised Y as structural guide.
   - **GPU Search Offload:** Patch-matching is optionally offloaded to a Vulkan compute pipeline via `GpuSearcher`.
   - **User-Tunable Parameters:** Exposed via QML sliders: Search Window (9-39), Group Size (4/8/16), Chroma Radius (1-16), Chroma Denoise (0-100). Serialized in `.PhotonData` edit stacks.
3. **Aesthetics:**
   - **Theme:** Toggle between "Zinc Dark" and "Zinc Light".
   - **Accent Color:** Choose from a predefined palette of high-contrast colors (Blue, Rose, Green, Orange).
4. **Cache Management:**
   - Option to clear the `.PhotonData/cache/thumbnails` directory.
   - Only active when a workspace is currently loaded.
5. **Logging:**
   - Integrated `Logger` class for system diagnostics.
   - User-definable log file location with sensible defaults.

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
  - **High-Performance Panning:** To ensure 60fps responsiveness during high-resolution RAW navigation, Photon uses a texture-caching strategy. Panning only updates the viewport geometry (quad coordinates) without re-uploading texture data to the GPU or performing CPU-side pixel conversions.

### B. The Tool Stack & Switcher (Right)

The right panel is divided into two parts: a **Tool Stack** (320px) and a **Section Switcher** (48px).

1. **Section Switcher (Vertical Rail):**
   - A slim vertical bar on the far right containing Lucide icons for high-level mode switching.
   - **Metadata** (Info icon): Extensive EXIF and image info.
   - **Edit** (Settings icon): The primary adjustment sliders.
   - **Crop** (Crop icon): Aspect ratio and rotation tools.
   - **Lens** (Telescope icon): Lens correction and distortion management.
   - **Presets** (Bookmark icon): User-saved adjustment states.
   - **Export** (Download icon): High-quality export options (JPEG/TIFF).
   - **Library** (Books icon): One-click jump back to grid mode.
   - **Settings** (Gear icon): Application preferences.

2. **Tool Stack (Dynamic Panel):**
   - A `StackLayout` that displays the selected mode's controls.
   - **Histogram:** (Pinned at the top of the stack). Professional real-time visualization of RGB and Luma distribution.

### Multi-Selection & Asset Management (Phase 14)

Photon supports professional asset management workflows:

1. **Selection Logic:**
   - **Single Click:** Selects an image and clears previous selection (unless Ctrl/Shift held).
   - **Ctrl + Click:** Toggles selection of an individual image.
   - **Shift + Click:** Selects a range of images from the last selected to the current.
   - **Ctrl + A:** Selects all visible images in the current view.
2. **Rating:**
   - Images can be assigned a rating from 0 to 5 stars.
   - Ratings are stored in the `.PhotonData/edits/` JSON sidecar.
   - Key 0-5 assigns rating to ALL currently selected images.
3. **Filtering:**
   - The Library View features a filter strip to show only images matching a specific rating (e.g., ">= 3 stars").

### High-Quality Export (Phase 15)

Non-destructive edits are applied during the export process:

1. **Engine:** A dedicated `ExportManager` handles background processing without blocking the UI.
2. **Pipeline:** RAW -> Apply Kelvin WB -> Linear Exposure -> Processing Stack -> AgX Tonemapping -> Dithering -> Format Conversion.
3. **Formats:**
   - **JPEG:** 8-bit, configurable quality (1-100).
   - **TIFF:** 8-bit or 16-bit for maximum archival quality.
4. **Batching:** Multiple selected images can be exported in parallel using a worker thread pool.

### Responsive Preview System (Phase 16-17)

To ensure zero-latency feedback when switching photos, Photon implements a background proxy system:

1. **Background Precomputation:** Upon opening a folder, a `PreviewManager` scans all images and begins generating 1080p JPEG proxies in `.PhotonData/cache/previews/`.
2. **Instant Loading:** When a photo is selected, the UI immediately displays the cached JPEG proxy (if available) while the `RawEngine` develops the full-resolution RAW in the background.
3. **Hybrid Rendering:** Once the RAW development is complete, the viewport seamlessly swaps the proxy for the real GPU-processed image.
4. **Smart Invalidation:** Previews are automatically regenerated when:
   - Edits are committed to an image.
   - The sidecar JSON timestamp is newer than the cached preview.

#### Async Preview Loading with Image Swap (Phase 17)

The preview system has been refined to eliminate visual glitches when switching photos:

1. **Immediate Clear:** When switching to a new photo, the viewport immediately clears the previous image display. This prevents the old image from being visible above the new one during the loading transition.
2. **Async Loading Flow:**
   - Photo selection triggers immediate display clear.
   - Preview JPEG loads asynchronously and displays as soon as available.
   - Full-resolution RAW develops in background.
   - Seamless swap from preview to full-res when RAW is ready (no flash or glitch).
3. **State Tracking:** The viewport tracks whether it's currently showing a preview (`m_showingPreview`) vs. the full-resolution image, ensuring proper aspect ratio and dimension handling throughout the transition.
4. **Loading Indicator:** While no preview is available, the viewport shows a blank/loading state rather than the previous image.

### GPU Processing Pipeline (Phase 5)

To achieve professional-grade results, Photon employs a high-fidelity GPU pipeline:

1. **Linear Workflow:** Input textures are converted from sRGB to **Linear Space** for all mathematical operations. This ensures correct light addition and blending.
2. **White Balance:** Handled in linear space using a kelvin-based temperature shift and a magenta/green tint adjustment.
3. **Demosaicing:** While Photon explored custom implementations of various demosaicing algorithms (including PPG and RCD), rigorous testing concluded that **LibRaw's native implementation** remains the superior choice. It provides the best balance of image reconstruction quality, artifact suppression, and computational performance for this project.
4. **Tonemapping:** **AgX Sigmoid transform** is implemented to provide a filmic highlight roll-off and natural color compression, preventing "digital" clipping of bright highlights.
5. **Grain:** High-quality **Film Grain** is implemented using a gradient noise algorithm, with controls for amount, size, and roughness. It is applied in linear-to-srgb space with a luma-based mask to protect shadows and highlights.
6. **Vignette:** An **Advanced Vignette** system is implemented with midpoint, roundness, and feathering controls, allowing for precise artistic framing.
7. **HSL Panel:** An **8-band HSL system** (Red, Orange, Yellow, Green, Aqua, Blue, Purple, Magenta) is implemented in the fragment shader. It uses weighted influence curves to allow targeted Hue, Saturation, and Luminance adjustments without causing artifacts.
8. **Color Grading:** A professional **3-Way Color Grading** system is implemented, allowing independent tinting of **Shadows, Midtones, and Highlights**. It features global **Balance** and **Blending** controls to precisely manage tonal transitions.
9. **Dithering:** High-quality dithering is implemented using a sine-based pseudo-random noise generator. It is applied to the final RGB output at a precision of 1/255 to mask banding artifacts and ensure smooth gradients on 8-bit displays.
10. **Denoising Pipeline (Phase 12/24/26):** Photon employs a hybrid architecture for professional-grade noise reduction:
    - **GPU Preview (NLM):** A real-time **Non-Local Means (NLM)** filter runs in the fragment shader. It uses 3x3 patch comparisons within a 7x7 search window, providing high-fidelity spatial denoising at 60fps.
    - **Full Quality Toggle:** A user preference in settings allows forcing the high-fidelity 2-step denoiser even during the preview phase.
    - **YCbCr Decoupled Processing (Phase 24):** The denoiser converts RGB to **YCbCr** color space. BM3D operates on the **Y (luminance)** channel only for ~3× speed improvement, while chrominance channels (Cb/Cr) are denoised via a **Multi-Scale Guided Filter** using the clean Y as a structural guide. This eliminates "color blotchiness" that joint-channel BM3D often misses.
    - **Multi-Scale Guided Filter:** An edge-preserving smoothing operator applied at three scales (r/2, r, r×2 with ε=0.5/2.0/6.0 scaled by chroma strength) to progressively remove fine-to-coarse chrominance noise while preserving luminance edges. Uses SIMD-optimized O(1) separable box filter on CPU or GPU-accelerated Vulkan compute path.
    - **Vulkan-Native Compute Offload:** Two compute pipelines run on dedicated Vulkan compute queues:
      - **`GpuSearcher`:** Offloads BM3D patch-matching (SSD using 3x3 patches) to GPU.
      - **`GpuChromaFilter` (Phase 26):** Runs the full multi-scale guided filter on GPU using two compute shaders (`box_filter.comp` for separable box blur, `guided_ops.comp` for element-wise coefficient computation). Falls back to CPU SIMD path automatically if Vulkan is unavailable.
    - **CPU Transform & Filter (SIMD):** All BM3D collaborative filtering, color space conversions, box filters, and guided filter coefficient computation use **AVX2 and FMA** instructions.
    - **Sharpness Enhancement (Phase 26):** The fragment shader uses a dual-radius 13-tap blur kernel (inner ring at 1.5 texels + outer ring at 3.0–4.0 texels) for effective unsharp masking even on heavily denoised images.
    - **Adaptive Proxy Scaling:** Preview denoising resolution dynamically adjusts based on the viewport size and zoom level (`viewport * zoom * 1.5`), ensuring zero pixelation even at 400% zoom.
    - **Asynchronous UX:** Background tasks are managed by a `QFutureWatcher`. Adjustment sliders remain interactive, and tasks are automatically aborted/restarted upon photo switching or parameter refinement.
    - **ROI-Driven Refinement (Phase 13):** When zoomed in, the engine prioritizes high-quality re-rendering of the visible Region of Interest (ROI) before triggering the background denoiser on that specific area, ensuring maximum sharpness and speed.
    - **Explicit Activation:** Denoising must be explicitly enabled via a checkbox. Disabling it immediately halts any background processing.
    - **Lifecycle Safety:** To prevent segmentation faults during application teardown, Photon implements a strict ownership and cleanup hierarchy:
      - **Thread Joining:** All background workers (LibRaw, BM3D Denoiser, Thumbnail generator) use dedicated thread pools that are explicitly joined in their respective destructors.
      - **Deterministic Teardown:** Singletons (`LogManager`, `AppStateManager`) are parented to the `QGuiApplication` instance and reset their internal static pointers to `nullptr` upon destruction to prevent dangling pointer access during final process cleanup.
      - **GPU Resource Release:** `RawViewport` implements the `releaseResources()` protocol to ensure all RHI-allocated textures and buffers are freed on the render thread while the graphics context is still valid.
      - **Instance Scoping:** The `QVulkanInstance` is managed as a local variable in `main()` to ensure it persists until all QML-related teardown is complete but is destroyed before the application exits.

**Accordion Sections:**

1. **Light:**

- _Sliders:_ Exposure, Contrast (UI: -100 to 100, mapped to 0.5 - 1.5 multiplier).
- _Tone:_ Highlights, Shadows, Whites, Blacks.
- _Divider Line_
- _Presence:_ Vibrance, Saturation.

1. **Color (HSL):**

- 8-band selector (Red, Orange, Yellow, Green, Aqua, Blue, Purple, Magenta).
- Targeted Hue, Saturation, and Luminance sliders.

1. **Color Grading:**

- 3-way region selector (Shadows, Midtones, Highlights).
- Cinematic tinting (Hue, Saturation, Luminance) per region.
- Global Balance and Blending sliders.

1. **Tone Curve:**

- Editable Parametric Curve (Highlights, Lights, Darks, Shadows) + Point Curve UI.

1. **Effects:**

- Clarity: Local contrast enhancement targeting midtones.
- Dehaze: Atmospheric haze removal using dark channel estimation.
- Structure: Micro-contrast adjustment for texture enhancement.
- Centrè: Radial tonal and color boost for subject emphasis.

1. **Detail:**

- Sharpening: Edge contrast enhancement (0 to 100).
- Noise Reduction: Luminance (NLM/BM3D) and Color reduction.

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

---

## DaVinci Tonemapping

```c
DEFINE_UI_PARAMS(max_input_nits, Max Input Nits, DCTLUI_SLIDER_FLOAT, 100.0, 48.0, 10000.0, 1.0)
DEFINE_UI_PARAMS(max_output_nits, Max Output Nits, DCTLUI_SLIDER_FLOAT, 100.0, 48.0, 10000.0, 1.0)
DEFINE_UI_PARAMS(adaptation, Adaptation, DCTLUI_SLIDER_FLOAT, 9.0, 9.0, 9.0, 1.0)
DEFINE_UI_PARAMS(user_b, User Input B, DCTLUI_SLIDER_FLOAT, 1.0, 0.0, 10.0, 0.0001)
DEFINE_UI_PARAMS(custom_b, Use Custom Adaptation, DCTLUI_CHECK_BOX, 0)
DEFINE_UI_PARAMS(invert, Invert, DCTLUI_CHECK_BOX, 0)
DEFINE_UI_PARAMS(clamp_toggle, Clamp, DCTLUI_CHECK_BOX, 1)

//clang-format on

__DEVICE__ float powf(float base, float exp) {
    return _copysignf(_powf(_fabs(base), exp), base);
}

__DEVICE__ float contrast(float x, float mid_gray, float gamma) {
    return mid_gray * powf(x / mid_gray, gamma);
}

// g(x) = a * (x / (x+b)) + c
__DEVICE__ float rolloff_function(float x, float a, float b, int invert) {
    if (invert) {
        return b * x / (a - x);
    } else {
        return a * (x / (x + b));
    }
}

__DEVICE__ float3 transform(int p_Width, int p_Height, int p_X, int p_Y, float p_R, float p_G, float p_B) {
    float input_white = max_input_nits / 100.0f;
    float output_white = max_output_nits / 100.0f;

    float3 out = make_float3(p_R, p_G, p_B);
    float b;
    if (custom_b) {
        b = user_b;
    } else {
        // Value of `b` when adaptation is 9.0:
        b = (input_white - (adaptation / 100.0f) * (input_white / output_white)) / ((input_white / output_white) - 1);
    }

    // Resolve evidently clamps the input to the input white point.
    if (clamp_toggle) {
        out.x = _fminf(out.x, input_white);
        out.y = _fminf(out.y, input_white);
        out.z = _fminf(out.z, input_white);
    }

    // Constraint 1: f(W_in) = W_out
    float a = output_white / (input_white / (input_white + b));
    if (input_white != output_white) {
        out.x = rolloff_function(out.x, a, b, invert);
        out.y = rolloff_function(out.y, a, b, invert);
        out.z = rolloff_function(out.z, a, b, invert);
    }

    // Resolve clamps to the output white point.
    if (clamp_toggle) {
        out.x = _clampf(out.x, 0.0f, output_white);
        out.y = _clampf(out.y, 0.0f, output_white);
        out.z = _clampf(out.z, 0.0f, output_white);
    }
    return out;
}
```
