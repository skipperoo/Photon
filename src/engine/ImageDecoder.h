#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <cstdint>
#include <vector>

namespace photon {

struct BitmapImage {
  // Interleaved RGB, 3 channels, 16-bit, sRGB gamma encoded, upright.
  std::vector<uint16_t> pixels;
  int width = 0;
  int height = 0;
  // Orientation tag as found in the file (1-8), pixels are already upright.
  int orientation = 1;
  QVariantMap metadata;
};

class ImageDecoder {
 public:
  static const QStringList& rawExtensions();
  static const QStringList& bitmapExtensions();

  static bool isRaw(const QString& path);
  static bool isBitmap(const QString& path);

  // Full resolution decode. Returns an empty image on failure.
  static BitmapImage decode(const QString& path);

  // Faster decode for thumbnails/previews. JPEG uses libjpeg downscaling,
  // TIFF decodes full resolution and downscales with Qt.
  static BitmapImage decodeScaled(const QString& path, int maxEdge);

  // Decode, downscale to maxEdge and rotate upright.
  static QImage extractThumbnail(const QString& path, int maxEdge = 360);

  // Read EXIF/TIFF metadata only (does not decode pixels).
  static QVariantMap readMetadata(const QString& path);
};

}  // namespace photon
