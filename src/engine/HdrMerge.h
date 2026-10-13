#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <vector>

#include <opencv2/core.hpp>

// HDR merge of exposure brackets (radiance merge, Debevec-style weighted
// average with EXIF/median gain estimation, optional OpenCV alignment and
// optional deghosting). The output is a 16-bit linear DNG scaled so the peak
// maps to 0.95, with BaselineExposure recording the scale (matching the
// panorama writer's conventions; LibRaw ignores BaselineExposure, so the
// host auto-brightness provides a normal default rendering).
class HdrMerge : public QObject {
  Q_OBJECT

 public:
  enum DeghostLevel {
    DeghostNone = 0,
    DeghostLow = 1,
    DeghostMedium = 2,
    DeghostHigh = 3
  };
  Q_ENUM(DeghostLevel)

  struct Options {
    bool align = true;
    int deghost = DeghostMedium;
    int maxEdge = 0;          // 0 = full resolution
    bool previewOnly = false; // write a PNG preview instead of a DNG
    QString previewPath;
  };

  explicit HdrMerge(QObject* parent = nullptr);

  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(bool previewBusy READ previewBusy NOTIFY previewBusyChanged)

  bool busy() const { return m_busy; }
  bool previewBusy() const { return m_previewBusy; }

  // Asynchronous merge; emits mergeCompleted with {success, message, filename,
  // width, height}.
  Q_INVOKABLE void mergeAsync(const QStringList& inputFiles, bool align,
                              int deghost);

  // Reduced-resolution preview of alignment/deghosting; emits previewReady with
  // {success, message, filename} where the filename is a temporary PNG.
  Q_INVOKABLE void previewAsync(const QStringList& inputFiles, bool align,
                                int deghost, int maxEdge = 1024);

 signals:
  void mergeCompleted(const QVariantMap& result);
  void previewReady(const QVariantMap& result);
  void busyChanged();
  void previewBusyChanged();

 private:
  struct Frame {
    cv::Mat rgb;  // CV_32FC3 linear sRGB (matching the DNG sample order)
    double exifExposure = 0.0;
    // Linear sRGB -> XYZ (D65). LibRaw/dcraw (and the panorama writer) treat
    // ColorMatrix1 as camera -> XYZ, the opposite of the DNG spec direction;
    // with data already in sRGB this renders correctly in Photon.
    double colorMatrix[9] = {0.4124564, 0.3575761, 0.1804375,
                             0.2126729, 0.7151522, 0.0721750,
                             0.0193339, 0.1191920, 0.9503041};
    float asShotNeutral[3] = {1.0f, 1.0f, 1.0f};
    QString path;
  };

  QVariantMap mergeSync(const QStringList& inputFiles, const Options& options);

  bool m_busy = false;
  bool m_previewBusy = false;

  static bool loadLinearFrame(const QString& path, Frame& frame);
  static float estimateGain(const cv::Mat& reference, const cv::Mat& frame);
  static bool alignFrame(const cv::Mat& reference, cv::Mat& frame);
  static QString writeDng(const QString& outputPath, const cv::Mat& radiance,
                          const double* colorMatrix,
                          const float* asShotNeutral, double baselineExposure);
};
