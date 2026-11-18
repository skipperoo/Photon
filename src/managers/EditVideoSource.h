#pragma once

#include <QImage>
#include <QJsonObject>
#include <QString>
#include <cstdint>
#include <vector>

#include "../engine/EditVideo.h"

class QRhi;
class QQuickWindow;

namespace photon {

// A photo prepared for the edit video: its develop source (as the export
// loads it), optionally downscaled for a fast preview, and the AI-denoised
// variant when the photo uses AI denoise. render() develops the photo with a
// set of Develop groups applied, exactly as the export would (at this
// source's resolution).
class EditVideoSource {
 public:
  // maxLongEdge: 0 = full resolution (export), else downscale the source so
  // its long edge is at most this many pixels (preview).
  bool load(const QString& path, int maxLongEdge, QRhi* rhi = nullptr,
            QQuickWindow* window = nullptr);

  const QString& path() const { return m_path; }
  const QJsonObject& edit() const { return m_full; }
  // Groups with at least one key in the edit, in panel order.
  std::vector<int> editedGroups() const;
  // The photo with only `groups` applied (8-bit sRGB, Format_RGB32).
  QImage render(editvideo::GroupMask groups) const;

 private:
  QString m_path;
  std::vector<uint16_t> m_pixels;
  std::vector<uint16_t> m_aiPixels;
  int m_width = 0;
  int m_height = 0;
  QJsonObject m_full;
  QJsonObject m_aiSettings;
  bool m_haveAi = false;
  QRhi* m_rhi = nullptr;
  QQuickWindow* m_window = nullptr;
};

}  // namespace photon
