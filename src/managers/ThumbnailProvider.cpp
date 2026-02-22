#include "ThumbnailProvider.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QFileInfo>
#include <QImageWriter>
#include <QStandardPaths>
#include <QtConcurrent>

#include "RawEngine.h"

ThumbnailProvider::ThumbnailProvider(QObject* parent)
    : QObject(parent), m_threadPool(new QThreadPool(this)) {
  // Set maximum thread count to limit resource usage
  m_threadPool->setMaxThreadCount(4);
}

ThumbnailProvider::~ThumbnailProvider() { m_threadPool->waitForDone(); }

QString ThumbnailProvider::getCacheDirectory() const {
  // This is a dummy implementation, as getCacheDirectory should probably take
  // the image path
  return QString();
}

QString ThumbnailProvider::getThumbnailCachePath(
    const QString& imagePath) const {
  QFileInfo fileInfo(imagePath);
  QString folder = fileInfo.absolutePath();
  QString photonDataPath = folder + "/.PhotonData/cache/thumbnails";

  // Generate a hash of the filename to use as thumbnail name
  // Using filename instead of full path because we are inside the folder's
  // .PhotonData
  QByteArray hash = QCryptographicHash::hash(fileInfo.fileName().toUtf8(),
                                             QCryptographicHash::Md5);
  QString filename = QString(hash.toHex()) + ".jpg";

  return photonDataPath + "/" + filename;
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
  return RawEngine::extractThumbnail(imagePath);
}

QImage ThumbnailProvider::getThumbnail(const QString& imagePath) {
  // First check if we have it in memory cache
  if (m_thumbnailCache.contains(imagePath)) {
    return m_thumbnailCache[imagePath];
  }

  // Then check if we have it in disk cache
  QImage cachedThumb = loadThumbnailFromCache(imagePath);
  if (!cachedThumb.isNull()) {
    m_thumbnailCache[imagePath] = cachedThumb;
    return cachedThumb;
  }

  // Generate thumbnail if not found in cache
  QImage thumbnail = generateThumbnail(imagePath);
  if (!thumbnail.isNull()) {
    // Save to cache for future use
    saveThumbnailToCache(imagePath, thumbnail);
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
  QFuture<void> future = QtConcurrent::run(m_threadPool, [this, imagePath]() {
    QImage thumbnail = generateThumbnail(imagePath);
    if (!thumbnail.isNull()) {
      saveThumbnailToCache(imagePath, thumbnail);

      // Emit signal on main thread
      QMetaObject::invokeMethod(this, "thumbnailReady", Qt::QueuedConnection,
                                Q_ARG(QString, imagePath),
                                Q_ARG(QImage, thumbnail));
    } else {
      QMetaObject::invokeMethod(this, "thumbnailGenerationFailed",
                                Qt::QueuedConnection, Q_ARG(QString, imagePath),
                                Q_ARG(QString, "Failed to generate thumbnail"));
    }
  });
}

bool ThumbnailProvider::isThumbnailCached(const QString& imagePath) {
  // Check memory cache
  if (m_thumbnailCache.contains(imagePath)) {
    return true;
  }

  // Check disk cache
  QString cachePath = getThumbnailCachePath(imagePath);
  return QFile::exists(cachePath);
}