#include "ThumbnailImageProvider.h"

#include <QImage>
#include <QUrl>

ThumbnailImageProvider::ThumbnailImageProvider(ThumbnailProvider* provider)
    : QQuickImageProvider(QQuickImageProvider::Image), m_provider(provider) {}

QImage ThumbnailImageProvider::requestImage(const QString& id, QSize* size,
                                            const QSize& requestedSize) {
  // Decode URL if necessary. QML might send URL encoded path.
  QString imagePath = QUrl::fromPercentEncoding(id.toUtf8());

  QImage img = m_provider->getThumbnail(imagePath);

  if (size) {
    *size = img.size();
  }

  if (requestedSize.isValid()) {
    return img.scaled(requestedSize, Qt::KeepAspectRatio,
                      Qt::SmoothTransformation);
  }

  return img;
}
