#include "ThumbnailProvider.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QThread>
#include <QtConcurrent>

#include <libraw/libraw.h>

#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <vector>

#include "CrashReporter.h"
#include "LogManager.h"
#include "RawEngine.h"
#include "../engine/AiDenoiser.h"
#include "../engine/ImageDecoder.h"
#include "../engine/ImageDeveloper.h"
#include "../engine/RawDecode.h"

namespace {
// A thumbnail rendered from the RAW itself, for files whose embedded preview
// is missing or cannot be decoded (e.g. DNGs with a TIFF-JPEG preview whose
// tables live in a separate tag, as Photon's panoramas had before
// 2026-10): a fast half-size decode, downscaled, developed with the default
// settings at the RAW base exposure (the photo as Develop shows it unedited).
QImage renderRawThumbnail(const QString& path) {
  constexpr int kMaxEdge = 720;  // twice the 360 px the cache stores
  LibRaw lr;
  photon::raw::configureDevelopDecode(lr);
  lr.imgdata.params.half_size = 1;
  if (lr.open_file(path.toLocal8Bit().constData()) != LIBRAW_SUCCESS ||
      lr.unpack() != LIBRAW_SUCCESS || lr.dcraw_process() != LIBRAW_SUCCESS)
    return QImage();
  int ret = 0;
  libraw_processed_image_t* mem = lr.dcraw_make_mem_image(&ret);
  if (!mem || mem->type != LIBRAW_IMAGE_BITMAP || mem->colors != 3 ||
      mem->bits != 16) {
    if (mem) LibRaw::dcraw_clear_mem(mem);
    return QImage();
  }
  const ushort* rgb = reinterpret_cast<const ushort*>(mem->data);
  int w = mem->width;
  int h = mem->height;
  QJsonObject settings;
  settings["baseExposure"] = photon::raw::baseExposure(rgb, w, h, 3);
  settings["outputBits"] = 8;
  std::vector<ushort> pixels;
  const double scale = double(kMaxEdge) / std::max(w, h);
  if (scale < 1.0) {
    const int sw = std::max(1, int(std::lround(w * scale)));
    const int sh = std::max(1, int(std::lround(h * scale)));
    pixels.resize(size_t(sw) * sh * 3);
    photon::ai::resampleRgb16(rgb, w, h, pixels.data(), sw, sh);
    w = sw;
    h = sh;
  } else {
    pixels.assign(rgb, rgb + size_t(w) * h * 3);
  }
  LibRaw::dcraw_clear_mem(mem);
  // LibRaw's develop output is already upright.
  return photon::ImageDeveloper::develop(pixels.data(), w, h, settings);
}
}  // namespace

ThumbnailProvider::ThumbnailProvider(QObject* parent)
    : QObject(parent), m_threadPool(new QThreadPool(this)) {
  // Set maximum thread count to limit resource usage
  m_threadPool->setMaxThreadCount(4);
  m_threadPool->setStackSize(4 * 1024 * 1024);
}

ThumbnailProvider::~ThumbnailProvider() { m_threadPool->waitForDone(); }

QString ThumbnailProvider::getCacheDirectory() const { return QString(); }

QString ThumbnailProvider::getThumbnailCachePath(
    const QString& imagePath) const {
  QFileInfo fileInfo(imagePath);
  QString folder = fileInfo.absolutePath();
  QString photonDataPath =
      QDir::toNativeSeparators(folder + "/.PhotonData/cache/thumbnails");

  // Generate a hash of the filename to use as thumbnail name
  QByteArray hash = QCryptographicHash::hash(fileInfo.fileName().toUtf8(),
                                             QCryptographicHash::Md5);
  QString filename = QString(hash.toHex()) + ".jpg";

  return QDir::toNativeSeparators(photonDataPath + "/" + filename);
}

