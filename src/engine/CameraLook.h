#pragma once

#include <QJsonObject>
#include <QString>

#include "CameraLookMath.h"

namespace photon {
namespace cameralook {

// The version of the fit: how a raw's tone curve is chosen. Bump whenever a
// change makes the fit give a different result for the same file; caches
// written by an older version are refitted.
constexpr int kCameraLookVersion = 1;

struct CameraLook {
  int version = kCameraLookVersion;
  CameraToneCurve tone;
};

// Cache location: <photoDir>/.PhotonData/cache/looks/<fileName>.json
QString cameraLookCachePath(const QString& sourcePath);
CameraLook loadCameraLook(const QString& sourcePath);
bool saveCameraLook(const QString& sourcePath, const CameraLook& look);

// Fits the tone curve of one raw from its embedded camera JPEG. Decodes the
// raw the way the render does (same LibRaw parameters, half resolution) and
// pairs it with the camera JPEG; heavy, run it on a worker thread.
CameraLook fitCameraLook(const QString& sourcePath);

QJsonObject toneCurveToJson(const CameraToneCurve& curve);
CameraToneCurve toneCurveFromJson(const QJsonObject& obj);

}  // namespace cameralook
}  // namespace photon
