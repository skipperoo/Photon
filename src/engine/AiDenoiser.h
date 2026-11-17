#pragma once

#include <QJsonObject>
#include <QString>
#include <QtGlobal>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace photon::ai {

// ---------------------------------------------------------------------------
// AI denoise (RawNIND UtNet2) for Photon, two models:
//
// - Bayer: runs on the raw Bayer mosaic (black-subtracted, white-normalised,
//   not white-balanced, RGGB-aligned). Its camera-RGB output replaces
//   LibRaw's demosaic and is pushed back through LibRaw's own colour
//   conversion and output curve.
// - Linear: for every other RAW LibRaw can develop (X-Trans, Foveon, linear
//   DNGs, unusual layouts): runs on LibRaw's demosaicked develop source
//   converted to linear Rec.2020 (the working space) and returns it denoised.
//
// Either way the model runs ONCE per photo and the result ("product") is a
// 16-bit RGB image in exactly the space and orientation of the develop
// source (dcraw_make_mem_image() with configureDevelopDecode). The product is
// cached on disk; the Amount slider is a blend between the plain LibRaw image
// and the product, applied to the develop source (preview, histogram and
// export all see the same pixels).
// ---------------------------------------------------------------------------

// Bumped whenever the product changes for the same model and photo.
inline constexpr int kAlgorithmVersion = 1;
// Bumped whenever the colour pass (chroma BM3D + guided filter) changes.
// 2: runs at the RAW base exposure with BT.2020 Y'CbCr.
inline constexpr int kColorVersion = 2;

enum class Model { Bayer, Linear };
inline const char* modelName(Model m) {
  return m == Model::Bayer ? "Bayer" : "Linear";
}

// 16-bit RGB product, same layout/orientation as LibRaw's mem image.
struct Product {
  Model model = Model::Bayer;  // the model that made it
  int width = 0;
  int height = 0;
  std::vector<ushort> rgb;       // AI result, width * height * 3
  std::vector<ushort> rgbColor;  // AI result + full colour pass (may be empty)
  bool isValid() const {
    return width > 0 && height > 0 &&
           rgb.size() == size_t(width) * size_t(height) * 3;
  }
  bool hasColor() const { return isValid() && rgbColor.size() == rgb.size(); }
};

enum class Status { Ok, Unsupported, NoModel, Failed, Aborted };

using ProgressFn = std::function<void(float fraction)>;

// --- Tiled runners (model-agnostic, unit-testable) -------------------------

// One model call. Bayer: `in` is 4 planes (R, G1, G2, B) of tile x tile
// cells, `out` must receive 3 planes (camera R, G, B) of (2*tile) x (2*tile)
// pixels. Linear: `in` and `out` are 3 planes (linear Rec.2020 R, G, B) of
// tile x tile pixels.
using TileFn = std::function<bool(const float* in, std::vector<float>& out)>;

struct TileParams {
  int tile = 512;    // Bayer: cells (one cell = one 2x2 RGGB quad); Linear: pixels
  int overlap = 64;  // cells / pixels shared by neighbouring tiles (feathered)
  // The model's output carries its own scale (about 1e6 for RawNIND); each
  // tile is brought back to the input's scale by matching channel means,
  // clamped to nominal * (1 +/- maxDeviation).
  float gainNominal = 1.0e6f;
  float gainMaxDeviation = 0.15f;
};

// Tile start offsets covering n cells (evenly spread, first 0, last n-tile).
std::vector<int> tileStarts(int n, int tile, int overlap);
// Blend weight of cell p inside a tile: smoothstep ramp over `fade` cells at
// edges that have a neighbour, 1 at picture edges, never exactly 0.
float tileWeight(int p, int tile, int fade, bool hasBefore, bool hasAfter);

// Denoise an RGGB-aligned, normalised mosaic (width, height even). Output:
// camera RGB, width * height * 3 interleaved floats on the input's scale.
Status denoiseMosaic(const float* mosaic, int width, int height,
                     const TileFn& model, const TileParams& params,
                     std::vector<float>& camRgb, std::atomic<bool>* abort,
                     const ProgressFn& progress, QString* error);

