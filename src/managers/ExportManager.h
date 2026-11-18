#pragma once

#include <QFutureWatcher>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <atomic>
#include <cstdint>
#include <vector>

#include "../engine/EditVideo.h"

class QRhi;
class QQuickWindow;

namespace photon {

// A photo ready to develop as the export does: the working-space source
// (16-bit RGB, Rec.2020 primaries, sRGB curve, upright) and its settings
// (the current edit state plus the render keys the export injects: camera
// look curve, white-balance gains, EXIF for lens matching, RAW base
// exposure). AI denoise is NOT applied (see photon::ai::applyToDevelopSource).
struct DevelopSource {
  std::vector<uint16_t> pixels;
  int width = 0;
  int height = 0;
  bool raw = false;
  QJsonObject settings;
};
bool loadDevelopSource(const QString& path, DevelopSource& out);

// Edit video export settings (chosen in the edit video dialog).
struct VideoExportOptions {
  QString aspect = QStringLiteral("original");  // "original", "4:5", ...
  int shortSide = 1080;                          // 1080 or 2160 (4K)
  editvideo::PillOptions pill;
  bool showLine = true;          // the before/after slider's vertical line
  std::vector<int> shownGroups;  // groups shown one by one, in panel order
  bool tweaks = false;           // the other edited groups as "Tweaks"
};

class ExportManager : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool isExporting READ isExporting NOTIFY isExportingChanged)
  Q_PROPERTY(float progress READ progress NOTIFY progressChanged)
  Q_PROPERTY(int doneCount READ doneCount NOTIFY progressChanged)
  Q_PROPERTY(int totalCount READ totalCount NOTIFY progressChanged)

 public:
  explicit ExportManager(QObject* parent = nullptr);
  ~ExportManager() override;

  void setRhi(QRhi* rhi) { m_rhi = rhi; }
  void setWindow(QQuickWindow* window) { m_window = window; }

  bool isExporting() const { return m_isExporting; }
  // Done photos plus the fraction of the current one (videos report it).
  float progress() const {
    return m_totalCount > 0
               ? (float(m_doneCount) + m_itemProgress.load()) / m_totalCount
               : 0.0f;
  }
  int doneCount() const { return m_doneCount; }
  int totalCount() const { return m_totalCount; }

  // bitDepth: 8 or 16 (16 applies to TIFF only); colorSpace: "srgb" or
  // "adobergb".
  Q_INVOKABLE void startExport(const QStringList& paths,
                               const QString& outputFolder,
                               const QString& format, int quality = 90,
                               int bitDepth = 8,
                               const QString& colorSpace = "srgb");
  // Edit video (MP4) of one photo: the original, the shown Develop groups
  // fading in one by one under a pill with their names (the others together
  // as "Tweaks"), then the before/after sliders. Written to
  // <outputFolder>/<name>.mp4.
  void startVideoExport(const QString& path, const QString& outputFolder,
                        const VideoExportOptions& options);
  QRhi* rhi() const { return m_rhi; }
  QQuickWindow* window() const { return m_window; }
  // True when this build can write videos (FFmpeg with an H.264 encoder).
  Q_INVOKABLE bool videoExportAvailable() const;
  Q_INVOKABLE void cancelExport();

 signals:
  void isExportingChanged();
  void progressChanged();
  void exportFinished(int successCount, int failedCount);

 private:
  QRhi* m_rhi = nullptr;
  QQuickWindow* m_window = nullptr;
  bool m_isExporting = false;
  int m_doneCount = 0;
  int m_totalCount = 0;
  std::atomic<float> m_itemProgress{0.0f};
  std::atomic<bool> m_abortExport{false};
  QFutureWatcher<void> m_watcher;

  void begin(int count);
  void processExport(const QStringList& paths, const QString& outputFolder,
                     const QString& format, int quality, int bitDepth,
                     const QString& colorSpace);
  bool exportVideo(const QString& path, const QString& outPath,
                   const VideoExportOptions& options);
};

}  // namespace photon
