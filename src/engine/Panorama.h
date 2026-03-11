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
#include "../managers/LogManager.h"
#include "../libraries/tiny_dng_writer.h"

namespace photon {

class Panorama : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

private:
  static cv::Mat raw_to_linear(QString file) {
    LibRaw processor;
    processor.imgdata.params.output_bps = 16;
    processor.imgdata.params.gamm[0] = 1.0;
    processor.imgdata.params.gamm[1] = 1.0;
    processor.imgdata.params.no_auto_bright = 1;
    processor.imgdata.params.use_auto_wb = 1;
    processor.imgdata.params.output_color = 1;
    if (processor.open_file(file.toStdString().c_str()) != LIBRAW_SUCCESS) {
      LogManager::instance()->log(
        QString("[ Panorama.cpp ] - Cannot open file %1").arg(file),
        ERROR);
      return cv::Mat();
    }

    if (processor.unpack() != LIBRAW_SUCCESS) {
      LogManager::instance()->log(
        QString("[ Panorama.cpp ] - Cannot unpack data of file %1").arg(file),
        ERROR);
      return cv::Mat();
    }

    if (processor.dcraw_process() != LIBRAW_SUCCESS) {
      LogManager::instance()->log(
        QString("[ Panorama.cpp ] - Cannot dcraw file %1").arg(file),
        ERROR);
      return cv::Mat();
    }

    libraw_processed_image_t *image = processor.dcraw_make_mem_image();

    if (!image) {
      LogManager::instance()->log(
        QString("[ Panorama.cpp ] - Cannot create procesed image from %1").arg(file),
        ERROR);
      return cv::Mat();
    }

    cv::Mat rawRGB(image->height, image->width, CV_16UC3, image->data);
    cv::Mat matBGR;
    cv::cvtColor(rawRGB, matBGR, cv::COLOR_RGB2BGR);
    LibRaw::dcraw_clear_mem(image);

    return matBGR;
  }

  void estimateTransform(cv::Stitcher& stitcher);
  QVariantMap stitchPhotos(const QStringList& inputFiles);

public:
  explicit Panorama(QObject* parent = nullptr) : QObject(parent) {}
  Q_INVOKABLE void stitchAsync(const QStringList& inputFiles);
signals:
  void stitchCompleted(QVariantMap result);
};

}
