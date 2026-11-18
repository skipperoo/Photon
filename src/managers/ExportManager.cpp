#include "../engine/RawDecode.h"
#include "ExportManager.h"
#include "EditStack.h"

#include "../engine/AiDenoiser.h"
#include "AppStateManager.h"

#include <libraw/libraw.h>

#include <QDebug>
#include <QDir>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QtConcurrent>

#include "../engine/CameraLook.h"
#include "../engine/EditVideo.h"
#include "../engine/VideoEncoder.h"
#include "EditVideoSource.h"
#include "../engine/ImageDeveloper.h"
#include "../engine/WhiteBalance.h"
#include "../engine/ImageDecoder.h"
#include "../engine/RawEngine.h"
#include "CrashReporter.h"
#include "LogManager.h"

namespace photon {

ExportManager::ExportManager(QObject* parent) : QObject(parent) {
  connect(&m_watcher, &QFutureWatcher<void>::finished, this, [this]() {
    m_isExporting = false;
    m_itemProgress = 0.0f;
    emit isExportingChanged();
    emit progressChanged();
    emit exportFinished(m_doneCount, m_totalCount - m_doneCount);
  });
}

namespace {
// The folder field holds a path, or a file:// URL when picked with the
// folder dialog: always write to the local path.
QString localFolder(const QString& folder) {
  return folder.startsWith(QLatin1String("file:")) ? QUrl(folder).toLocalFile()
                                                   : folder;
}
}  // namespace

void ExportManager::begin(int count) {
  m_isExporting = true;
  m_doneCount = 0;
  m_totalCount = count;
  m_itemProgress = 0.0f;
  m_abortExport = false;
  emit isExportingChanged();
  emit progressChanged();
}

ExportManager::~ExportManager() { cancelExport(); }

void ExportManager::startExport(const QStringList& paths,
                                const QString& outputFolder,
                                const QString& format, int quality,
                                int bitDepth, const QString& colorSpace) {
  if (m_isExporting) return;
  begin(int(paths.size()));
  const QString folder = localFolder(outputFolder);
  m_watcher.setFuture(QtConcurrent::run([this, paths, folder, format, quality,
                                         bitDepth, colorSpace]() {
    processExport(paths, folder, format, quality, bitDepth, colorSpace);
  }));
}

void ExportManager::startVideoExport(const QString& path,
                                     const QString& outputFolder,
                                     const VideoExportOptions& options) {
  if (m_isExporting) return;
  begin(1);
  const QString folder = localFolder(outputFolder);
  m_watcher.setFuture(QtConcurrent::run([this, path, folder, options]() {
    CrashReporter::setBreadcrumb(QString("video export %1").arg(path));
    QDir().mkpath(folder);
    const QString outPath = folder + "/" + QFileInfo(path).baseName() + ".mp4";
    // A failed or cancelled video counts as not exported.
    if (exportVideo(path, outPath, options)) m_doneCount++;
    m_itemProgress = 0.0f;
    emit progressChanged();
  }));
}

bool ExportManager::videoExportAvailable() const {
  return photon::video::isAvailable();
}

void ExportManager::cancelExport() {
  m_abortExport = true;
  if (m_watcher.isRunning()) {
    m_watcher.waitForFinished();
  }
}

bool loadDevelopSource(const QString& path, DevelopSource& out) {
  out = DevelopSource();
  const QFileInfo fileInfo(path);

  // 1. Sidecar edits.
  const QString editsPath = QDir::toNativeSeparators(
      fileInfo.absolutePath() + "/.PhotonData/edits/" + fileInfo.fileName() +
      ".json");
  QJsonObject state;
  if (QFile::exists(editsPath)) {
    QFile file(editsPath);
    if (file.open(QIODevice::ReadOnly)) {
      const QJsonArray arr = QJsonDocument::fromJson(file.readAll()).array();
      if (!arr.isEmpty()) state = photon::editstack::currentState(arr);
    }
  }

  // 1b. Camera look: the fitted tone curve, so renders match the preview when
  //     the photo's look amount is set.
  photon::cameralook::applyToSettings(path, state);

  // 2. Pixels: LibRaw for RAW, ImageDecoder for JPEG/TIFF.
  if (ImageDecoder::isBitmap(path)) {
    LogManager::instance()->log(
        QString("[ ExportManager.cpp ] - Decoding bitmap file: %1").arg(path),
        PHOTON_DEBUG);
    BitmapImage decoded = ImageDecoder::decode(path, true);
    if (decoded.pixels.empty()) return false;
    out.pixels = std::move(decoded.pixels);
    out.width = decoded.width;
    out.height = decoded.height;
    out.settings = state;
    return true;
  }

  LibRaw processor;
  photon::raw::configureDevelopDecode(processor);
  if (processor.open_file(path.toLocal8Bit().data()) != LIBRAW_SUCCESS)
    return false;
  // Transient EXIF fields for lens-profile matching during develop.
  state["exifMake"] =
      QString::fromLocal8Bit(processor.imgdata.idata.make).trimmed();
  state["exifModel"] =
      QString::fromLocal8Bit(processor.imgdata.idata.model).trimmed();
  state["exifLens"] = QString::fromUtf8(processor.imgdata.lens.Lens).trimmed();
  state["exifFocal"] = double(processor.imgdata.other.focal_len);
  state["exifAperture"] = double(processor.imgdata.other.aperture);
  if (processor.unpack() != LIBRAW_SUCCESS ||
      processor.dcraw_process() != LIBRAW_SUCCESS)
    return false;
  // Kelvin white balance: multipliers relative to the neutral anchor (the
  // as-shot rendering), so the render matches the preview. RAW renders use
  // the Kelvin path (the relative key is ignored).
  const float asShot = photon::whitebalance::kNeutralKelvin;
  const float targetKelvin =
      float(state.value("temperatureKelvin").toDouble(asShot));
  float wbGains[3] = {1.0f, 1.0f, 1.0f};
  photon::whitebalance::relativeGains(targetKelvin, asShot, wbGains);
  state["wbGainR"] = wbGains[0];
  state["wbGainG"] = wbGains[1];
  state["wbGainB"] = wbGains[2];
  state["temperature"] = 0.0;
  int ret = 0;
  libraw_processed_image_t* mem = processor.dcraw_make_mem_image(&ret);
  if (!mem || mem->type != LIBRAW_IMAGE_BITMAP || mem->colors != 3 ||
      mem->bits != 16) {
    if (mem) LibRaw::dcraw_clear_mem(mem);
    return false;
  }
  const ushort* rgb = reinterpret_cast<const ushort*>(mem->data);
  out.pixels.assign(rgb, rgb + size_t(mem->width) * mem->height * 3);
  out.width = mem->width;
  out.height = mem->height;
  out.raw = true;
  state["baseExposure"] =
      photon::raw::baseExposure(rgb, mem->width, mem->height, 3);
  LibRaw::dcraw_clear_mem(mem);
  out.settings = state;
  return true;
}

void ExportManager::processExport(const QStringList& paths,
                                  const QString& outputFolder,
                                  const QString& format, int quality,
                                  int bitDepth, const QString& colorSpace) {
  const bool tiff = format.toLower().startsWith("tif");
  QDir().mkpath(outputFolder);

  for (const QString& path : paths) {
    if (m_abortExport) break;

    CrashReporter::setBreadcrumb(QString("export %1").arg(path));
    const QFileInfo fileInfo(path);
    const QString outPath =
        outputFolder + "/" + fileInfo.baseName() + "." + format.toLower();

    QImage result;
    DevelopSource source;
    if (loadDevelopSource(path, source)) {
      source.settings["denoiseSecondPass"] = true;
      source.settings["outputBits"] = tiff && bitDepth == 16 ? 16 : 8;
      source.settings["outputColorSpace"] = colorSpace;
      if (source.raw) {
        // AI denoise: blend the cached/computed RawNIND product into the
        // develop source (falls back to Classic when the photo is not
        // supported).
        source.settings["denoiseMode"] =
            ::AppStateManager::instance()->denoiseEngine();
        photon::ai::applyToDevelopSource(path, source.pixels.data(),
                                         source.width, source.height,
                                         source.settings, true);
      }
      result = ImageDeveloper::develop(source.pixels.data(), source.width,
                                       source.height, source.settings, m_rhi,
                                       m_window);
    }

    if (!result.isNull()) {
      QImageWriter writer(outPath, format.toUtf8());
      if (format.toLower() == "jpg" || format.toLower() == "jpeg") {
        writer.setQuality(quality);
      }
      writer.write(result);
    }

    m_doneCount++;
    emit progressChanged();
  }
}

bool ExportManager::exportVideo(const QString& path, const QString& outPath,
                                const VideoExportOptions& options) {
  namespace ev = photon::editvideo;
  auto log = [](const QString& msg, LogLevel level = PHOTON_INFO) {
    LogManager::instance()->log("[ ExportManager.cpp ] - " + msg, level);
  };
  auto setProgress = [this](float f) {
    m_itemProgress = std::clamp(f, 0.0f, 0.999f);
    emit progressChanged();
  };

  EditVideoSource source;
  if (!source.load(path, 0, m_rhi, m_window)) return false;

  // Develop each planned step at full resolution, as the export would.
  const std::vector<ev::StepPlan> plan =
      ev::planSteps(options.shownGroups, options.tweaks);
  std::vector<ev::Step> steps;
  QSize canvas;
  QSize fitted;
  for (size_t i = 0; i < plan.size(); ++i) {
    if (m_abortExport) return false;
    const QImage developed = source.render(plan[i].groups);
    if (developed.isNull()) {
      log(QString("video: develop failed for %1").arg(path), PHOTON_WARNING);
      return false;
    }
    if (i == 0) {
      canvas = ev::canvasSize(ev::aspectFromString(options.aspect),
                              developed.size(), options.shortSide);
      fitted = developed.size().scaled(canvas, Qt::KeepAspectRatio);
    }
    steps.push_back({plan[i].name,
                     developed
                         .scaled(fitted, Qt::IgnoreAspectRatio,
                                 Qt::SmoothTransformation)
                         .convertToFormat(QImage::Format_RGB32)});
    setProgress(0.5f * float(i + 1) / float(plan.size()));
  }

  // Compose and encode.
  const ev::Composer composer(std::move(steps), canvas, ev::Timing(),
                              options.pill, options.showLine);
  photon::video::Encoder encoder;
  QString error;
  if (!encoder.open(outPath, canvas, composer.fps(), &error)) {
    log(QString("video: %1").arg(error), PHOTON_ERROR);
    return false;
  }
  const int frames = composer.frameCount();
  for (int i = 0; i < frames; ++i) {
    if (m_abortExport) {
      encoder.finish(nullptr);
      QFile::remove(outPath);
      return false;
    }
    if (!encoder.write(composer.frame(i), &error)) {
      log(QString("video: %1").arg(error), PHOTON_ERROR);
      encoder.finish(nullptr);
      QFile::remove(outPath);
      return false;
    }
    if (i % 10 == 0) setProgress(0.5f + 0.5f * float(i) / float(frames));
  }
  if (!encoder.finish(&error)) {
    log(QString("video: %1").arg(error), PHOTON_ERROR);
    return false;
  }
  log(QString("video: wrote %1 (%2 steps, %3 frames, %4x%5, %6)")
          .arg(outPath)
          .arg(int(plan.size()))
          .arg(frames)
          .arg(canvas.width())
          .arg(canvas.height())
          .arg(encoder.encoderName()));
  return true;
}

}  // namespace photon
