#include "CameraLook.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <memory>

#include <libraw/libraw.h>

#include "../managers/LogManager.h"

namespace photon {
namespace cameralook {
namespace {

constexpr int kProxyEdge = 192;

float luma709(float r, float g, float b) {
  return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// The embedded camera JPEG, rotated the way the engine orients its
// thumbnails (the same flip mapping as RawEngine::extractThumbnail), so it
// pairs pixel for pixel with the processed raw.
QImage extractCameraJpeg(const QString& path) {
  LibRaw processor;
  if (processor.open_file(path.toLocal8Bit().data()) != LIBRAW_SUCCESS)
    return QImage();
  if (processor.unpack_thumb() != LIBRAW_SUCCESS) return QImage();

  int flip = processor.imgdata.sizes.flip;
  int orient = 1;
  if (flip == 3)
    orient = 3;
  else if (flip == 5)
    orient = 8;
  else if (flip == 6)
    orient = 6;

  int ret = 0;
  libraw_processed_image_t* thumb = processor.dcraw_make_mem_thumb(&ret);
  if (!thumb) return QImage();
  QImage img;
  if (thumb->type == LIBRAW_IMAGE_JPEG) {
    img.loadFromData(thumb->data, thumb->data_size, "JPEG");
  } else if (thumb->type == LIBRAW_IMAGE_BITMAP) {
    img = QImage(thumb->data, thumb->width, thumb->height,
                 QImage::Format_RGB888)
              .copy();
  }
  LibRaw::dcraw_clear_mem(thumb);

  if (orient <= 1 || img.isNull()) return img;
  QTransform trans;
  if (orient == 3)
    trans.rotate(180);
  else if (orient == 6)
    trans.rotate(90);
  else if (orient == 8)
    trans.rotate(270);
  return img.transformed(trans);
}

}  // namespace

QString cameraLookCachePath(const QString& sourcePath) {
  const QFileInfo info(sourcePath);
  return QDir::toNativeSeparators(
      info.absolutePath() + "/.PhotonData/cache/looks/" + info.fileName() +
      ".json");
}

QJsonObject toneCurveToJson(const CameraToneCurve& curve) {
  QJsonObject obj;
  if (!curve.valid) return obj;
  QJsonArray knots;
  for (int i = 0; i < kToneKnots; ++i) {
    QJsonArray knot;
    knot.append(double(curve.knots[i][0]));
    knot.append(double(curve.knots[i][1]));
    knots.append(knot);
  }
  obj["knots"] = knots;
  return obj;
}

CameraToneCurve toneCurveFromJson(const QJsonObject& obj) {
  CameraToneCurve curve;
  const QJsonArray knots = obj.value("knots").toArray();
  if (knots.size() != kToneKnots) return curve;
  for (int i = 0; i < kToneKnots; ++i) {
    const QJsonArray knot = knots[i].toArray();
    if (knot.size() != 2) return CameraToneCurve();
    curve.knots[i][0] = float(knot[0].toDouble());
    curve.knots[i][1] = float(knot[1].toDouble());
  }
  if (!curve.validate()) return CameraToneCurve();
  return curve;
}

CameraLook loadCameraLook(const QString& sourcePath) {
  CameraLook look;
  QFile file(cameraLookCachePath(sourcePath));
  if (!file.open(QIODevice::ReadOnly)) return look;
  const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
  if (obj.value("version").toInt() != kCameraLookVersion) return look;
  look.tone = toneCurveFromJson(obj);
  if (!look.tone.valid) return CameraLook();
  return look;
}

bool saveCameraLook(const QString& sourcePath, const CameraLook& look) {
  if (!look.tone.valid) return false;
  const QString path = cameraLookCachePath(sourcePath);
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
  QJsonObject obj = toneCurveToJson(look.tone);
  obj["version"] = kCameraLookVersion;
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  return file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact)) > 0;
}

CameraLook fitCameraLook(const QString& sourcePath) {
  CameraLook look;

  // Decode the sensor proxy the way the render does: the same LibRaw
  // parameters as the engine (camera white balance, auto-bright threshold),
  // at half resolution for speed. Both sides end up in scene-linear sRGB.
  LibRaw processor;
  processor.imgdata.params.output_bps = 16;
  processor.imgdata.params.use_camera_wb = 1;
  processor.imgdata.params.no_auto_bright = 0;
  processor.imgdata.params.auto_bright_thr = 0.01f;
  processor.imgdata.params.half_size = 1;
  if (processor.open_file(sourcePath.toLocal8Bit().data()) != LIBRAW_SUCCESS)
    return look;
  if (processor.unpack() != LIBRAW_SUCCESS) return look;
  if (processor.dcraw_process() != LIBRAW_SUCCESS) return look;
  int ret = 0;
  libraw_processed_image_t* raw = processor.dcraw_make_mem_image(&ret);
  std::unique_ptr<libraw_processed_image_t, void (*)(libraw_processed_image_t*)>
      guard(raw, [](libraw_processed_image_t* image) {
        if (image) LibRaw::dcraw_clear_mem(image);
      });
  if (!raw || raw->type != LIBRAW_IMAGE_BITMAP || raw->width < 16 ||
      raw->height < 16)
    return look;

  // The camera JPEG supplies the correspondences only, never output pixels.
  const QImage reference = extractCameraJpeg(sourcePath);
  if (reference.isNull()) return look;

  const int sensorWidth = int(raw->width);
  const int sensorHeight = int(raw->height);
  const double sensorAspect = double(sensorWidth) / double(sensorHeight);
  const double referenceAspect =
      double(reference.width()) / double(reference.height());
  if (std::abs(referenceAspect / sensorAspect - 1.0) > 0.02) {
    LogManager::instance()->log(
        QString("[ CameraLook.cpp ] - %1: embedded JPEG aspect does not "
                "match the sensor, skipping the look")
            .arg(QFileInfo(sourcePath).fileName()),
        PHOTON_DEBUG);
    return look;
  }

  // Fixed, bounded proxies so the look cannot depend on the decode size.
  int proxyWidth = kProxyEdge;
  int proxyHeight = kProxyEdge;
  if (sensorWidth >= sensorHeight)
    proxyHeight = std::max(1, int(std::lround(kProxyEdge / sensorAspect)));
  else
    proxyWidth = std::max(1, int(std::lround(kProxyEdge * sensorAspect)));
  const QImage referenceProxy =
      reference.scaled(proxyWidth, proxyHeight, Qt::IgnoreAspectRatio,
                       Qt::SmoothTransformation)
          .convertToFormat(QImage::Format_RGB888);
  if (referenceProxy.isNull()) return look;

  const ushort* data = reinterpret_cast<const ushort*>(raw->data);
  std::vector<std::pair<float, float>> pairs;
  pairs.reserve(std::size_t(proxyWidth) * std::size_t(proxyHeight));
  for (int py = 0; py < proxyHeight; ++py) {
    const int y0 = py * sensorHeight / proxyHeight;
    const int y1 = std::max(y0 + 1, (py + 1) * sensorHeight / proxyHeight);
    const uchar* refLine = referenceProxy.constScanLine(py);
    for (int px = 0; px < proxyWidth; ++px) {
      const int x0 = px * sensorWidth / proxyWidth;
      const int x1 = std::max(x0 + 1, (px + 1) * sensorWidth / proxyWidth);
      double sumR = 0.0;
      double sumG = 0.0;
      double sumB = 0.0;
      int count = 0;
      for (int y = y0; y < y1; ++y) {
        const ushort* row = data + std::size_t(y) * sensorWidth * 3;
        for (int x = x0; x < x1; ++x) {
          sumR += row[x * 3 + 0];
          sumG += row[x * 3 + 1];
          sumB += row[x * 3 + 2];
          ++count;
        }
      }
      if (count == 0) continue;
      const float sceneR = srgbToLinearF(float(sumR / count / 65535.0));
      const float sceneG = srgbToLinearF(float(sumG / count / 65535.0));
      const float sceneB = srgbToLinearF(float(sumB / count / 65535.0));
      if (!(sceneR > 0.001f && sceneR < 1.5f) ||
          !(sceneG > 0.001f && sceneG < 1.5f) ||
          !(sceneB > 0.001f && sceneB < 1.5f))
        continue;
      const float jpegR = srgbToLinearF(refLine[px * 3 + 0] / 255.0f);
      const float jpegG = srgbToLinearF(refLine[px * 3 + 1] / 255.0f);
      const float jpegB = srgbToLinearF(refLine[px * 3 + 2] / 255.0f);
      if (!(jpegR > 0.004f && jpegR < 0.98f) ||
          !(jpegG > 0.004f && jpegG < 0.98f) ||
          !(jpegB > 0.004f && jpegB < 0.98f))
        continue;
      const float target = luma709(jpegR, jpegG, jpegB);
      if (target < 0.015f || target > 0.85f) continue;
      const float scene = luma709(sceneR, sceneG, sceneB);
      pairs.emplace_back(scene, target);
    }
  }

  if (pairs.size() < kMinPairs) {
    LogManager::instance()->log(
        QString("[ CameraLook.cpp ] - %1: only %2 usable samples, skipping "
                "the look")
            .arg(QFileInfo(sourcePath).fileName())
            .arg(pairs.size()),
        PHOTON_DEBUG);
    return look;
  }
  look.tone = fitCameraToneCurve(pairs);
  if (!look.tone.valid) {
    LogManager::instance()->log(
        QString("[ CameraLook.cpp ] - %1: fit did not pass the acceptance "
                "gates (%2 samples)")
            .arg(QFileInfo(sourcePath).fileName())
            .arg(pairs.size()),
        PHOTON_DEBUG);
    return look;
  }
  LogManager::instance()->log(
      QString("[ CameraLook.cpp ] - fitted camera tone curve from %1 samples "
              "for %2 (scene %3..%4)")
          .arg(pairs.size())
          .arg(QFileInfo(sourcePath).fileName())
          .arg(double(look.tone.knots[0][0]))
          .arg(double(look.tone.knots[kToneKnots - 1][0])),
      PHOTON_DEBUG);
  return look;
}

}  // namespace cameralook
}  // namespace photon
