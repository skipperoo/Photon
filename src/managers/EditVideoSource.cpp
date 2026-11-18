#include "EditVideoSource.h"

#include <algorithm>
#include <cmath>

#include "../engine/AiDenoiser.h"
#include "../engine/ImageDeveloper.h"
#include "AppStateManager.h"
#include "ExportManager.h"
#include "LogManager.h"

namespace photon {

bool EditVideoSource::load(const QString& path, int maxLongEdge, QRhi* rhi,
                           QQuickWindow* window) {
  m_path = path;
  m_rhi = rhi;
  m_window = window;
  m_haveAi = false;
  std::vector<uint16_t>().swap(m_aiPixels);

  DevelopSource source;
  if (!loadDevelopSource(path, source)) {
    LogManager::instance()->log(
        QString("[ EditVideoSource.cpp ] - cannot read %1").arg(path),
        PHOTON_WARNING);
    return false;
  }
  m_full = source.settings;
  m_full["denoiseSecondPass"] = true;
  m_full["outputBits"] = 8;
  m_full["outputColorSpace"] = QStringLiteral("srgb");

  // Preview: a box-filtered smaller source (the develop is resolution
  // independent: geometry is normalised, effects scale with the image).
  const int longEdge = std::max(source.width, source.height);
  if (maxLongEdge > 0 && longEdge > maxLongEdge) {
    const double s = double(maxLongEdge) / longEdge;
    const int w = std::max(8, int(std::lround(source.width * s)));
    const int h = std::max(8, int(std::lround(source.height * s)));
    std::vector<uint16_t> small(size_t(w) * h * 3);
    photon::ai::resampleRgb16(source.pixels.data(), source.width,
                              source.height, small.data(), w, h);
    source.pixels = std::move(small);
    source.width = w;
    source.height = h;
  }
  m_pixels = std::move(source.pixels);
  m_width = source.width;
  m_height = source.height;

  // AI denoise is a property of the source: steps with Detail applied use
  // the AI-blended pixels (the product is resampled to this size).
  if (source.raw) {
    m_full["denoiseMode"] = ::AppStateManager::instance()->denoiseEngine();
    m_aiPixels = m_pixels;
    m_aiSettings = m_full;
    m_haveAi = photon::ai::applyToDevelopSource(path, m_aiPixels.data(), m_width,
                                                m_height, m_aiSettings, true);
    if (!m_haveAi) {
      m_full = m_aiSettings;  // Classic fallback recorded in the settings
      std::vector<uint16_t>().swap(m_aiPixels);
    }
  }
  return true;
}

std::vector<int> EditVideoSource::editedGroups() const {
  std::vector<int> groups;
  const QJsonObject& edit = m_haveAi ? m_aiSettings : m_full;
  for (int g = 0; g < int(editvideo::editGroups().size()); ++g) {
    if (editvideo::editHasGroup(edit, g)) groups.push_back(g);
  }
  return groups;
}

QImage EditVideoSource::render(editvideo::GroupMask groups) const {
  if (m_pixels.empty()) return QImage();
  const int detail = editvideo::groupOfKey(QStringLiteral("denoiseAmount"));
  const bool useAi = m_haveAi && (groups & editvideo::groupBit(detail));
  const QJsonObject settings =
      editvideo::stepSettings(useAi ? m_aiSettings : m_full, groups);
  const std::vector<uint16_t>& pixels = useAi ? m_aiPixels : m_pixels;
  return ImageDeveloper::develop(pixels.data(), m_width, m_height, settings,
                                 m_rhi, m_window)
      .convertToFormat(QImage::Format_RGB32);
}

}  // namespace photon
