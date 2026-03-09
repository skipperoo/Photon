# Photon

---

[![Build and Release](https://github.com/skipperoo/Photon/actions/workflows/build.yml/badge.svg)](https://github.com/skipperoo/Photon/actions/workflows/build.yml)

## What is this and who is this for?

Photon is an open source RAW image editor, born to (_try to_) replace basic Adobe Lightroom® functionalities, focused on ease of use and performance.
I'm an occasional photographer and lately I've been using the Lightroom mobile version to edit my photos as it is free and has all the features I want, except for panorama stitching, so I thought it was a good idea to attempt to create something to fit my needs and be finally free from Adobe.

If you are looking for a modern quick photo editor, this might be for you. On the other hand, if you want advanced AI features, local adjustment and so on, either pay for Lightroom or try [RapidRaw](https://github.com/CyberTimon/RapidRAW), which looks very promising.

## Current state

Not having much experience with both Qt6 and how images are processed, I used both Gemini and Copilot to kickstart the project, especially to implement what could have taken months and months of full time work, which I cannot afford right now.

The project is in an advanced state and most of the functionalities listed below as completed work good enough for me, so I decided to step back from automatic programming/vibe coding/whatever and start to implement and refine what's missing manually, to both asses the code quality produced up until now (I would be a liar if I say that I diligently reviewed all the AI output...) and to actually keep my skills sharp in these funny times.

Here's what works and what is still in the backlog:

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
| Crop & Transform Tools                      |   ✅   |
| Tone Curve (Spline UI)                      |   ✅   |
| Batch Copy & Paste                          |   ✅   |
| Lens Correction (Lensfun)                   |   🔁   |
| Panorama Stitching                          |   🔁   |
| HDR merge                                   |   🔁   |
| Import/Export presetes                      |   🔁   |

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
Searching for alternatives on GitHub I found RapidRaw, a very promising editor with a stunning UI and some very powerful capabilities. I give it a shot and I really liked it, especially the UX that allowed me to quickly edit my last shooting session. However, while the editing workflow is exceptional, I found the performance disappointing, even on a laptop with a dedicated GPU: the preview takes a lot of time to render, the adjustment are applied slowly and the overall experience is laggy.
For these reasons I decided to start this journey, choosing to use QT6, which I think it's a better tool for implementing an high performance photo editor.
