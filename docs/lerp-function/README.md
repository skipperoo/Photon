# Local `lerp` vs `std::lerp` — Release Build Fix Log

## Problem

Building Photon in `Release` mode failed with 34 compiler errors, all originating from
`src/engine/RawEngine.cpp:38` and its call sites:

```
/home/leonardo/Documents/rblabs/Photon/src/engine/RawEngine.cpp:38:44: error:
'float lerp(float, float, float)' conflicts with a previous declaration
   38 | static float lerp(float a, float b, float t) { return a + t * (b - a); }
      |                                            ^
In file included from /usr/include/c++/16/math.h:36,
                 from /usr/include/libraw/libraw.h:40,
                 from /home/leonardo/Documents/rblabs/Photon/src/engine/RawEngine.h:3,
                 from /home/leonardo/Documents/rblabs/Photon/src/engine/RawEngine.cpp:1:
/usr/include/c++/16/cmath:3857:3: note: previous declaration
'constexpr float std::lerp(float, float, float)'
```

The errors were reported twice (for the `Photon` target and for `tst_RawEngine`,
which compiles the same translation unit) but all had a single root cause.

## Root cause

The build environment is Fedora 44 with GCC 16.2.1. In GCC 16, libstdc++'s
`<math.h>` now contains a global `using std::lerp;` declaration
(`/usr/include/c++/16/math.h:183`). `<math.h>` reaches `RawEngine.cpp` transitively
through `<libraw/libraw.h>`.

That `using` declaration introduces all `std::lerp` overloads (`float`, `double`,
`long double`) into the global namespace. Consequently:

- The file-local `static float lerp(float, float, float)` at
  `src/engine/RawEngine.cpp:38` is a redeclaration of the imported
  `std::lerp(float, float, float)` with different linkage, hence
  `conflicts with a previous declaration`.
- Every unqualified `lerp(...)` call becomes ambiguous between the local function
  and the `std::` overload family (the compiler reports 4 candidates per call).

Older toolchains (GCC 15 and earlier, used by the existing `build/` directory)
did not have this `using std::lerp;` in `<math.h>`, so the code had compiled
unchanged for years. The problem is purely a toolchain upgrade regression, not a
logic bug.

## Decision log

| Option | Assessment | Decision |
| --- | --- | --- |
| Rename the local helper to `lerpF` | Zero behavior change, smallest possible diff, keeps the helper's exact arithmetic. | **Chosen** |
| Delete the helper and call `std::lerp` everywhere | Works, but changes edge-case semantics (see below) and touches every call site with a different function. | Rejected |
| Move the helper into an anonymous namespace | Still collides with the imported overloads in the same overload set, so calls remain ambiguous. | Rejected |
| Qualify calls as `::lerp` | Does not help: the declaration conflict at line 38 remains, because the global name already refers to `std::lerp`. | Rejected |
| Suppress the warning/error via compiler flags | The conflict is ill-formed C++, not a warning; no portable flag exists. | Rejected |

A repository-wide search confirmed `RawEngine.cpp` was the only file defining a
global-scope `lerp`. The local helper was renamed to `lerpF` at its definition and
at all 25 call sites.

## Difference between the local `lerp` and `std::lerp`

Local helper (before the rename):

```cpp
static float lerp(float a, float b, float t) { return a + t * (b - a); }
```

`std::lerp` (C++20, `<cmath>`):

- Is `constexpr`.
- Uses the same `a + t * (b - a)` formula for ordinary values, but adds
  well-defined behavior for special cases:
  - `t == 0` returns `a`; `t == 1` returns `b`; `a == b` returns `a`.
  - NaN operands or `t` produce NaN.
  - Infinities are handled explicitly (for example `lerp(-inf, inf, t)` follows
    IEC 60559 rules instead of producing `NaN` from `inf - inf`).
- Guarantees monotonicity of the result with respect to `t`.

For the inputs used in `RawEngine.cpp` (finite pixel, mask and blend values,
essentially always with `t` in `[0, 1]`) the two functions produce identical
results; the differences only manifest for NaN/infinite inputs and in the formal
monotonicity guarantee. Because the release pipeline must not silently change
image math, renaming was preferred over switching to `std::lerp`.

## Changes applied

- `src/engine/RawEngine.cpp`: `lerp` renamed to `lerpF` (definition plus 25 call
  sites). No numeric behavior change.
- `src/engine/Panorama.h`: the aggregate `<opencv2/opencv.hpp>` include was
  replaced by the specific `<opencv2/core.hpp>` and `<opencv2/imgproc.hpp>`
  headers. This is required by the Docker release build, which links a minimal
  static OpenCV build that only ships the `core`, `imgproc`, `flann`,
  `features2d`, `calib3d` and `stitching` modules. Verified with
  `g++ -fsyntax-only -std=c++20` against the full system OpenCV.

## Outcome

- All 34 `lerp` compile errors are resolved; `RawEngine.cpp` compiles cleanly
  with GCC 16.
- The host `build-release` build then failed for an unrelated reason: PGDG's
  `libpq5` package on the host lacks the `RHPG_*` symbol versions that Fedora's
  `libgdal` requires, and OpenCV's `imgcodecs` transitively pulls in `libgdal`.
  This is an environment defect, not a source defect.
- To make release builds independent of host library updates, the Linux release
  build is now produced in a pinned Ubuntu 22.04 container
  (`Dockerfile.release`, helper `build_release.sh`) with statically linked
  LibRaw and OpenCV, and a pinned bundled Qt 6.10.1. The resulting
  `dist/linux/Photon-Linux-x86_64.AppImage` has no `NEEDED` entries for
  `libraw`, `libopencv_*`, `libgdal` or `libpq`, and starts natively on Wayland
  and X11.
- Verification on the host toolchain: `tst_RawEngine` passes 11/11 tests and
  `tst_AppStateManager` passes 11/11 tests with the renamed helper.
