#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <vector>

namespace photon {

enum class DemosaicMethod {
  LibRaw,  // Current LibRaw dcraw implementation
  PPG,     // Patterned Pixel Grouping (fast, good quality)
  RCD,     // Ratio Corrected Demosaicing (high quality)
  AMaZE,   // Aliasing Minimization and Zipper Elimination (highest quality,
           // slow)
  VNG4     // Variable Number of Gradients (for special cases)
};

class DemosaicEngine : public QObject {
  Q_OBJECT

 public:
  explicit DemosaicEngine(QObject* parent = nullptr);
  ~DemosaicEngine();

  // Demosaic the Bayer pattern data
  // Input: raw Bayer data (1 channel), width, height, filters pattern
  // Output: RGB data (3 channels, interleaved)
  bool demosaic(const float* input, float* output, int width, int height,
                uint32_t filters, DemosaicMethod method = DemosaicMethod::RCD);

  // Get available methods
  static QStringList availableMethods();
  static QString methodName(DemosaicMethod method);
  static DemosaicMethod methodFromString(const QString& name);

 private:
  // LibRaw's built-in demosaicing (reference implementation)
  bool demosaicLibRaw(float* output, int width, int height);

  // PPG (Patterned Pixel Grouping) - based on darktable's implementation
  bool demosaicPPG(const float* input, float* output, int width, int height,
                   uint32_t filters);

  // RCD (Ratio Corrected Demosaicing) - based on darktable's implementation
  bool demosaicRCD(const float* input, float* output, int width, int height,
                   uint32_t filters);

  // Helper functions
  inline int fc(int row, int col, uint32_t filters) const {
    return (filters >> (((row << 1) & 6) | (col & 1))) & 3;
  }

  void interpolateGreenPPG(float* output, const float* input, int width,
                           int height, uint32_t filters);

  void interpolateRedBluePPG(float* output, int width, int height,
                             uint32_t filters);

  void borderInterpolate(float* output, const float* input, int width,
                         int height, uint32_t filters, int border_size);

  // RCD-specific helpers
  void rcdBorderInterpolate(float* output, const float* input, int width,
                            int height, uint32_t filters, int margin);

  void rcdInterpolateGreen(float* rgb[3], const float* cfa, float* vh_dir,
                           const float* lpf, int tile_rows, int tile_cols,
                           int tile_size, uint32_t filters);

  void rcdInterpolateRedBlue(float* rgb[3], const float* cfa, float* pq_dir,
                             const float* vh_dir, int tile_rows, int tile_cols,
                             int tile_size, uint32_t filters);

  void rcdCalculateVHDir(float* vh_dir, const float* cfa, int tile_rows,
                         int tile_cols, int tile_size);

  void rcdCalculatePQDir(float* pq_dir, const float* cfa, int tile_rows,
                         int tile_cols, int tile_size, uint32_t filters);

  void rcdCalculateLPF(float* lpf, const float* cfa, int tile_rows,
                       int tile_cols, int tile_size, uint32_t filters);
};

}  // namespace photon
