#include "HdrMerge.h"
#include "HdrMergeMath.h"
#include "ImageDecoder.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <numeric>

#include <libraw/libraw.h>
#include <tiffio.h>

#include <opencv2/calib3d.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>

#include "../managers/LogManager.h"

namespace {

constexpr float kClipDefault = 0.99f;
constexpr int kAlignWorkEdge = 1200;
constexpr int kDeghostWorkEdge = 1024;

float smoothstepf(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / std::max(e1 - e0, 1e-6f), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

cv::Mat makeWorkImage(const cv::Mat& rgb) {
  cv::Mat gray;
  cv::cvtColor(rgb, gray, cv::COLOR_RGB2GRAY);
  const float scale =
      float(kAlignWorkEdge) / float(std::max(gray.cols, gray.rows));
  if (scale < 1.0f) {
    cv::resize(gray, gray, cv::Size(), scale, scale, cv::INTER_AREA);
  }
  gray.convertTo(gray, CV_8U, 255.0, 0.0);
  return gray;
}

// One channel of the radiometric merge. `masks[i]` (optional, full-res floats)
// multiplies the weight of frame i (1 = keep, 0 = ghosted).
std::vector<float> mergeChannel(const std::vector<cv::Mat>& frames,
                                const std::vector<float>& gains,
                                const std::vector<cv::Mat>& masks, int channel,
                                float clip,
                                const std::vector<float>& fallback) {
  // Defensive: never index past any of the buffers involved.
  size_t pixels = fallback.size();
  for (const cv::Mat& frame : frames) {
    if (channel < frame.channels())
      pixels = std::min(pixels, size_t(frame.total()));
  }
  for (const cv::Mat& mask : masks) {
    if (!mask.empty()) pixels = std::min(pixels, size_t(mask.total()));
  }
  std::vector<float> acc(pixels, 0.0f);
  std::vector<float> weightSum(pixels, 0.0f);
  const float s0 = 0.80f * clip;
  const float s1 = 0.96f * clip;

  for (size_t i = 0; i < frames.size(); ++i) {
    cv::Mat plane;
    cv::extractChannel(frames[i], plane, channel);
    const float* data = plane.ptr<float>(0);
    const float gain = gains[i];
    const float weightScale = 1.0f / std::max(gain, 1e-6f);
    const float* mask = masks.empty() ? nullptr : masks[i].ptr<float>(0);
    for (size_t p = 0; p < pixels; ++p) {
      const float v = data[p];
      float w = weightScale * (1.0f - smoothstepf(s0, s1, v));
      if (i == 0) w += photon::hdrmath::kWeightFloor;
      if (mask) w *= (1.0f - std::clamp(mask[p], 0.0f, 1.0f));
      if (w <= 0.0f) continue;
      acc[p] += w * v * gain;
      weightSum[p] += w;
    }
  }

  std::vector<float> out = fallback;
  for (size_t p = 0; p < pixels; ++p) {
    if (weightSum[p] > 0.0f) out[p] = acc[p] / weightSum[p];
  }
  return out;
}

}  // namespace

HdrMerge::HdrMerge(QObject* parent) : QObject(parent) {}

bool HdrMerge::loadLinearFrame(const QString& path, Frame& frame) {
  if (photon::ImageDecoder::isBitmap(path)) {
    photon::BitmapImage decoded = photon::ImageDecoder::decode(path);
    if (decoded.pixels.empty() || decoded.width <= 0 || decoded.height <= 0)
      return false;
    // 16-bit sRGB -> linear (same LUT approach as the panorama loader).
    static std::vector<uint16_t> linearLut;
    if (linearLut.empty()) {
      linearLut.resize(65536);
      for (int i = 0; i < 65536; ++i) {
        const float c = float(i) / 65535.0f;
        const float linear = (c <= 0.04045f)
                                 ? (c / 12.92f)
                                 : std::pow((c + 0.055f) / 1.055f, 2.4f);
        linearLut[i] = uint16_t(std::clamp(linear, 0.0f, 1.0f) * 65535.0f + 0.5f);
      }
    }
    cv::Mat rgb16(decoded.height, decoded.width, CV_16UC3);
    for (int y = 0; y < decoded.height; ++y) {
      uint16_t* row = rgb16.ptr<uint16_t>(y);
      for (int x = 0; x < decoded.width; ++x) {
        const size_t base =
            (size_t(y) * size_t(decoded.width) + size_t(x)) * 3;
        row[x * 3 + 0] = linearLut[decoded.pixels[base + 0]];
        row[x * 3 + 1] = linearLut[decoded.pixels[base + 1]];
        row[x * 3 + 2] = linearLut[decoded.pixels[base + 2]];
      }
    }
    rgb16.convertTo(frame.rgb, CV_32FC3, 1.0 / 65535.0);
    frame.exifExposure = 0.0;  // measured gains will be used instead
    frame.path = path;
    return true;
  }

  LibRaw processor;
  processor.imgdata.params.output_bps = 16;
  processor.imgdata.params.no_auto_bright = 1;
  processor.imgdata.params.use_camera_wb = 1;
  processor.imgdata.params.output_color = 0;
  processor.imgdata.params.use_camera_matrix = 1;

  if (processor.open_file(path.toLocal8Bit().constData()) != LIBRAW_SUCCESS)
    return false;
  if (processor.unpack() != LIBRAW_SUCCESS) return false;
  if (processor.dcraw_process() != LIBRAW_SUCCESS) return false;

  libraw_processed_image_t* image = processor.dcraw_make_mem_image();
  if (!image || image->type != LIBRAW_IMAGE_BITMAP) {
    if (image) LibRaw::dcraw_clear_mem(image);
    return false;
  }

  cv::Mat raw16(image->height, image->width, CV_16UC3, image->data);
  raw16.convertTo(frame.rgb, CV_32FC3, 1.0 / 65535.0);
  LibRaw::dcraw_clear_mem(image);

  // Convert camera RGB (WB already applied by dcraw_process) to linear sRGB so
  // merged frames and the written DNG use sRGB primaries (correct colours).
  {
    cv::Mat matrix(3, 3, CV_32F);
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 3; ++j) {
        matrix.at<float>(i, j) = float(processor.imgdata.color.rgb_cam[i][j]);
      }
    }
    cv::Mat srgb;
    cv::transform(frame.rgb, srgb, matrix);
    frame.rgb = srgb;
  }

  // Relative exposure from EXIF: t / N^2 * ISO (ISO 100 normalized).
  const float shutter = processor.imgdata.other.shutter;
  const float aperture = processor.imgdata.other.aperture;
  const float iso = std::max(processor.imgdata.other.iso_speed, 1.0f);
  if (shutter > 0.0f && aperture > 0.0f) {
    frame.exifExposure = double(shutter) * double(iso) /
                         (double(aperture) * double(aperture));
  } else {
    frame.exifExposure = 0.0;
  }

  frame.path = path;
  return true;
}

