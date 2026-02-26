#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <atomic>

class QRhi;
class QQuickWindow;

namespace photon {

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
  float progress() const {
    return m_totalCount > 0 ? (float)m_doneCount / m_totalCount : 0.0f;
  }
  int doneCount() const { return m_doneCount; }
  int totalCount() const { return m_totalCount; }

  Q_INVOKABLE void startExport(const QStringList& paths,
                               const QString& outputFolder,
                               const QString& format, int quality = 90);
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
  std::atomic<bool> m_abortExport{false};
  QFutureWatcher<void> m_watcher;

  void processExport(const QStringList& paths, const QString& outputFolder,
                     const QString& format, int quality);
};

}  // namespace photon
