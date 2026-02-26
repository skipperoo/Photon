#include "PreviewManager.h"

#include <libraw/libraw.h>

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent>

#include "../engine/ImageDeveloper.h"
#include "AppStateManager.h"
#include "FileScanner.h"
#include "LogManager.h"

namespace photon {

PreviewManager* PreviewManager::s_instance = nullptr;

PreviewManager::PreviewManager(QObject* parent)
    : QObject(parent), m_threadPool(new QThreadPool(this)) {
  s_instance = this;
  m_threadPool->setMaxThreadCount(std::max(1, QThread::idealThreadCount() / 2));
}

PreviewManager::~PreviewManager() { cancelAll(); }

PreviewManager* PreviewManager::instance() { return s_instance; }

QString PreviewManager::getCachePath(const QString& rawPath) const {
  QFileInfo fileInfo(rawPath);
  QString folder = fileInfo.absolutePath();
  QString filename = fileInfo.fileName() + ".preview.jpg";
  return folder + "/.PhotonData/cache/previews/" + filename;
}

bool PreviewManager::isPreviewValid(const QString& rawPath) const {
  QString cachePath = getCachePath(rawPath);
  QFileInfo cacheInfo(cachePath);
  if (!cacheInfo.exists()) return false;

  QFileInfo rawInfo(rawPath);
  QString editsPath = rawInfo.absolutePath() + "/.PhotonData/edits/" +
                      rawInfo.fileName() + ".json";
  QFileInfo editsInfo(editsPath);

  // Valid if newer than RAW AND newer than sidecar
  if (cacheInfo.lastModified() < rawInfo.lastModified()) return false;
  if (editsInfo.exists() && cacheInfo.lastModified() < editsInfo.lastModified())
    return false;

  return true;
}

QString PreviewManager::getPreviewPath(const QString& rawPath) const {
  if (isPreviewValid(rawPath)) {
    return getCachePath(rawPath);
  }
  return QString();
}

void PreviewManager::startFolderScan(const QString& folderPath) {
  if (folderPath.isEmpty()) return;

  cancelAll();  // Cancel any existing scan before starting a new one

  {
    QMutexLocker locker(&m_mutex);
    m_abort = false;
  }

  FileScanner scanner;
  auto files = scanner.scanForRawFiles(folderPath);

  {
    QMutexLocker locker(&m_mutex);
    m_total = files.size();
    m_done = 0;
    m_isProcessing = true;
    emit isProcessingChanged();
    emit progressChanged();
  }

  for (const auto& fileVar : files) {
    QVariantMap fileMap = fileVar.toMap();
    QString path = fileMap["path"].toString();
    m_threadPool->start([this, path]() {
      {
        QMutexLocker locker(&m_mutex);
        if (m_abort) return;
      }

      if (!isPreviewValid(path)) {
        processItem(path);
      }

      QMetaObject::invokeMethod(this, [this]() {
        bool finished = false;
        {
          QMutexLocker locker(&m_mutex);
          m_done++;
          emit progressChanged();
          if (m_done >= m_total) {
            m_isProcessing = false;
            finished = true;
          }
        }
        if (finished) {
          emit isProcessingChanged();
        }
      });
    });
  }
}

void PreviewManager::refreshPreview(const QString& rawPath) {
  m_threadPool->start([this, rawPath]() { processItem(rawPath); });
}

void PreviewManager::cancelAll() {
  {
    QMutexLocker locker(&m_mutex);
    m_abort = true;
  }
  m_threadPool->clear();
  m_threadPool->waitForDone();
  {
    QMutexLocker locker(&m_mutex);
    m_isProcessing = false;
  }
  emit isProcessingChanged();
}

void PreviewManager::processItem(const QString& rawPath) {
  LogManager::instance()->log(QString("[ PreviewManager ] - processItem START: %1").arg(rawPath), "DEBUG");

  {
    QMutexLocker locker(&m_mutex);
    if (m_abort) {
      LogManager::instance()->log(QString("[ PreviewManager ] - processItem ABORTED: %1").arg(rawPath), "DEBUG");
      return;
    }
  }

  QFileInfo fileInfo(rawPath);
  QString cachePath = getCachePath(rawPath);

  // 1. Load Sidecar Edits
  QString editsPath = fileInfo.absolutePath() + "/.PhotonData/edits/" +
                      fileInfo.fileName() + ".json";
  QJsonObject lastState;
  if (QFile::exists(editsPath)) {
    LogManager::instance()->log(QString("[ PreviewManager ] - Loading sidecar: %1").arg(editsPath), "DEBUG");
    QFile file(editsPath);
    if (file.open(QIODevice::ReadOnly)) {
      QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
      QJsonArray arr = doc.array();
      if (!arr.isEmpty()) {
        lastState = arr.last().toObject();
      }
    }
  }

  // 2. Load RAW via LibRaw (Fast mode)
  LogManager::instance()->log(QString("[ PreviewManager ] - Opening RAW file: %1").arg(rawPath), "DEBUG");
  LibRaw processor;
  processor.imgdata.params.output_bps = 16;
  processor.imgdata.params.use_camera_wb = 1;
  processor.imgdata.params.no_auto_bright = 1;
  processor.imgdata.params.half_size = 1;  // 1080p is enough, half_size is fast

  if (processor.open_file(rawPath.toLocal8Bit().data()) == LIBRAW_SUCCESS) {
    LogManager::instance()->log(QString("[ PreviewManager ] - Unpacking RAW: %1").arg(rawPath));
    if (processor.unpack() == LIBRAW_SUCCESS) {
      LogManager::instance()->log(QString("[ PreviewManager ] - Processing RAW: %1").arg(rawPath));
      if (processor.dcraw_process() == LIBRAW_SUCCESS) {
        int ret = 0;
        libraw_processed_image_t* mem = processor.dcraw_make_mem_image(&ret);
        if (mem && mem->type == LIBRAW_IMAGE_BITMAP) {
          LogManager::instance()->log(QString("[ PreviewManager ] - Developing image: %1 (%2x%3)")
                  .arg(rawPath).arg(mem->width).arg(mem->height));
          // 3. Develop Image with Edits
          lastState["denoiseSecondPass"] =
              ::AppStateManager::instance()->previewDenoiseFull();
          QImage result = ImageDeveloper::develop(
              reinterpret_cast<const ushort*>(mem->data), mem->width,
              mem->height, lastState, m_rhi, m_window);

          LogManager::instance()->log(QString("[ PreviewManager ] - Develop complete, result null: %1")
                  .arg(result.isNull()));
          if (!result.isNull()) {
            // Scale to 1080p if larger
            if (result.width() > 1920 || result.height() > 1080) {
              LogManager::instance()->log(QString("[ PreviewManager ] - Scaling down from %1x%2")
                      .arg(result.width()).arg(result.height()));
              result = result.scaled(1920, 1080, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
            }

            // 4. Save to Disk
            QDir().mkpath(QFileInfo(cachePath).absolutePath());
            LogManager::instance()->log(QString("[ PreviewManager ] - Saving to: %1")
                    .arg(cachePath));
            if (result.save(cachePath, "JPG", 90)) {
              LogManager::instance()->log("[ PreviewManager ] - Save successful, emitting signal");
              QMetaObject::invokeMethod(this, [this, rawPath, cachePath]() {
                emit previewReady(rawPath, cachePath);
              });
            }
          }
          LibRaw::dcraw_clear_mem(mem);
        }
      }
    }
  }
  LogManager::instance()->log(QString("[ PreviewManager ] - processItem END: %1").arg(rawPath), "DEBUG");
}

}  // namespace photon
