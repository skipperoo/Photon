#pragma once

#include <QFuture>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QVariantList>

class QRhi;
class QQuickWindow;

namespace photon {

class PreviewManager : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool isProcessing READ isProcessing NOTIFY isProcessingChanged)
  Q_PROPERTY(float progress READ progress NOTIFY progressChanged)

 public:
  explicit PreviewManager(QObject* parent = nullptr);
  ~PreviewManager() override;

  static PreviewManager* instance();

  void setRhi(QRhi* rhi) { m_rhi = rhi; }
  void setWindow(QQuickWindow* window) { m_window = window; }

  // Get path to cached preview if it exists and is valid
  Q_INVOKABLE QString getPreviewPath(const QString& rawPath) const;

  // Start background scan of a folder
  Q_INVOKABLE void startFolderScan(const QString& folderPath);

  // Force refresh of a single image preview (e.g. after edit)
  Q_INVOKABLE void refreshPreview(const QString& rawPath);

  Q_INVOKABLE void cancelAll();

  bool isProcessing() const { return m_isProcessing; }
  float progress() const {
    return m_total > 0 ? (float)m_done / m_total : 0.0f;
  }

 signals:
  void isProcessingChanged();
  void progressChanged();
  void previewReady(const QString& rawPath, const QString& cachePath);

 private:
  void processItem(const QString& rawPath, bool skipGpu = false);
  bool isPreviewValid(const QString& rawPath) const;
  QString getCachePath(const QString& rawPath) const;

  QRhi* m_rhi = nullptr;
  QQuickWindow* m_window = nullptr;
  static PreviewManager* s_instance;
  bool m_isProcessing = false;
  bool m_abort = false;
  int m_done = 0;
  int m_total = 0;
  mutable QMutex m_mutex;
  QThreadPool* m_threadPool;

  // Guard against overlapping single-file preview refreshes
  bool m_refreshRunning = false;
  QString m_pendingRefreshPath;
};

}  // namespace photon
