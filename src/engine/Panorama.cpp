#include "Panorama.h"
#include "../managers/LogManager.h"

using namespace photon;


static ColorInfo extractColorInfo(LibRaw *processor) {
  ColorInfo info;

  // cam_xyz is [4][3]: camera-RGB → XYZ D50
  // Only first 3 rows (R,G,B) are needed; row 3 is an unused 4th channel
  double cam2xyz[3][3];
  for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++)
          cam2xyz[i][j] = processor->imgdata.color.cam_xyz[i][j];


  cv::Mat C(3, 3, CV_64F, cam2xyz);
  // Not sure if the inverse is neede here
  // cv::Mat Cinv = C.inv();   // now XYZ D50 → camera: this is ColorMatrix1

  for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++)
          info.matrix[i * 3 + j] = C.at<double>(i, j);

  // WB is baked in (use_auto_wb=1), so tell DNG "no further WB needed"
  info.asShotNeutral[0] = 1.0f;
  info.asShotNeutral[1] = 1.0f;
  info.asShotNeutral[2] = 1.0f;


  return info;
}

void Panorama::stitchAsync(const QStringList& inputFiles,
                           bool compensateExposure) {
  QThread* thread = QThread::create([this, inputFiles, compensateExposure]() {
    QVariantMap result = stitchPhotos(inputFiles, compensateExposure);
    QMetaObject::invokeMethod(
        this, [this, result]() { emit stitchCompleted(result); });
  });
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  thread->start();
  LogManager::instance()->log(
      QString("[ Panorama.cpp ] - Started panorama stitching thread"), INFO);
}

cv::Mat Panorama::raw_to_linear(const QString& file, std::unique_ptr<ColorInfo>& colorInfo) {
  LibRaw processor;
  processor.imgdata.params.output_bps = 16;
  processor.imgdata.params.no_auto_bright = 1;
  processor.imgdata.params.use_camera_wb = 1;
  processor.imgdata.params.output_color = 0;
  processor.imgdata.params.use_camera_matrix = 1;
  if (processor.open_file(file.toStdString().c_str()) != LIBRAW_SUCCESS) {
    LogManager::instance()->log(
        QString("[ Panorama.cpp ] - Cannot open file %1").arg(file), ERROR);
    return cv::Mat();
  }

  if (processor.unpack() != LIBRAW_SUCCESS) {
    LogManager::instance()->log(
        QString("[ Panorama.cpp ] - Cannot unpack data of file %1").arg(file),
        ERROR);
    return cv::Mat();
  }
  if (!colorInfo.get()) {
     colorInfo = std::make_unique<ColorInfo> (ColorInfo (extractColorInfo(&processor)));
    LogManager::instance()->log(
        QString("[ Panorama.cpp ] - Initialized ColorInfo on %1").arg(file),
        DEBUG);
  }

  if (processor.dcraw_process() != LIBRAW_SUCCESS) {
    LogManager::instance()->log(
        QString("[ Panorama.cpp ] - Cannot dcraw file %1").arg(file), ERROR);
    return cv::Mat();
  }

  libraw_processed_image_t* image = processor.dcraw_make_mem_image();

  if (!image) {
    LogManager::instance()->log(
        QString("[ Panorama.cpp ] - Cannot create processed image from %1")
            .arg(file),
        ERROR);
    return cv::Mat();
  }

  cv::Mat rawRGB(image->height, image->width, CV_16UC3, image->data);
  cv::Mat matBGR;
  cv::cvtColor(rawRGB, matBGR, cv::COLOR_RGB2BGR);
  LibRaw::dcraw_clear_mem(image);

  return matBGR;
}