float HdrMerge::estimateGain(const cv::Mat& reference, const cv::Mat& frame) {
  std::vector<float> ratios;
  ratios.reserve(size_t(reference.rows) * size_t(reference.cols) / 4);
  for (int y = 0; y < reference.rows; y += 2) {
    const cv::Vec3f* refRow = reference.ptr<cv::Vec3f>(y);
    const cv::Vec3f* frameRow = frame.ptr<cv::Vec3f>(y);
    for (int x = 0; x < reference.cols; x += 2) {
      const float rv = refRow[x][1];
      const float fv = frameRow[x][1];
      if (rv < 0.02f || rv > 0.90f || fv < 0.02f || fv > 0.90f) continue;
      ratios.push_back(std::log(rv / std::max(fv, 1e-6f)));
    }
  }
  if (ratios.size() < 64) return 1.0f;
  const size_t mid = ratios.size() / 2;
  std::nth_element(ratios.begin(), ratios.begin() + mid, ratios.end());
  return std::exp(ratios[mid]);
}

bool HdrMerge::alignFrame(const cv::Mat& reference, cv::Mat& frame) {
  cv::Mat refWork = makeWorkImage(reference);
  cv::Mat frameWork = makeWorkImage(frame);
  if (refWork.empty() || frameWork.empty() || refWork.size() != frameWork.size())
    return false;

  auto sift = cv::SIFT::create(1500);
  std::vector<cv::KeyPoint> kpRef, kpFrame;
  cv::Mat descRef, descFrame;
  sift->detectAndCompute(refWork, cv::noArray(), kpRef, descRef);
  sift->detectAndCompute(frameWork, cv::noArray(), kpFrame, descFrame);
  if (descRef.empty() || descFrame.empty()) return false;

  cv::BFMatcher matcher(cv::NORM_L2);
  std::vector<std::vector<cv::DMatch>> knn;
  matcher.knnMatch(descFrame, descRef, knn, 2);
  std::vector<cv::Point2f> src, dst;
  for (const auto& pair : knn) {
    if (pair.size() < 2) continue;
    if (pair[0].distance < 0.8f * pair[1].distance) {
      src.push_back(kpFrame[pair[0].queryIdx].pt);
      dst.push_back(kpRef[pair[0].trainIdx].pt);
    }
  }
  if (src.size() < 12) return false;

  cv::Mat inliers;
  const cv::Mat affine =
      cv::estimateAffinePartial2D(src, dst, inliers, cv::RANSAC, 3.0);
  if (affine.empty() || cv::countNonZero(inliers) < 10) return false;

  // Plausibility gate: corners must not move more than 12% of the diagonal.
  const float diag = std::hypot(float(refWork.cols), float(refWork.rows));
  const float limit = 0.12f * diag;
  const float corners[4][2] = {{0.0f, 0.0f},
                               {float(frameWork.cols), 0.0f},
                               {0.0f, float(frameWork.rows)},
                               {float(frameWork.cols), float(frameWork.rows)}};
  for (const auto& c : corners) {
    const float dx = float(affine.at<double>(0, 0) * c[0] +
                           affine.at<double>(0, 1) * c[1] +
                           affine.at<double>(0, 2)) -
                     c[0];
    const float dy = float(affine.at<double>(1, 0) * c[0] +
                           affine.at<double>(1, 1) * c[1] +
                           affine.at<double>(1, 2)) -
                     c[1];
    if (std::hypot(dx, dy) > limit) return false;
  }

  const double scale =
      double(reference.cols) / double(std::max(refWork.cols, 1));
  cv::Mat full = affine.clone();
  full.at<double>(0, 2) *= scale;
  full.at<double>(1, 2) *= scale;
  cv::Mat warped;
  cv::warpAffine(frame, warped, full, reference.size(), cv::INTER_LINEAR,
                 cv::BORDER_REPLICATE);
  frame = warped;
  return true;
}