bool ThumbnailProvider::saveThumbnailToCache(const QString& imagePath,
                                             const QImage& thumbnail) const {
  QString cachePath = getThumbnailCachePath(imagePath);
  QDir cacheDir(QFileInfo(cachePath).absolutePath());

  // Create cache directory if it doesn't exist
  if (!cacheDir.exists()) {
    if (!cacheDir.mkpath(".")) {
      qWarning() << "Failed to create thumbnail cache directory:"
                 << cacheDir.absolutePath();
      return false;
    }
  }

  // Scale thumbnail to 360px as per specification
  QImage scaledThumb =
      thumbnail.scaled(360, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation);

  QImageWriter writer(cachePath, "JPEG");
  writer.setQuality(85);

  return writer.write(scaledThumb);
}

QImage ThumbnailProvider::loadThumbnailFromCache(
    const QString& imagePath) const {
  QString cachePath = getThumbnailCachePath(imagePath);
  QFile cacheFile(cachePath);

  if (cacheFile.exists()) {
    QImage thumbnail(cachePath);
    if (!thumbnail.isNull()) {
      return thumbnail;
    }
  }

  return QImage();
}

QImage ThumbnailProvider::generateThumbnail(const QString& imagePath) const {
  photon::CrashReporter::setBreadcrumb(
      QString("thumbnail %1").arg(imagePath));
  if (photon::ImageDecoder::isBitmap(imagePath)) {
    return photon::ImageDecoder::extractThumbnail(imagePath);
  }
  QImage embedded = RawEngine::extractThumbnail(imagePath);
  if (!embedded.isNull()) return embedded;
  photon::LogManager::instance()->log(
      QString("[ ThumbnailProvider.cpp ] - no usable embedded preview in %1; "
              "rendering the thumbnail")
          .arg(imagePath),
      photon::PHOTON_DEBUG);
  return renderRawThumbnail(imagePath);
}

QImage ThumbnailProvider::getThumbnail(const QString& imagePath) {
  {
    QMutexLocker locker(&m_cacheMutex);
    // First check if we have it in memory cache
    if (m_thumbnailCache.contains(imagePath))
      return m_thumbnailCache[imagePath];
  }

  // Then check if we have it in disk cache
  QImage cachedThumb = loadThumbnailFromCache(imagePath);
  if (!cachedThumb.isNull()) {
    QMutexLocker locker(&m_cacheMutex);
    m_thumbnailCache[imagePath] = cachedThumb;
    return cachedThumb;
  }

  // Generate thumbnail if not found in cache
  QImage thumbnail = generateThumbnail(imagePath);
  if (!thumbnail.isNull()) {
    // Save to cache for future use
    saveThumbnailToCache(imagePath, thumbnail);
    QMutexLocker locker(&m_cacheMutex);
    m_thumbnailCache[imagePath] = thumbnail;
  }

  return thumbnail;
}

QString ThumbnailProvider::getThumbnailPath(const QString& imagePath) {
  return getThumbnailCachePath(imagePath);
}

void ThumbnailProvider::generateThumbnailAsync(const QString& imagePath) {
  // Check if already cached
  if (isThumbnailCached(imagePath)) {
    emit thumbnailReady(imagePath, getThumbnail(imagePath));
    return;
  }

  // Run thumbnail generation in a separate thread pool managed by this object
  m_threadPool->start([this, imagePath]() {
    QImage thumbnail = generateThumbnail(imagePath);
    if (!thumbnail.isNull()) {
      saveThumbnailToCache(imagePath, thumbnail);

      // Emit signal on main thread
      QMetaObject::invokeMethod(this, [this, imagePath, thumbnail]() {
        emit thumbnailReady(imagePath, thumbnail);
      });
    } else {
      QMetaObject::invokeMethod(this, [this, imagePath]() {
        emit thumbnailGenerationFailed(imagePath,
                                       "Failed to generate thumbnail");
      });
    }
  });
}

bool ThumbnailProvider::isThumbnailCached(const QString& imagePath) {
  // Check memory cache
  {
    QMutexLocker locker(&m_cacheMutex);
    if (m_thumbnailCache.contains(imagePath)) {
      return true;
    }
  }

  // Check disk cache
  QString cachePath = getThumbnailCachePath(imagePath);
  return QFile::exists(cachePath);
}