QVariantMap Panorama::stitchPhotos(const QStringList& inputFiles,
                                   bool compensateExposure, size_t featuresThreshold) {
  cv::ocl::setUseOpenCL(true);
  QVariantMap result;

  if (inputFiles.size() < 2) {
    LogManager::instance()->log(
        "[ Panorama.cpp ] - Cannot stitch a single image", ERROR);
    result["message"] = QString("Select more photos!");
    result["success"] = false;
    return result;
  }

  LogManager::instance()->log(
      QString("[ Panorama.cpp ] - Loading %1 images").arg(inputFiles.size()),
      INFO);

  std::unique_ptr<ColorInfo> colorInfo;

  // These are the 16 bit images loaded from the camera
  std::vector<cv::Mat> images16;
  for (const auto& filename : inputFiles) {
    cv::Mat img = Panorama::raw_to_linear(filename, colorInfo);
    if (img.empty()) {
      LogManager::instance()->log(
          QString("[ Panorama.cpp ] - Failed to process image %1")
              .arg(filename),
          ERROR);
      continue;
    }
    images16.push_back(img.clone());
  }

  if (images16.size() < 2) {
    LogManager::instance()->log(
        "[ Panorama.cpp ] - Not enough valid images to stitch", ERROR);
    result["message"] = QString("Not enough valid images to stitch");
    result["success"] = false;
    return result;
  }

  LogManager::instance()->log(
      QString("[ Panorama.cpp ] - Processing %1 images").arg(images16.size()),
      DEBUG);

  // Create 8-bit gamma-corrected images for feature detection
  // Feature detectors require 8-bit input
  std::vector<cv::Mat> images8bit;
  std::vector<cv::Size> sizes;
  for (const auto& img16 : images16) {
    cv::Mat img32F, img8;
    img16.convertTo(img32F, CV_32FC3, 1.0 / 65535.0);
    cv::pow(img32F, 1.0 / 2.2, img32F);
    img32F.convertTo(img8, CV_8UC3, 255.0);
    images8bit.push_back(img8);
    sizes.push_back(img8.size());
  }

  // PHASE 1: Feature Detection and Matching
  LogManager::instance()->log("[ Panorama.cpp ] - Phase 1: Feature detection",
                              DEBUG);

  cv::Ptr<cv::Feature2D> finder = cv::SIFT::create();
  std::vector<cv::detail::ImageFeatures> features(images8bit.size());

  for (size_t i = 0; i < images8bit.size(); i++) {
    cv::detail::computeImageFeatures(finder, images8bit[i], features[i]);
    features[i].img_idx = static_cast<int>(i);
    LogManager::instance()->log(
        QString("[ Panorama.cpp ] - Image %1: %2 features detected")
            .arg(i)
            .arg(static_cast<int>(features[i].keypoints.size())),
        DEBUG);
  }


  // Match features between images
  cv::Ptr<cv::detail::FeaturesMatcher> matcher =
      cv::makePtr<cv::detail::BestOf2NearestMatcher>(false, 0.3f);
  std::vector<cv::detail::MatchesInfo> pairwise_matches;

  (*matcher)(features, pairwise_matches);
  matcher->collectGarbage();

  // Check if we have enough matches
  int num_matches = 0;
  for (const auto& match : pairwise_matches) {
    if (match.confidence > 0.0) num_matches++;
  }

  if (num_matches < static_cast<int>(images8bit.size()) - 1) {
    LogManager::instance()->log(
        "[ Panorama.cpp ] - Not enough matching features found", ERROR);
    result["message"] =
        QString("Stitching failed: not enough matching features found.");
    result["success"] = false;
    return result;
  }

  // PHASE 2: Camera Parameter Estimation
  LogManager::instance()->log("[ Panorama.cpp ] - Phase 2: Camera estimation",
                              DEBUG);

  cv::Ptr<cv::detail::Estimator> estimator =
      cv::makePtr<cv::detail::HomographyBasedEstimator>();
  std::vector<cv::detail::CameraParams> cameras;

  if (!(*estimator)(features, pairwise_matches, cameras)) {
    LogManager::instance()->log(
        "[ Panorama.cpp ] - Homography estimation failed", ERROR);
    result["message"] =
        QString("Stitching failed: failed to align the images.");
    result["success"] = false;
    return result;
  }

  // Convert rotation matrices to CV_32F format required by bundle adjuster
  for (size_t i = 0; i < cameras.size(); ++i) {
    cameras[i].R.convertTo(cameras[i].R, CV_32F);
  }

  // Refine camera parameters with bundle adjustment
  cv::Ptr<cv::detail::BundleAdjusterBase> adjuster =
      cv::makePtr<cv::detail::BundleAdjusterRay>();

  adjuster->setConfThresh(1.0);
  if (!(*adjuster)(features, pairwise_matches, cameras)) {
    LogManager::instance()->log("[ Panorama.cpp ] - Bundle adjustment failed",
                                ERROR);
    result["message"] =
        QString("Stitching failed: failed to optimize camera parameters.");
    result["success"] = false;
    return result;
  }

  // PHASE 3: Warping Images (16-bit)
  LogManager::instance()->log("[ Panorama.cpp ] - Phase 3: Warping images",
                              DEBUG);

  // Find median focal length
  std::vector<double> focals;
  for (size_t i = 0; i < cameras.size(); ++i) {
    focals.push_back(cameras[i].focal);
  }
  std::sort(focals.begin(), focals.end());
  float median_focal = static_cast<float>(focals[focals.size() / 2]);

  // Create cylindrical warper with scale based on focal length
  float warped_image_scale = median_focal;
  cv::Ptr<cv::WarperCreator> warper_creator =
      cv::makePtr<cv::CylindricalWarper>();
  cv::Ptr<cv::detail::RotationWarper> warper =
      warper_creator->create(static_cast<float>(warped_image_scale));

  LogManager::instance()->log("[ Panorama.cpp ] - Created warper", DEBUG);
  // Warp images and create masks
  std::vector<cv::Mat> images_warped16;
  std::vector<cv::Mat> masks_warped;
  std::vector<cv::UMat> images_warped16_umat;
  std::vector<cv::UMat> masks_warped_umat;
  std::vector<cv::Point> corners;
  std::vector<cv::Size> sizes_warped;

  for (size_t i = 0; i < images16.size(); i++) {
    cv::Mat K;
    cameras[i].K().convertTo(K, CV_32F);
    cv::Rect roi = warper->warpRoi(sizes[i], K, cameras[i].R);
    corners.push_back(roi.tl());
    sizes_warped.push_back(roi.size());

    // Warp the 16-bit image
    cv::Mat warped;
    warper->warp(images16[i], K, cameras[i].R, cv::INTER_LINEAR,
                 cv::BORDER_REFLECT, warped);
    images_warped16.push_back(warped);
    images_warped16_umat.push_back(warped.getUMat(cv::ACCESS_READ));

    // Create and warp mask
    cv::Mat mask = cv::Mat::ones(sizes[i], CV_8U) * 255;
    cv::Mat warped_mask;
    warper->warp(mask, K, cameras[i].R, cv::INTER_NEAREST, cv::BORDER_CONSTANT,
                 warped_mask);
    masks_warped.push_back(warped_mask);
    masks_warped_umat.push_back(warped_mask.getUMat(cv::ACCESS_READ));
  }

  // PHASE 4: Exposure Compensation (optional)
  if (compensateExposure) {
    LogManager::instance()->log(
        "[ Panorama.cpp ] - Phase 4: Exposure compensation", DEBUG);

    cv::Ptr<cv::detail::ExposureCompensator> compensator =
        cv::makePtr<cv::detail::GainCompensator>();
    compensator->feed(corners, images_warped16_umat, masks_warped_umat);
    for (size_t i = 0; i < images_warped16.size(); ++i) {
      compensator->apply(static_cast<int>(i), corners[i], images_warped16[i],
                         masks_warped[i]);
    }
  } else {
    LogManager::instance()->log(
        "[ Panorama.cpp ] - Phase 4: Skipping exposure compensation", DEBUG);
  }

  // PHASE 5: Seam Finding (Graph-Cut)
  // GraphCutSeamFinder expects 8-bit UMat images and binary masks
  LogManager::instance()->log("[ Panorama.cpp ] - Phase 5: Seam finding",
                              DEBUG);

  // Ensure masks are binary (0 or 255)
  std::vector<cv::UMat> masks_binary;
  for (auto& mask : masks_warped) {
    cv::Mat mask_bin;
    cv::threshold(mask, mask_bin, 127, 255, cv::THRESH_BINARY);
    masks_binary.push_back(mask_bin.getUMat(cv::ACCESS_READ));
  }
  
  std::vector<cv::UMat> images_warped8_umat;
  for (const auto& img16 : images_warped16) {
    cv::Mat img8;
    img16.convertTo(img8, CV_8UC3, 255.0 / 65535.0);
    images_warped8_umat.push_back(img8.getUMat(cv::ACCESS_READ));
  }

  // Use a simpler seam finder that's more robust

  cv::Ptr<cv::detail::SeamFinder> seam_finder =
      cv::makePtr<cv::detail::VoronoiSeamFinder>();
  seam_finder->find(images_warped8_umat, corners, masks_binary);
  /*
  cv::Ptr<cv::detail::SeamFinder> seam_finder =
    cv::makePtr<cv::detail::GraphCutSeamFinder>(
        cv::detail::GraphCutSeamFinder::COST_COLOR);
  seam_finder->find(images_warped8_umat, corners, masks_binary);
*/
  // PHASE 6: Multi-band Blending (16-bit)
  LogManager::instance()->log("[ Panorama.cpp ] - Phase 6: Multi-band blending",
                              DEBUG);

  // Calculate final panorama size
  cv::Rect dst_roi = cv::detail::resultRoi(corners, sizes_warped);

  // Create multi-band blender with high number of bands for quality
  // Blend width is typically based on image size - use 1/8 of the smaller
  // dimension
  int blend_width = std::min(dst_roi.width, dst_roi.height) / 8;
  int num_bands = static_cast<int>(
      std::ceil(std::log(static_cast<double>(blend_width)) / std::log(2.0)));
  num_bands = std::min(num_bands, 8);  // Cap at 8 for performance

  LogManager::instance()->log(
      QString("[ Panorama.cpp ] - Using %1 bands for blending").arg(num_bands),
      DEBUG);

  // Create blender - use default CV_32F weight type
  cv::Ptr<cv::detail::Blender> blender =
      cv::makePtr<cv::detail::MultiBandBlender>(false, num_bands);
  blender->prepare(corners, sizes_warped);


  float scale_factor = 8.0;
// 1. Feed images to blender using Scaled 16-bit Signed
  for (size_t i = 0; i < images_warped16.size(); i++) {
      cv::Mat img16S;
      // We multiply by 1/scale_factor to allow the blend to sum up the highlights without clipping
      images_warped16[i].convertTo(img16S, CV_16SC3, 1/scale_factor);
      blender->feed(img16S, masks_warped[i], corners[i]);
  }

  // 2. Blend
  cv::Mat result_16s, result_mask;
  blender->blend(result_16s, result_mask);

  // 3. Convert back to 16-bit Unsigned
  cv::Mat result16;
  // Now we multiply again bythe scale_factor to restore the original data. 
  // The more you scale the more you lose data.
  result_16s.convertTo(result16, CV_16UC3, scale_factor);

  // Blend
  /*
  cv::Mat result_mask;
  cv::Mat result8;
  blender->blend(result8, result_mask);
  */
  // Convert 8-bit gamma result back to 16-bit linear
  /*
  cv::Mat result32F, result16;
  result8.convertTo(result32F, CV_32FC3, 1.0 / 255.0);
  cv::pow(result32F, 2.2, result32F);
  result32F.convertTo(result16, CV_16UC3, 65535.0);
  */
  cv::Mat resultRGB;
  cv::cvtColor(result16, resultRGB, cv::COLOR_BGR2RGB);

  // Ensure continuous memory layout for TIFF writing
  if (!resultRGB.isContinuous()) resultRGB = resultRGB.clone();

  if (resultRGB.empty()) {
    LogManager::instance()->log("[ Panorama.cpp ] - Blending failed", ERROR);
    result["message"] = QString("Stitching failed during blending!");
    result["success"] = false;
    return result;
  }

  LogManager::instance()->log(
      QString("[ Panorama.cpp ] - Blended result: %1x%2")
          .arg(result16.cols)
          .arg(result16.rows),
      DEBUG);

  // Ensure continuous memory layout for TIFF writing
  // if (!result16.isContinuous()) result16 = result16.clone();

  // PHASE 7: Save to DNG
  LogManager::instance()->log("[ Panorama.cpp ] - Phase 7: Saving to DNG",
                              DEBUG);

  QString filename =
      QDir::toNativeSeparators(QString("%1/%2.pano.dng")
                                   .arg(QFileInfo(inputFiles[0]).absolutePath(),
                                        QFileInfo(inputFiles[0]).baseName()));

  LogManager::instance()->log(
      QString("[ Panorama.cpp ] - Saving panorama to %1").arg(filename), INFO);
  /*
  libdng_init();
  libdng_info dng = {0};
  libdng_new(&dng);
  if (!libdng_set_mode_from_name(&dng, "SRGGB16")) {
    fprintf(stderr, "Invalid pixel format supplied\n");
  }

  for (size_t i = 0; i < 9; i++)
    dng.color_matrix_1[i] = colorInfo.get()->matrix[i];


  for (size_t i = 0; i < 3; i++)
    dng.analogbalance[i] = colorInfo->asShotNeutral[i];

  libdng_set_make_model(&dng, "Photon", "Panorama");
  

  if (
    !libdng_write(
      &dng,
      filename.toStdString().c_str(),
      result16.cols, result16.rows,
      reinterpret_cast<uint8_t*>(result16.data),
      result16.total() * result16.elemSize()
    )
  ) {
    result["success"] = false;
    result["message"] = "Error creating DNG file.";

    libdng_free(&dng);
    return result;
  }

  libdng_free(&dng);
  */

  TIFF* out = TIFFOpen(filename.toStdString().c_str(), "w");
  if (!out) {
    result["success"] = false;
    result["message"] = "Could not open file for writing.";
    return result;
  }

  TIFFSetField(out, TIFFTAG_IMAGEWIDTH, resultRGB.cols);
  TIFFSetField(out, TIFFTAG_IMAGELENGTH, resultRGB.rows);
  TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, 3);
  TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, 16);
  TIFFSetField(out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(out, TIFFTAG_PHOTOMETRIC, 34892);
  TIFFSetField(out, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);

  static const uint8_t dng_ver[] = {1, 4, 0, 0};
  TIFFSetField(out, TIFFTAG_DNGVERSION, dng_ver);
  TIFFSetField(out, TIFFTAG_DNGBACKWARDVERSION, dng_ver);
  TIFFSetField(out, TIFFTAG_SUBFILETYPE, 0);
  TIFFSetField(out, TIFFTAG_MAKE, "Photon");
  TIFFSetField(out, TIFFTAG_MODEL, "Panorama Engine");
  TIFFSetField(out, TIFFTAG_UNIQUECAMERAMODEL, "Photon Panorama Engine");
  TIFFSetField(out, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(out, 0));

  uint32_t whiteLevel[3] = {65535, 65535, 65535};
  TIFFSetField(out, TIFFTAG_WHITELEVEL, 3, whiteLevel);

  TIFFSetField(out, TIFFTAG_COLORMATRIX1, 9, colorInfo.get()->matrix);
  TIFFSetField(out, TIFFTAG_ASSHOTNEUTRAL, 3, colorInfo.get()->asShotNeutral);
  TIFFSetField(out, TIFFTAG_CALIBRATIONILLUMINANT1, 23);

  // Write the 16-bit data
  for (int row = 0; row < resultRGB.rows; row++) {
    uint16_t* rowPtr = resultRGB.ptr<uint16_t>(row);
    if (TIFFWriteScanline(out, rowPtr, row, 0) < 0) {
      TIFFClose(out);
      result["success"] = false;
      result["message"] = "Error writing scanline to DNG.";
      return result;
    }
  }

  TIFFClose(out);
  

  LogManager::instance()->log(
      "[ Panorama.cpp ] - Panorama stitching completed successfully", INFO);

  result["success"] = true;
  result["message"] = "Success! DNG saved to " + filename;
  result["filename"] = filename;
  result["width"] = resultRGB.cols;
  result["height"] = resultRGB.rows;
  return result;
}
