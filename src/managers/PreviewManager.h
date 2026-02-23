#pragma once

#include <QFuture>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <atomic>

namespace photon {

class PreviewManager : public QObject {
  Q_OBJECT
  Q_PROPERTY(float progress READ progress NOTIFY progressChanged)
  Q_PROPERTY(bool isProcessing READ isProcessing NOTIFY isProcessingChanged)

 public:
  explicit PreviewManager(QObject* parent = nullptr);
  ~PreviewManager() override;

  static PreviewManager* instance();

  float progress() const {
    return m_total > 0 ? (float)m_done / m_total : 0.0f;
  }
  bool isProcessing() const { return m_isProcessing; }

  /**
   * @brief Returns the absolute path to the cached preview for a given RAW
   * file. Checks if it exists and is valid.
   */
  Q_INVOKABLE QString getPreviewPath(const QString& rawPath) const;

  /**
   * @brief Triggers background generation for an entire folder.
   */
  Q_INVOKABLE void startFolderScan(const QString& folderPath);

  /**
   * @brief Triggers immediate refresh for a specific file (e.g. after edit).
   */
  Q_INVOKABLE void refreshPreview(const QString& rawPath);

  /**
   * @brief Cancels all pending preview generation tasks.
   */
  Q_INVOKABLE void cancelAll();

 signals:
  void progressChanged();
  void isProcessingChanged();
  void previewReady(const QString& rawPath, const QString& previewPath);

 private:
  static PreviewManager* s_instance;
  QThreadPool* m_threadPool;
  std::atomic<bool> m_abort{false};
  bool m_isProcessing = false;
  int m_done = 0;
  int m_total = 0;

  QString getCachePath(const QString& rawPath) const;
  bool isPreviewValid(const QString& rawPath) const;
  void processItem(const QString& rawPath);
};

}  // namespace photon
