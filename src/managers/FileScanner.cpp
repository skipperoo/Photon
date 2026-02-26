#include "FileScanner.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

FileScanner::FileScanner(QObject* parent) : QObject(parent) {
  // Initialize supported RAW file extensions
  m_supportedExtensions << "arw" << "cr2" << "cr3" << "nef" << "dng"
                        << "orf" << "raf" << "rw2" << "pef" << "srw"
                        << "x3f" << "iiq" << " nrw" << "kdc" << "dcr";
}

QVariantList FileScanner::scanForRawFiles(const QString& folderPath) const {
  QVariantList rawFiles;

  QDir dir(folderPath);
  if (!dir.exists()) {
    qWarning() << "Folder does not exist:" << folderPath;
    return rawFiles;
  }

  // Get all files in the directory
  QFileInfoList fileInfoList =
      dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                        QDir::Time);  // Sort by modification time

  for (const QFileInfo& fileInfo : fileInfoList) {
    if (isRawFile(fileInfo)) {
      QVariantMap fileMap;
      fileMap["path"] = fileInfo.absoluteFilePath();
      fileMap["name"] = fileInfo.fileName();
      fileMap["size"] = fileInfo.size();
      fileMap["modified"] = fileInfo.lastModified();

      // Read rating from sidecar if it exists
      int rating = 0;
            QString sidecarPath = QDir::toNativeSeparators(fileInfo.absolutePath() + "/.PhotonData/edits/" +
                                fileInfo.fileName() + ".json");      QFile sidecarFile(sidecarPath);
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

bool FileScanner::isRawFile(const QFileInfo& fileInfo) const {
  QString extension = fileInfo.suffix().toLower();
  return m_supportedExtensions.contains(extension);
}