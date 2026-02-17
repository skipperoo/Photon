#pragma once

#include <libraw/libraw.h>

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>
#include <QtConcurrent>
#include <memory>
#include <vector>

class RawEngine : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
  Q_PROPERTY(
      float exposure READ exposure WRITE setExposure NOTIFY exposureChanged)
  Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)

 public:
  explicit RawEngine(QObject* parent = nullptr);
  ~RawEngine();

  QString source() const { return m_source; }
  void setSource(const QString& source);

  float exposure() const { return m_exposure; }
  void setExposure(float ev);

  bool isLoading() const { return m_isLoading; }

  // Asynchronous load
  void loadRawFileAsync(const QString& path);

  QImage getThumbnail();

  static QImage extractThumbnail(const QString& path);

  const uchar* getProcessedData(int& width, int& height, int& colors);

 signals:
  void sourceChanged();
  void exposureChanged();
  void imageLoaded();
  void isLoadingChanged();
  void errorOccurred(const QString& error);

 private:
  QString m_source;
  float m_exposure = 0.0f;
  bool m_isLoading = false;
  std::unique_ptr<LibRaw> m_processor;
  libraw_processed_image_t* m_processedImage = nullptr;
  bool m_isLoaded = false;

  QFutureWatcher<bool> m_loadWatcher;

  void clearProcessedImage();
  void updateProcessingParams();
  bool loadRawFileSync(const QString& path);
};
