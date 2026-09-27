#include "ExportManager.h"

#include <libraw/libraw.h>

#include <QDebug>
#include <QDir>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent>

#include "../engine/ImageDeveloper.h"
#include "../engine/ImageDecoder.h"
#include "../engine/RawEngine.h"
#include "LogManager.h"

namespace photon {

ExportManager::ExportManager(QObject* parent) : QObject(parent) {}

ExportManager::~ExportManager() { cancelExport(); }

void ExportManager::startExport(const QStringList& paths,
                                const QString& outputFolder,
                                const QString& format, int quality) {
  if (m_isExporting) return;

  m_isExporting = true;
  m_doneCount = 0;
  m_totalCount = paths.size();
  m_abortExport = false;
  emit isExportingChanged();
  emit progressChanged();

  QFuture<void> future =
      QtConcurrent::run([this, paths, outputFolder, format, quality]() {
        processExport(paths, outputFolder, format, quality);
      });
  m_watcher.setFuture(future);

  connect(&m_watcher, &QFutureWatcher<void>::finished, this, [this]() {
    m_isExporting = false;
    emit isExportingChanged();
    emit exportFinished(m_doneCount, m_totalCount - m_doneCount);
  });
}

void ExportManager::cancelExport() {
  m_abortExport = true;
  if (m_watcher.isRunning()) {
    m_watcher.waitForFinished();
  }
}

void ExportManager::processExport(const QStringList& paths,
                                  const QString& outputFolder,
                                  const QString& format, int quality) {
  QDir().mkpath(outputFolder);

  for (const QString& path : paths) {
    if (m_abortExport) break;

    QFileInfo fileInfo(path);
    QString outPath =
        outputFolder + "/" + fileInfo.baseName() + "." + format.toLower();

    // 1. Load Sidecar Edits
    QString editsPath = QDir::toNativeSeparators(fileInfo.absolutePath() +
                                                 "/.PhotonData/edits/" +
                                                 fileInfo.fileName() + ".json");
    QJsonObject lastState;
    if (QFile::exists(editsPath)) {
      QFile file(editsPath);
      if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QJsonArray arr = doc.array();
        if (!arr.isEmpty()) {
          lastState = arr.last().toObject();
        }
      }
    }

    // 2. Load pixels: LibRaw for RAW, ImageDecoder for JPEG/TIFF
    QImage result;
    lastState["denoiseSecondPass"] = true;

    if (ImageDecoder::isBitmap(path)) {
      LogManager::instance()->log(
          QString("[ ExportManager ] - Decoding bitmap file: %1").arg(path),
          PHOTON_DEBUG);
      BitmapImage decoded = ImageDecoder::decode(path);
      if (!decoded.pixels.empty()) {
        result = ImageDeveloper::develop(decoded.pixels.data(), decoded.width,
                                         decoded.height, lastState, m_rhi,
                                         m_window);
      }
    } else {
      LibRaw processor;
      processor.imgdata.params.output_bps = 16;
      processor.imgdata.params.use_camera_wb = 1;
      processor.imgdata.params.no_auto_bright = 0;
      processor.imgdata.params.auto_bright_thr = 0.01;

      if (processor.open_file(path.toLocal8Bit().data()) == LIBRAW_SUCCESS) {
        if (processor.unpack() == LIBRAW_SUCCESS) {
          if (processor.dcraw_process() == LIBRAW_SUCCESS) {
            int ret = 0;
            libraw_processed_image_t* mem =
                processor.dcraw_make_mem_image(&ret);
            if (mem && mem->type == LIBRAW_IMAGE_BITMAP) {
              // 3. Develop Image with Edits
              result = ImageDeveloper::develop(
                  reinterpret_cast<const ushort*>(mem->data), mem->width,
                  mem->height, lastState, m_rhi, m_window);
            }
            if (mem) LibRaw::dcraw_clear_mem(mem);
          }
        }
      }
    }

    if (!result.isNull()) {
      // 4. Save to Disk
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

}  // namespace photon
