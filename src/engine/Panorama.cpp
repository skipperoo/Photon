#include "Panorama.h"
using namespace photon;


void Panorama::stitchAsync(const QStringList& inputFiles) {
  QThread* thread = QThread::create([this, inputFiles]() {
    QString message;
    QVariantMap result = stitchPhotos(inputFiles);

    QMetaObject::invokeMethod(this, [this, result]() {
        emit stitchCompleted(result);
    });
  });
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  thread->start();
  LogManager::instance()->log(QString("Started panorama stitching thread"), INFO);
}



QVariantMap Panorama::stitchPhotos(const QStringList& inputFiles) {
  QVariantMap result;
  if (inputFiles.size() < 2) {
    LogManager::instance()->log(
      QString("[ Panorama.cpp ] - Cannot stitch a single image"),
      ERROR);

    result["message"] = QString("Select more photos!");
    result["success"] = false;
    return result;
  }

  std::vector<cv::Mat> images, estImgs;
  for (const auto& filename : inputFiles) {
    cv::Mat img = raw_to_linear(filename);
    if (img.empty()) {
      LogManager::instance()->log(
        QString("[ Panorama.cpp ] - Failed to process image %1").arg(filename),
        ERROR);
      continue;
    }
    images.push_back(img);
    cv::Mat img32F, img8;
    img.convertTo(img32F, CV_32F, 1.0 / 65535.0);
    // Apply a 2.2 gamma curve to brighten shadows/midtones so OpenCV can "see" the features
    cv::pow(img32F, 1.0 / 2.2, img32F);
    img32F.convertTo(img8, CV_8U, 255.0);
    estImgs.push_back(img8);
  }

  LogManager::instance()->log(
    QString("[ Panorama.cpp ] - Merging %1 images").arg(images.size()),
    DEBUG);

  cv::Ptr<cv::Stitcher> stitcher = cv::Stitcher::create(cv::Stitcher::PANORAMA);
  stitcher->setExposureCompensator(cv::makePtr<cv::detail::NoExposureCompensator>());
  cv::Ptr<cv::detail::Blender> blender = cv::detail::Blender::createDefault(cv::detail::Blender::MULTI_BAND);
  stitcher->setBlender(blender);
  cv::Stitcher::Status status = stitcher->estimateTransform(estImgs);
  if (status != cv::Stitcher::OK) {
    std::string errorReason;
    switch (status) {
        case cv::Stitcher::ERR_NEED_MORE_IMGS:
            errorReason = "not enough matching features found. The images may lack contrast, or there is too little overlap between them.";
            break;
        case cv::Stitcher::ERR_HOMOGRAPHY_EST_FAIL:
            errorReason = "failed to align the images. The overlap might be too small or contain moving subjects.";
            break;
        case cv::Stitcher::ERR_CAMERA_PARAMS_ADJUST_FAIL:
            errorReason = "failed to optimize camera parameters. The images might have extreme lens distortion or inconsistent exposure.";
            break;
        default:
            errorReason = "Unknown error occurred (Code: " + std::to_string(int(status)) + ").";
            break;
    }
    LogManager::instance()->log(
      QString("[ Panorama.cpp ] - Stitching failed: %1").arg(errorReason),
      ERROR);
    result["message"] = QString("Stitching failed: %1").arg(errorReason);
    result["success"] = false;
    return result;
  }

  cv::Mat panoramaBGR;
  status = stitcher->composePanorama(images, panoramaBGR);

  if (status != cv::Stitcher::OK) {
    LogManager::instance()->log(
      QString("[ Panorama.cpp ] - Stitching failed during composition!"),
      ERROR);
    result["message"] = QString("Stitching failed!");
    result["success"] = false;
    return result;

  }
  cv::Mat panoramaRGB;

  cv::cvtColor(panoramaBGR, panoramaRGB, cv::COLOR_BGR2RGB);
  panoramaRGB.convertTo(panoramaRGB, cv::COLOR_16U);

  tinydngwriter::DNGWriter dngwriter(false);
  tinydngwriter::DNGImage dngimage;

  dngimage.SetSubfileType(false, false, false);
  dngimage.SetImageWidth(panoramaRGB.cols);
  dngimage.SetImageLength(panoramaRGB.rows);
  dngimage.SetRowsPerStrip(panoramaRGB.rows);
  dngimage.SetSamplesPerPixel(3); // 3 channels rgb
  uint16_t bps[3] = {16, 16, 16};

  dngimage.SetBitsPerSample(3, bps);
  dngimage.SetPlanarConfig(tinydngwriter::PLANARCONFIG_CONTIG);
  dngimage.SetCompression(tinydngwriter::COMPRESSION_NONE);
  dngimage.SetPhotometric(tinydngwriter::PHOTOMETRIC_RGB);
  //
  // Standard DNG tags for color calibration.
  // Since we outputted to sRGB from LibRaw, an identity matrix is technically 
  // sufficient, but D65 illuminant provides standard baseline compatibility.
  uint16_t illuminant = 21; // D65
  dngimage.SetCalibrationIlluminant1(illuminant);
  double colorMatrix[9] = {
      1.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 1.0f
  };
  dngimage.SetColorMatrix1(3, colorMatrix);
  dngimage.SetImageData(reinterpret_cast<uint8_t*>(panoramaRGB.data), panoramaRGB.total() * panoramaRGB.elemSize());
  dngwriter.AddImage(&dngimage);
  QString filename =
    QDir::toNativeSeparators(
      QString(
        "%1/%2.pano.dng"
      ).arg(
        QFileInfo(inputFiles[0]).absolutePath(),
        QFileInfo(inputFiles[0]).baseName()
      )
    );

  LogManager::instance()->log(
    QString("[ Panorama.cpp ] - Saving panorama to %1").arg(filename),
    INFO);
  
  std::string errMsg;
  if (
    !dngwriter.WriteToFile(
      filename.toStdString().c_str(),
      &errMsg
    )
  ) {

  LogManager::instance()->log(
    QString("[ Panorama.cpp ] - Couldn't write dng file %1: %2").arg(filename, errMsg),
    ERROR);
    result["message"] = QString("Couldn't write dng file %1: %2").arg(filename, errMsg);
    result["success"] = false;
    return result;
  }

    result["message"] = QString("Panorama create successfully and saved to %1!").arg(filename);
    result["success"] = true;
    return result;

}
