#pragma once

#include <QObject>
#include <QGuiApplication>
#include <QString>
#include <QThread>
#include <QtQml/qqml.h>
#include <libraw/libraw.h>
#include <opencv2/opencv.hpp>
#include <opencv2/stitching.hpp>
#include <opencv2/stitching/detail/exposure_compensate.hpp>
#include <opencv2/stitching/detail/blenders.hpp>
#include <opencv2/core/ocl.hpp>
#include <tiffio.h>
/*
extern "C" {
  #include "libdng/libdng.h"
}*/

namespace photon {

struct ColorInfo {
    float matrix[9];      // XYZ→camera (for ColorMatrix1)
    float asShotNeutral[3]; // normalized WB

};

class Panorama : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

private:
  static cv::Mat raw_to_linear(const QString& file, std::unique_ptr<ColorInfo>& colorInfo);

  void estimateTransform(cv::Stitcher& stitcher);
  QVariantMap stitchPhotos(const QStringList& inputFiles, bool compensateExposure, size_t featuresThreshold = 2000);

public:
  explicit Panorama(QObject* parent = nullptr) : QObject(parent) {}
  Q_INVOKABLE void stitchAsync(const QStringList& inputFiles, bool compensateExposure = false);
signals:
  void stitchCompleted(QVariantMap result);
};

}
