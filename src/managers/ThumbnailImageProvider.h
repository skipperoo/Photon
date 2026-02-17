#pragma once

#include <QQuickImageProvider>

#include "ThumbnailProvider.h"

class ThumbnailImageProvider : public QQuickImageProvider {
 public:
  ThumbnailImageProvider(ThumbnailProvider* provider);

  QImage requestImage(const QString& id, QSize* size,
                      const QSize& requestedSize) override;

 private:
  ThumbnailProvider* m_provider;
};
