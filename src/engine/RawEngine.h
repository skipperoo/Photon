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
  Q_PROPERTY(float contrast READ contrast WRITE setContrast NOTIFY contrastChanged)
  Q_PROPERTY(float highlights READ highlights WRITE setHighlights NOTIFY highlightsChanged)
  Q_PROPERTY(float shadows READ shadows WRITE setShadows NOTIFY shadowsChanged)
  Q_PROPERTY(float whites READ whites WRITE setWhites NOTIFY whitesChanged)
  Q_PROPERTY(float blacks READ blacks WRITE setBlacks NOTIFY blacksChanged)
  Q_PROPERTY(float vibrance READ vibrance WRITE setVibrance NOTIFY vibranceChanged)
  Q_PROPERTY(float saturation READ saturation WRITE setSaturation NOTIFY saturationChanged)
  Q_PROPERTY(float temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged)
  Q_PROPERTY(float tint READ tint WRITE setTint NOTIFY tintChanged)
  Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
  Q_PROPERTY(bool halfSize READ halfSize WRITE setHalfSize NOTIFY halfSizeChanged)

 public:
  explicit RawEngine(QObject* parent = nullptr);
  ~RawEngine();

  QString source() const { return m_source; }
  void setSource(const QString& source);

  float exposure() const { return m_exposure; }
  void setExposure(float ev);

  float contrast() const { return m_contrast; }
  void setContrast(float val);

  float highlights() const { return m_highlights; }
  void setHighlights(float val);

  float shadows() const { return m_shadows; }
  void setShadows(float val);

  float whites() const { return m_whites; }
  void setWhites(float val);

  float blacks() const { return m_blacks; }
  void setBlacks(float val);

  float vibrance() const { return m_vibrance; }
  void setVibrance(float val);

  float saturation() const { return m_saturation; }
  void setSaturation(float val);

  float temperature() const { return m_temperature; }
  void setTemperature(float val);

  float tint() const { return m_tint; }
  void setTint(float val);

  bool isLoading() const { return m_isLoading; }

  bool halfSize() const { return m_halfSize; }
  void setHalfSize(bool half);

  // Asynchronous load
  void loadRawFileAsync(const QString& path);

  QImage getThumbnail();

  static QImage extractThumbnail(const QString& path);

  const uchar* getProcessedData(int& width, int& height, int& colors);

 signals:
  void sourceChanged();
  void exposureChanged();
  void contrastChanged();
  void highlightsChanged();
  void shadowsChanged();
  void whitesChanged();
  void blacksChanged();
  void vibranceChanged();
  void saturationChanged();
  void temperatureChanged();
  void tintChanged();
  void imageLoaded();
  void isLoadingChanged();
  void halfSizeChanged();
  void errorOccurred(const QString& error);

 private:
  QString m_source;
  float m_exposure = 0.0f;
  float m_contrast = 1.0f;
  float m_highlights = 0.0f;
  float m_shadows = 0.0f;
  float m_whites = 0.0f;
  float m_blacks = 0.0f;
  float m_vibrance = 0.0f;
  float m_saturation = 0.0f;
  float m_temperature = 0.0f;
  float m_tint = 0.0f;
  bool m_isLoading = false;
  bool m_halfSize = false;
  std::unique_ptr<LibRaw> m_processor;
  libraw_processed_image_t* m_processedImage = nullptr;
  bool m_isLoaded = false;

  QFutureWatcher<bool> m_loadWatcher;

  void clearProcessedImage();
  void updateProcessingParams();
  bool loadRawFileSync(const QString& path);
};
