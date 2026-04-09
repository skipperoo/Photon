# Photon

---

[![Build and Release](https://github.com/skipperoo/Photon/actions/workflows/build.yml/badge.svg)](https://github.com/skipperoo/Photon/actions/workflows/build.yml)

## What is this and who is this for?

Photon is an open source RAW image editor, focused on ease of use and performance.
I'm an occasional photographer and lately I've been using the mobile version of Lightroom to edit my photos as it is free and has all the features I need, but unfortunately it does not run on Linux.

If you are looking for a modern quick photo editor, this might be for you. On the other hand, if you want advanced AI features, local adjustments and so on, either pay for Lightroom or try [RapidRaw](https://github.com/CyberTimon/RapidRAW), which looks very promising.

## Current state

Not having much experience with both Qt6 and how RAW images work, I used both Gemini and Copilot to kickstart the project, shrinking down months of full time research and work.

> [!NOTE]
> From the first day of development I wanted to implement things as fast as possible to get a working application and start editing my photos on Linux, for this reason I skipped chores and code hygiene practices, but now I will slow down to clean up the project and fix all the little things and inconsistencies that annoy me.

Features:

- [x] RAW Decoding (LibRaw)
- [x] GPU-Accelerated Rendering (Vulkan/RHI)
- [x] Non-Destructive Editing (JSON Sidecars)
- [x] Exposure & Contrast
- [x] Vibrance & Saturation
- [x] 8-Band HSL Adjustments
- [x] Color Grading (Shadows/Midtones/Highlights)
- [x] Film Grain & Vignette
- [x] Live Histogram (RGB/Luma)
- [x] Undo/Redo History
- [x] Preset System
- [x] EXIF Metadata & Orientation
- [x] Hybrid Denoising (BM3D + GPU NLM)
- [x] Image Export (JPEG/TIFF)
- [x] Theme Customization (Light/Dark/Accents)
- [x] Crop & Transform Tools
- [x] Tone Curve (Spline UI)
- [x] Batch Copy & Paste
- [x] Panorama Stitching
- [ ] Lens Correction (Lensfun)
- [ ] HDR merge
- [ ] Import/Export presetes

## Getting Started

I will soon provide binaries for both Windows and Linux, but for now, please clone and build the program manually.

### Prerequisites

- CMake 3.16+
- Qt 6.8+ (with ShaderTools and RHI)
- LibRaw
- A Vulkan-capable GPU

### Build

To build Photon:

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# To run it
./Photon
```

## Credits

- [RapidRaw](https://github.com/CyberTimon/RapidRAW) - Inspired the UI and the workflow of the application
- [Darktable](https://github.com/darktable-org/darktable) - Helped building and polishing some of the logic.

## Why another editing tool?

I've always used Lightroom to edit my photos and I never found a valid alternative: tools like Rawtherapee and Darktable are for sure very capable and powerful, but I find them unnecessary complex to perform simple edits.
Searching for alternatives on GitHub I found RapidRaw, a very promising editor with a stunning UI and some very powerful capabilities. I gave it a shot and I really liked it, especially the UX that allowed me to quickly edit my last shooting session. However, while the editing workflow is exceptional, I found the performance disappointing, even on a laptop with a dedicated GPU: the preview takes a lot of time to render, the adjustment are applied slowly and the overall experience is laggy.
For these reasons I decided to start this journey, choosing to use QT6 and C++, which I think it's a better tool for implementing an high performance photo editor.
