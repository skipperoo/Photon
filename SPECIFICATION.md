# 🎨 Photon UI/UX Specification

**Vision:** A high-performance, native C++/Qt implementation of a modern RAW editor.
**Visual Reference:** The UI should closely mirror the clean, dark aesthetic of [RapidRAW](https://github.com/CyberTimon/RapidRAW).

**Theme:** "Zinc Dark" (Shadcn). Flat, borderless inputs, subtle gradients on hover, rigorous spacing.

---

## 1. 📂 Data & Project Structure

To ensure non-destructive editing and high performance, Photon manages a sidecar directory automatically.

**Logic:**

- When a folder is opened, Photon checks for/creates a `.PhotonData` folder (hidden on Linux/Mac, standard folder on Win).
- **Edits:** Stored as JSON arrays to support full Undo/Redo history.
- **Cache:** Stores generated thumbnails to avoid re-parsing RAW files on every launch.

**Directory Tree:**

```text
/User/Pictures/Vacation2024/
├── IMG_001.ARW
├── IMG_002.ARW
└── PhotonData/
    ├── edits/
    │   ├── IMG_001.json  # Contains [ {op: "exposure", val: 0.5}, ... ]
    │   └── IMG_002.json
    └── cache/
        ├── thumbnails/
        │   ├── IMG_001.jpg # 360px Preview for Library/Filmstrip
        │   └── IMG_002.jpg
        └── previews/       # Optional: Full 1080p previews for fast loading

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
- _Action:_ Opens a modal overlay with Application Preferences (GPU selection, Cache size).

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

**Goal:** Precision editing.
**Layout:** Three-Pane "Holy Grail" Layout.

### A. The Viewport (Left/Center - Dominant)

- **Content:** The `RawViewport` (C++ Vulkan Widget).
- **Behavior:**
  - Pan (Space + Drag) & Zoom (Scroll Wheel).
  - "Fit" vs "100%" toggle in a floating toolbar at the bottom of the viewport.

### B. The Tool Panel (Right - Collapsible)

- **Behavior:** Scrollable vertical stack of Accordions (Shadcn `Collapsible`).
- **Width:** Fixed (e.g., 320px). Can be toggled hidden (Shortcut: Tab).
- **Histogram:** (Top, pinned). RGB + Luma graphs.

### GPU Processing Pipeline (Phase 5)

To achieve professional-grade results, Photon employs a high-fidelity GPU pipeline:

1.  **Linear Workflow:** Input textures are converted from sRGB to **Linear Space** for all mathematical operations. This ensures correct light addition and blending.
2.  **White Balance:** Handled in linear space using a kelvin-based temperature shift and a magenta/green tint adjustment.
3.  **Tonemapping:** **AgX Sigmoid transform** is implemented to provide a filmic highlight roll-off and natural color compression, preventing "digital" clipping of bright highlights.
4.  **Grain:** High-quality **Film Grain** is implemented using a gradient noise algorithm, with controls for amount, size, and roughness. It is applied in linear-to-srgb space with a luma-based mask to protect shadows and highlights.
5.  **Vignette:** An **Advanced Vignette** system is implemented with midpoint, roundness, and feathering controls, allowing for precise artistic framing.
6.  **Dithering:** (Planned) Final output is dithered to prevent banding on 8-bit displays.

**Accordion Sections:**

1. **Light:**

- _Sliders:_ Exposure, Contrast.
- _Tone:_ Highlights, Shadows, Whites, Blacks.
- _Divider Line_
- _Presence:_ Vibrance, Saturation.

1. **Tone Curve:**

- Editable Parametric Curve (Highlights, Lights, Darks, Shadows) + Point Curve UI.

1. **Effects:**

- Clarity (Mid-tone contrast).
- Dehaze (Atmospheric removal).
- Structure (Local detail).
- Centrè (Vignette/Post-crop darkening).

1. **Detail:**

- Sharpening (Amount, Radius, Masking).
- Noise Reduction (Luminance, Color).

### C. The Filmstrip (Bottom)

- **Content:** Horizontal scrollable list of thumbnails from the current folder.
- **Sync:** Highlighted thumbnail matches the main Viewport image.
- **Navigation:** Left/Right Arrow keys move selection.

### D. Presets Panel (Left - Optional/Toggle)

- List of user-saved JSON states.
- Clicking applies all settings from the JSON to the current image.

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
