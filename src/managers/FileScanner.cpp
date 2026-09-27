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

      // Read rating from sidecar if it exists
      int rating = 0;
      QString sidecarPath = QDir::toNativeSeparators(
          fileInfo.absolutePath() + "/.PhotonData/edits/" +
          fileInfo.fileName() + ".json");
      QFile sidecarFile(sidecarPath);
      if (sidecarFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(sidecarFile.readAll());
        QJsonArray arr = doc.array();
        if (!arr.isEmpty()) {
          QJsonObject lastState = arr.last().toObject();
          if (lastState.contains("rating")) {
            rating = lastState["rating"].toInt();
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