QVariantMap HdrMerge::mergeSync(const QStringList& inputFiles,
                                const Options& options) {
  QVariantMap result;
  if (inputFiles.size() < 2) {
    result["success"] = false;
    result["message"] = "Need at least 2 photos for an HDR merge.";
    return result;
  }

  std::vector<Frame> frames(inputFiles.size());
  for (int i = 0; i < inputFiles.size(); ++i) {
    if (!loadLinearFrame(inputFiles[i], frames[i])) {
      result["success"] = false;
      result["message"] = QString("Cannot decode %1").arg(inputFiles[i]);
      return result;
    }
  }

  // Reference: middle frame by EXIF exposure (fallback: mean brightness).
  std::vector<int> order(frames.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    const double ea = std::max(frames[a].exifExposure, 1e-6);
    const double eb = std::max(frames[b].exifExposure, 1e-6);
    if (std::abs(std::log2(ea) - std::log2(eb)) > 0.1)
      return frames[a].exifExposure < frames[b].exifExposure;
    return cv::mean(frames[a].rgb)[1] < cv::mean(frames[b].rgb)[1];
  });
  const int refIndex = order[order.size() / 2];
  cv::Mat& reference = frames[refIndex].rgb;

  // Preview mode: shrink everything to maxEdge before aligning/merging.
  if (options.maxEdge > 0) {
    const double shrink =
        double(options.maxEdge) / double(std::max(reference.cols, reference.rows));
    if (shrink < 1.0) {
      const cv::Size target(std::max(2, int(std::round(reference.cols * shrink))),
                            std::max(2, int(std::round(reference.rows * shrink))));
      for (Frame& frame : frames)
        cv::resize(frame.rgb, frame.rgb, target, 0, 0, cv::INTER_AREA);
    }
  }

  for (size_t i = 0; i < frames.size(); ++i) {
    if (frames[i].rgb.size() != reference.size()) {
      cv::resize(frames[i].rgb, frames[i].rgb, reference.size(), 0, 0,
                 cv::INTER_AREA);
    }
    if (options.align && int(i) != refIndex) {
      if (!alignFrame(reference, frames[i].rgb)) {
        photon::LogManager::instance()->log(
            QString("[ HdrMerge.cpp ] - Alignment skipped for %1")
                .arg(frames[i].path),
            photon::PHOTON_WARNING);
      }
    }
  }

  // Gains towards the reference scale (EXIF, validated against measurement).
  std::vector<float> gains(frames.size(), 1.0f);
  for (size_t i = 0; i < frames.size(); ++i) {
    if (int(i) == refIndex) continue;
    float gain = 1.0f;
    const float measured = estimateGain(reference, frames[i].rgb);
    const double refExp = frames[refIndex].exifExposure;
    if (refExp > 0.0 && frames[i].exifExposure > 0.0 &&
        std::abs(std::log2(std::max(float(refExp / frames[i].exifExposure),
                                    1e-6f)) -
                 std::log2(std::max(measured, 1e-6f))) < 0.5f) {
      gain = float(refExp / frames[i].exifExposure);
    } else {
      gain = measured;
    }
    gains[i] = std::clamp(gain, 1.0f / 64.0f, 64.0f);
  }

  const float clip = kClipDefault;

  std::vector<cv::Mat> mats;
  mats.reserve(frames.size());
  for (const Frame& frame : frames) mats.push_back(frame.rgb);


  // Pass 1: no masks; used as the deghost prediction.
  std::vector<std::vector<float>> merged(3), plainMerged(3);
  for (int c = 0; c < 3; ++c) {
    cv::Mat refChannel;
    cv::extractChannel(reference, refChannel, c);
    const std::vector<float> fallback(
        refChannel.ptr<float>(0),
        refChannel.ptr<float>(0) + refChannel.total());
    plainMerged[c] = mergeChannel(mats, gains, {}, c, clip, fallback);
  }

  // Deghosting masks per frame (computed at reduced resolution).
  std::vector<cv::Mat> masks;
  if (options.deghost != DeghostNone && frames.size() > 1) {
    const float threshold = options.deghost == DeghostHigh     ? 0.35f
                            : options.deghost == DeghostMedium ? 0.6f
                                                               : 1.0f;
    const int radius = options.deghost == DeghostHigh     ? 5
                       : options.deghost == DeghostMedium ? 3
                                                          : 2;

    // Small prediction in BGR order.
    cv::Mat smallPred(reference.rows, reference.cols, CV_32FC3);
    for (int c = 0; c < 3; ++c) {
      cv::Mat channel(reference.rows, reference.cols, CV_32F,
                      plainMerged[2 - c].data());
      cv::insertChannel(channel, smallPred, c);
    }
    const float ds =
        float(kDeghostWorkEdge) / float(std::max(smallPred.cols, smallPred.rows));
    if (ds < 1.0f) cv::resize(smallPred, smallPred, cv::Size(), ds, ds, cv::INTER_AREA);

    masks.resize(frames.size());
    for (size_t i = 0; i < frames.size(); ++i) {
      masks[i] = cv::Mat(smallPred.size(), CV_32F, cv::Scalar(0.0f));
      if (int(i) == refIndex) {
        // Zero mask, but it must still match the full-resolution frame size.
        cv::Mat fullMask;
        cv::resize(masks[i], fullMask, reference.size(), 0, 0, cv::INTER_LINEAR);
        masks[i] = fullMask;
        continue;
      }
      cv::Mat smallFrame;
      cv::resize(frames[i].rgb, smallFrame, smallPred.size(), 0, 0,
                 cv::INTER_AREA);
      for (int y = 0; y < smallPred.rows; ++y) {
        const cv::Vec3f* mergedRow = smallPred.ptr<cv::Vec3f>(y);
        const cv::Vec3f* frameRow = smallFrame.ptr<cv::Vec3f>(y);
        float* maskRow = masks[i].ptr<float>(y);
        for (int x = 0; x < smallPred.cols; ++x) {
          const float frameLuma =
              (frameRow[x][0] + frameRow[x][1] + frameRow[x][2]) / 3.0f;
          if (frameLuma < 0.02f || frameLuma > 0.90f) continue;
          const float pred =
              (mergedRow[x][0] + mergedRow[x][1] + mergedRow[x][2]) / 3.0f;
          const float ratio = std::abs(
              std::log2(std::max(frameLuma * gains[i], 1e-6f) /
                        std::max(pred, 1e-6f)));
          maskRow[x] = smoothstepf(threshold * 0.7f, threshold, ratio);
        }
      }
      cv::Mat dilated;
      cv::dilate(masks[i], dilated,
                 cv::getStructuringElement(cv::MORPH_RECT,
                                           {2 * radius + 1, 2 * radius + 1}));
      cv::Mat blurred;
      cv::GaussianBlur(dilated, blurred, cv::Size(), radius * 0.5);
      cv::Mat fullMask;
      cv::resize(blurred, fullMask, reference.size(), 0, 0, cv::INTER_LINEAR);
      masks[i] = fullMask;
    }
  }

  // Pass 2: merge with the ghost masks.
  for (int c = 0; c < 3; ++c) {
    cv::Mat refChannel;
    cv::extractChannel(reference, refChannel, c);
    const std::vector<float> fallback(
        refChannel.ptr<float>(0),
        refChannel.ptr<float>(0) + refChannel.total());
    merged[c] = mergeChannel(mats, gains, masks, c, clip, fallback);
  }

  // Peak normalization -> 16-bit with BaselineExposure.
  float maxValue = 0.0f;
  for (int c = 0; c < 3; ++c) {
    for (const float v : merged[c]) maxValue = std::max(maxValue, v);
  }
  if (maxValue <= 0.0f) maxValue = 1.0f;
  const double scale = 0.95 / double(maxValue);
  const double baselineExposure = std::log2(1.0 / scale);

  cv::Mat radiance(reference.rows, reference.cols, CV_16UC3);
  for (int c = 0; c < 3; ++c) {
    cv::Mat channel(reference.rows, reference.cols, CV_32F, merged[c].data());
    cv::Mat scaled;
    channel.convertTo(scaled, CV_32F, float(scale));
    cv::Mat channel16;
    scaled.convertTo(channel16, CV_16U, 65535.0);
    cv::insertChannel(channel16, radiance, c);
  }

  if (options.previewOnly) {
    // Gamma-encoded PNG for the dialog preview (peak at white).
    cv::Mat floatRGB;
    radiance.convertTo(floatRGB, CV_32FC3, 1.0 / 65535.0);
    cv::pow(floatRGB, 1.0 / 2.2, floatRGB);
    cv::Mat preview8;
    floatRGB.convertTo(preview8, CV_8UC3, 255.0);
    QImage previewImage(preview8.data, preview8.cols, preview8.rows,
                        int(preview8.step), QImage::Format_RGB888);
    previewImage = previewImage.copy();
    const QString path = options.previewPath.isEmpty()
                             ? QDir::tempPath() + "/photon-hdr-preview.png"
                             : options.previewPath;
    if (!previewImage.save(path, "PNG")) {
      result["success"] = false;
      result["message"] = QString("Could not write preview to %1").arg(path);
      return result;
    }
    result["success"] = true;
    result["message"] = QString();
    result["filename"] = path;
    result["width"] = radiance.cols;
    result["height"] = radiance.rows;
    return result;
  }

  QString filename =
      QDir::toNativeSeparators(QString("%1/%2.hdr.dng")
                                   .arg(QFileInfo(inputFiles[0]).absolutePath(),
                                        QFileInfo(inputFiles[0]).baseName()));
  // Write to a temporary name first so auto-scans never see a half-written
  // file (which would poison thumbnail/preview caches), then rename.
  const QString tempName = filename + QStringLiteral(".part");
  QString error =
      writeDng(tempName, radiance, frames[refIndex].colorMatrix,
               frames[refIndex].asShotNeutral, baselineExposure);
  if (error.isEmpty()) {
    if (QFile::exists(filename)) QFile::remove(filename);
    if (!QFile::rename(tempName, filename)) {
      error = QString("Could not finalize %1").arg(filename);
      QFile::remove(tempName);
    }
  }
  if (!error.isEmpty()) {
    result["success"] = false;
    result["message"] = error;
    return result;
  }

  photon::LogManager::instance()->log(
      QString("[ HdrMerge.cpp ] - HDR merge written to %1 (%2 frames, baseline "
              "%3 EV)")
          .arg(filename)
          .arg(frames.size())
          .arg(baselineExposure, 0, 'f', 2),
      photon::PHOTON_INFO);

  result["success"] = true;
  result["message"] = "HDR merge saved to " + filename;
  result["filename"] = filename;
  result["width"] = radiance.cols;
  result["height"] = radiance.rows;
  return result;
}

