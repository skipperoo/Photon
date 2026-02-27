# Photon

---

[![Build and Release](https://github.com/skipperoo/Photon/actions/workflows/build.yml/badge.svg)](https://github.com/skipperoo/Photon/actions/workflows/build.yml)

Photon is an open source alternative to Adobe Lightroom®, focused on ease of use and performance.
It features a minimal interface with all the controls you would expect from a Raw photo editor:

- Light adjustments
- Tone curve
- HSL controls
- Highlights, midtones and shadows color grading
- Vignette
- Clarity, dehaze and texture controls
- Sharepining and denoising

Here's what works and what does not

| Feature                                     | Status |
| :------------------------------------------ | :----: |
| RAW Decoding (LibRaw)                       |   ✅   |
| GPU-Accelerated Rendering (Vulkan/RHI)      |   ✅   |
| Non-Destructive Editing (JSON Sidecars)     |   ✅   |
| Exposure & Contrast                         |   ✅   |
| Vibrance & Saturation                       |   ✅   |
| 8-Band HSL Adjustments                      |   ✅   |
| Color Grading (Shadows/Midtones/Highlights) |   ✅   |
| Film Grain & Vignette                       |   ✅   |
| Live Histogram (RGB/Luma)                   |   ✅   |
| Undo/Redo History                           |   ✅   |
| Preset System                               |   ✅   |
| EXIF Metadata & Orientation                 |   ✅   |
| Hybrid Denoising (BM3D + GPU NLM)           |   ✅   |
| Interactive Viewport (Pan & Zoom)           |   ✅   |
| Image Export (JPEG/TIFF)                    |   ✅   |
| Theme Customization (Light/Dark/Accents)    |   ✅   |
| Crop & Transform Tools                      |   🔁   |
| Lens Correction (Lensfun)                   |   🔁   |
| Tone Curve (Spline UI)                      |   🔁   |
| Batch Processing                            |   🔁   |

## Getting Started

### Prerequisites

- CMake 3.16+
- Qt 6.8+ (with ShaderTools and RHI)
- LibRaw
- A Vulkan-capable GPU

### Build (High Performance)

To build Photon with native optimizations and Link Time Optimization (LTO):

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-march=native -flto=auto -O3" ..
make -j$(nproc)
```

### Run

```bash
./build/Photon
```

## Why another editing tool?

I've always used Lightroom to edit my photos and I never found a valid alternative: tools like Rawtherapee and Darktable are for sure very capable and powerful tool, but I find them unnecessary complex to perform simple edits.
Searching for alternatives on GitHub I found [RapidRaw](https://github.com/CyberTimon/RapidRAW), a very promising editor with a stunning UI and some very powerful capabilities. I give it a shot and I really liked it, especially the UX that allowed me to quickly edit my last shooting session. However, while the editing workflow is exceptional, I found the performance disappointing, even on a laptop with a dedicated GPU: the preview takes a lot of time to render, the adjustment are applied slowly and the overall experience is laggy.
For these reasons I decided to start this journey, choosing to use QT6, which I think it's a better tool for implementing an high performance photo editor.
