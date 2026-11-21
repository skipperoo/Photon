#pragma once

#include <QJsonObject>
#include <QString>

#include "CameraLookMath.h"

namespace photon {
namespace cameralook {

// The version of the fit: how a raw's tone curve is chosen. Bump whenever a
// change makes the fit give a different result for the same file; caches
// written by an older version are refitted.
// 2: a second curve for the AgX profile.
// 3: relaxed improvement gate; photos without a usable look are cached too.
constexpr int kCameraLookVersion = 3;

struct CameraLook {
  int version = kCameraLookVersion;
  // The look for the normal and black & white profiles: scene luminance ->
  // camera luminance.
  CameraToneCurve tone;
  // The look for the AgX profile: AgX-rendered luminance -> camera luminance
  // (AgX is a tone map of its own, so the look is fitted on its output; at
  // 100% both profiles land on the camera's rendering). May be invalid when
  // AgX alone is already as close to the camera as the fit allows.
  CameraToneCurve agxTone;
  // The fit ran and this photo has no usable look (no embedded JPEG, a JPEG
  // that does not match the sensor, too few samples, or a rejected fit). It is
  // cached so the fit is not repeated; transient decode errors leave it false.
  bool unavailable = false;

  const CameraToneCurve& forProfile(bool agx) const { return agx ? agxTone : tone; }
};

// Cache location: <photoDir>/.PhotonData/cache/looks/<fileName>.json
QString cameraLookCachePath(const QString& sourcePath);
CameraLook loadCameraLook(const QString& sourcePath);
bool saveCameraLook(const QString& sourcePath, const CameraLook& look);

// Fits the tone curve of one raw from its embedded camera JPEG. Decodes the
// raw the way the render does (same LibRaw parameters, half resolution) and
// pairs it with the camera JPEG; heavy, run it on a worker thread.
CameraLook fitCameraLook(const QString& sourcePath);

// The cached look, or a fresh fit (saved) when the cache is missing, stale or
// purged; a cached "unavailable" result is returned without refitting.
// Blocking: for worker threads (export, library previews).
CameraLook obtainCameraLook(const QString& sourcePath);

// Sets settings["cameraLook"] (and settings["cameraLookAgx"]) for a RAW whose
// cameraLookAmount is > 0, so a develop matches the interactive preview.
void applyToSettings(const QString& sourcePath, QJsonObject& settings);

QJsonObject toneCurveToJson(const CameraToneCurve& curve);
CameraToneCurve toneCurveFromJson(const QJsonObject& obj);

}  // namespace cameralook
}  // namespace photon