QString HdrMerge::writeDng(const QString& outputPath, const cv::Mat& radiance,
                           const double* colorMatrix,
                           const float* asShotNeutral,
                           double baselineExposure) {
  const int maxDim = 256;
  const double thumbScale =
      double(maxDim) / std::max(radiance.cols, radiance.rows);
  cv::Mat thumb16;
  cv::resize(radiance, thumb16, cv::Size(), thumbScale, thumbScale,
             cv::INTER_AREA);
  cv::Mat thumbFloat;
  thumb16.convertTo(thumbFloat, CV_32FC3, 1.0 / 65535.0);
  cv::pow(thumbFloat, 1.0 / 2.2, thumbFloat);
  cv::Mat thumb8;
  thumbFloat.convertTo(thumb8, CV_8UC3, 255.0);

  TIFF* out = TIFFOpen(outputPath.toLocal8Bit().constData(), "w");
  if (!out) return QString("Could not open %1 for writing.").arg(outputPath);

  TIFFSetField(out, TIFFTAG_MAKE, "Photon");
  TIFFSetField(out, TIFFTAG_MODEL, "HDR Engine");
  TIFFSetField(out, TIFFTAG_UNIQUECAMERAMODEL, "Photon HDR Engine");
  static const uint8_t dngVer[] = {1, 4, 0, 0};
  TIFFSetField(out, TIFFTAG_DNGVERSION, dngVer);
  TIFFSetField(out, TIFFTAG_DNGBACKWARDVERSION, dngVer);

  TIFFSetField(out, TIFFTAG_SUBFILETYPE, FILETYPE_REDUCEDIMAGE);
  TIFFSetField(out, TIFFTAG_IMAGEWIDTH, thumb8.cols);
  TIFFSetField(out, TIFFTAG_IMAGELENGTH, thumb8.rows);
  TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, 8);
  TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, 3);
  TIFFSetField(out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  // Uncompressed thumbnail: TIFF-JPEG keeps the DQT/DHT in the JPEGTables tag,
  // which thumbnail extractors do not merge back, producing undecodable JPEGs.
  TIFFSetField(out, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
  TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(out, TIFFTAG_ROWSPERSTRIP, thumb8.rows);
  for (int row = 0; row < thumb8.rows; ++row) {
    TIFFWriteScanline(out, thumb8.ptr<uint8_t>(row), row, 0);
  }
  TIFFWriteDirectory(out);

  TIFFSetField(out, TIFFTAG_SUBFILETYPE, 0);
  TIFFSetField(out, TIFFTAG_IMAGEWIDTH, radiance.cols);
  TIFFSetField(out, TIFFTAG_IMAGELENGTH, radiance.rows);
  TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, 3);
  TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, 16);
  TIFFSetField(out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(out, TIFFTAG_PHOTOMETRIC, 34892);  // LINEARRAW
  TIFFSetField(out, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
  TIFFSetField(out, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(out, 0));
  uint32_t whiteLevel[3] = {65535, 65535, 65535};
  TIFFSetField(out, TIFFTAG_WHITELEVEL, 3, whiteLevel);
  TIFFSetField(out, TIFFTAG_BASELINEEXPOSURE, float(baselineExposure));
  if (colorMatrix) {
    double matrix[9];
    std::copy(colorMatrix, colorMatrix + 9, matrix);
    TIFFSetField(out, TIFFTAG_COLORMATRIX1, 9, matrix);
  }
  if (asShotNeutral) {
    float neutral[3] = {asShotNeutral[0], asShotNeutral[1], asShotNeutral[2]};
    TIFFSetField(out, TIFFTAG_ASSHOTNEUTRAL, 3, neutral);
  }
  TIFFSetField(out, TIFFTAG_CALIBRATIONILLUMINANT1, 21);  // D65 (sRGB primaries)

  for (int row = 0; row < radiance.rows; ++row) {
    if (TIFFWriteScanline(out, const_cast<uint16_t*>(radiance.ptr<uint16_t>(row)),
                          row, 0) < 0) {
      TIFFClose(out);
      return QString("Error writing %1.").arg(outputPath);
    }
  }
  TIFFClose(out);
  return QString();
}

void HdrMerge::mergeAsync(const QStringList& inputFiles, bool align,
                          int deghost) {
  if (m_busy || m_previewBusy) return;
  Options options;
  options.align = align;
  options.deghost = std::clamp(deghost, int(DeghostNone), int(DeghostHigh));

  m_busy = true;
  emit busyChanged();

  QThread* thread = QThread::create([this, inputFiles, options]() {
    QVariantMap result;
    try {
      result = mergeSync(inputFiles, options);
    } catch (const std::exception& e) {
      result["success"] = false;
      result["message"] = QString("HDR merge failed: %1").arg(e.what());
    } catch (...) {
      result["success"] = false;
      result["message"] = QStringLiteral("HDR merge failed unexpectedly");
    }
    QMetaObject::invokeMethod(
        this,
        [this, result]() {
          m_busy = false;
          emit busyChanged();
          emit mergeCompleted(result);
        },
        Qt::QueuedConnection);
  });
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  thread->start();
}

void HdrMerge::previewAsync(const QStringList& inputFiles, bool align,
                            int deghost, int maxEdge) {
  if (m_previewBusy || m_busy) return;
  Options options;
  options.align = align;
  options.deghost = std::clamp(deghost, int(DeghostNone), int(DeghostHigh));
  options.maxEdge = std::max(256, maxEdge);
  options.previewOnly = true;
  options.previewPath =
      QDir::tempPath() + QStringLiteral("/photon-hdr-preview.png");

  m_previewBusy = true;
  emit previewBusyChanged();

  QThread* thread = QThread::create([this, inputFiles, options]() {
    QVariantMap result;
    try {
      result = mergeSync(inputFiles, options);
    } catch (const std::exception& e) {
      result["success"] = false;
      result["message"] = QString("Preview failed: %1").arg(e.what());
    } catch (...) {
      result["success"] = false;
      result["message"] = QStringLiteral("Preview failed unexpectedly");
    }
    QMetaObject::invokeMethod(
        this,
        [this, result]() {
          m_previewBusy = false;
          emit previewBusyChanged();
          emit previewReady(result);
        },
        Qt::QueuedConnection);
  });
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  thread->start();
}
