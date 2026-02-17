#pragma once

#include <QDir>
#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <QThreadPool>

class ThumbnailProvider : public QObject {
  Q_OBJECT

 public:
  explicit ThumbnailProvider(QObject* parent = nullptr);
  ~ThumbnailProvider() override;

  Q_INVOKABLE QImage getThumbnail(const QString& imagePath);
  Q_INVOKABLE QString getThumbnailPath(const QString& imagePath);
  Q_INVOKABLE void generateThumbnailAsync(const QString& imagePath);
  Q_INVOKABLE bool isThumbnailCached(const QString& imagePath);

 signals:
  void thumbnailReady(const QString& imagePath, const QImage& thumbnail);
  void thumbnailGenerationFailed(const QString& imagePath,
                                 const QString& error);

 private:
  QString getCacheDirectory() const;
  QString getThumbnailCachePath(const QString& imagePath) const;
  bool saveThumbnailToCache(const QString& imagePath,
                            const QImage& thumbnail) const;
  QImage loadThumbnailFromCache(const QString& imagePath) const;
  QImage generateThumbnail(const QString& imagePath) const;

  QHash<QString, QImage> m_thumbnailCache;
  QThreadPool* m_threadPool;
};