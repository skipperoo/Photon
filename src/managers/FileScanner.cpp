#include "FileScanner.h"

#include <QDateTime>
#include <QString>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "LogManager.h"
#include "../engine/ImageDecoder.h"

FileScanner::FileScanner(QObject* parent) : QObject(parent) {
  // Initialize supported RAW and rendered (JPEG/TIFF) file extensions
  m_supportedExtensions = photon::ImageDecoder::rawExtensions();
  m_supportedExtensions += photon::ImageDecoder::bitmapExtensions();
}

QVariantList FileScanner::scanForRawFiles(const QString& folderPath) const {
  QVariantList rawFiles;

  QDir dir(folderPath);
  if (!dir.exists()) {
    photon::LogManager::instance()->log(
      QString("Folder %1 does not exists").arg(folderPath)
    );
    return rawFiles;
  }

  // Get all files in the directory
  QFileInfoList fileInfoList =
      dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                        QDir::Time);  // Sort by modification time

  for (const QFileInfo& fileInfo : fileInfoList) {
    if (isSupportedFile(fileInfo)) {
      QVariantMap fileMap;
      fileMap["path"] = fileInfo.absoluteFilePath();
      fileMap["name"] = fileInfo.fileName();
      fileMap["size"] = fileInfo.size();
      fileMap["modified"] = fileInfo.lastModified();
      fileMap["extension"] = fileInfo.suffix().toLower();

      // Ratings live in their own per-photo file; fall back to the legacy
      // rating stored in the edit sidecar's last entry (older builds).
      int rating = 0;
      const QString ratingPath = QDir::toNativeSeparators(
          fileInfo.absolutePath() + "/.PhotonData/ratings/" +
          fileInfo.fileName() + ".json");
      QFile ratingFile(ratingPath);
      if (ratingFile.open(QIODevice::ReadOnly)) {
        const QJsonObject obj =
            QJsonDocument::fromJson(ratingFile.readAll()).object();
        rating = obj.value("rating").toInt(0);
      } else {
        const QString sidecarPath = QDir::toNativeSeparators(
            fileInfo.absolutePath() + "/.PhotonData/edits/" +
            fileInfo.fileName() + ".json");
        QFile sidecarFile(sidecarPath);
        if (sidecarFile.open(QIODevice::ReadOnly)) {
          const QJsonArray arr =
              QJsonDocument::fromJson(sidecarFile.readAll()).array();
          if (!arr.isEmpty()) {
            const QJsonObject lastState = arr.last().toObject();
            if (lastState.contains("rating")) {
              rating = lastState["rating"].toInt();
            } else {
              // Old sidecars keep the rating in the current state, behind the
              // trailing {"__index": N} marker.
              const int index = lastState.value("__index").toInt(-1);
              if (index >= 0 && index < arr.size() - 1) {
                const QJsonObject state = arr[index].toObject();
                if (state.contains("rating"))
                  rating = state["rating"].toInt();
              }
            }
          }
        }
      }
      fileMap["rating"] = rating;

      rawFiles.append(fileMap);
    }
  }

  return rawFiles;
}

bool FileScanner::isSupportedFile(const QFileInfo& fileInfo) const {
  QString extension = fileInfo.suffix().toLower();
  return m_supportedExtensions.contains(extension);
}