// Denoise linear Rec.2020 RGB (width * height * 3 interleaved floats, white
// at 1, negatives allowed) with the linear model. Output: the same layout on
// the input's scale (may be slightly negative in the deepest shadows).
Status denoiseLinear(const float* lin, int width, int height,
                     const TileFn& model, const TileParams& params,
                     std::vector<float>& linRgb, std::atomic<bool>* abort,
                     const ProgressFn& progress, QString* error);

// --- Blending / resampling helpers -----------------------------------------

// Box-filter resample of interleaved 16-bit RGB (area average).
void resampleRgb16(const ushort* src, int sw, int sh, ushort* dst, int dw,
                   int dh);
// dst = plain + amount * (product - plain), amount in [0, 1].
void blendRgb16(const ushort* plain, const ushort* product, ushort* dst,
                size_t pixelCount, float amount);
// Two-slider blend in a luma/colour-difference space (Y, B-Y, R-Y):
//   Y = lerp(Yplain, Yai, luma)
//   C = lerp(lerp(Cplain, Cai, luma), Ccolor, color)
// `aiColor` (AI + full colour pass) may be null: then colour follows luma.
void blendDenoised(const ushort* plain, const ushort* ai, const ushort* aiColor,
                   ushort* dst, size_t pixelCount, float luma, float color);

// --- Disk cache --------------------------------------------------------------

// Cache file for a photo and model fingerprint (in <dir>/.PhotonData/denoise).
QString cachePath(const QString& photoPath, const QString& modelFingerprint);
bool saveProduct(const QString& file, const Product& product);
bool loadProduct(const QString& file, Product& product);
// Remove cached products of this photo except `keep` (and `keep2`).
void removeStaleProducts(const QString& photoPath, const QString& keep,
                         const QString& keep2 = QString());

// --- Engine (model + device) -------------------------------------------------

// True when this build includes AI denoise (ncnn).
bool isCompiledIn();
// True when any model's files exist in a search folder (no loading; cheap).
bool modelFilesPresent();
// True when this model's files exist in a search folder (cheap).
bool modelFilesPresent(Model model);
// True when at least one model loaded (loads them and picks the device on
// first call: slow, call it from a worker thread).
bool isModelAvailable();
// Device running this model ("GPU: NVIDIA ...", "CPU"; "" if not loaded).
QString deviceDescription(Model model);
// Stable fingerprint of this model's files + algorithm and decode versions
// ("" if not loaded).
QString modelFingerprint(Model model);
// The model for this RAW: Bayer for a standard Bayer sensor when the Bayer
// model is present, otherwise Linear. False for files LibRaw cannot open or
// when the needed model is missing.
bool modelForRaw(const QString& path, Model& model);
// True when this RAW can be processed (modelForRaw succeeds).
bool isSupportedRaw(const QString& path);

// Load the cached product, or compute (and cache) it. Blocking; run it on a
// worker thread. `computeIfMissing=false` only reads the cache.
Status obtainProduct(const QString& path, Product& out, bool computeIfMissing,
                     std::atomic<bool>* abort, const ProgressFn& progress,
                     QString* message);

// Release GPU resources (call once at application exit).
void shutdown();

// Device for AI denoise: "Auto" (fastest correct device), "CPU", or a GPU by
// its Vulkan device name. Applied lazily (next tile). `userChanged` clears a
// leftover GPU crash marker so an explicit choice is retried.
void setDevicePreference(const QString& preference, bool userChanged);

// Apply AI denoise to a develop source buffer (LibRaw mem image of `path`,
// any size): injects the scene white of the plain source, blends the product
// by the photo's Amount and marks the settings so ImageDeveloper skips the
// Classic denoise. Falls back to Classic (unchanged settings) when the photo
// is not supported or no product can be obtained. Returns true if applied.
bool applyToDevelopSource(const QString& path, ushort* rgb, int width,
                          int height, QJsonObject& settings,
                          bool computeIfMissing);

}  // namespace photon::ai
