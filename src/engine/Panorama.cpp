#include "Panorama.h"
using namespace photon;


void Panorama::stitchAsync(const QStringList& inputFiles) {
  QThread* thread = QThread::create([this, inputFiles]() {

  });
}

void Panorama::stitchPhotos(const std::vector<QString> inputFiles) {
  if (inputFiles.size() < 2) {
    LogManager::instance()->log(
      QString("[ Panorama.cpp ] - Cannot stitch a single image"),
      ERROR);
  }

  std::vector<cv::Mat> images;
  for (const auto& filename : inputFiles) {
    cv::Mat img = raw_to_linear(filename);
    if (img.empty()) {
      LogManager::instance()->log(
        QString("[ Panorama.cpp ] - Failed to process image %1").arg(filename),
        ERROR);
      continue;
    }
    images.push_back(img);
  }

  LogManager::instance()->log(
    QString("[ Panorama.cpp ] - Merging %1 images").arg(images.size()),
    DEBUG);

  cv::Mat panoramaBGR;
  cv::Ptr<cv::Stitcher> stitcher = cv::Stitcher::create(cv::Stitcher::PANORAMA);

  cv::Stitcher::Status status = stitcher->stitch(images, panoramaBGR);

  if (status != cv::Stitcher::OK) {
    LogManager::instance()->log(
      QString("[ Panorama.cpp ] - Stitcher failed!"),
      DEBUG);
    // emit result
  }

  cv::Mat panoramaRGB;

  cv::cvtColor(panoramaBGR, panoramaRGB, cv::COLOR_BGR2RGB);

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
    ERROR);
  
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
    // emit failure
  }

}
