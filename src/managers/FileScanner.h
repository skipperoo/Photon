#pragma once

#include <QDateTime>
#include <QFileInfo>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class FileScanner : public QObject {
  Q_OBJECT

 public:
  explicit FileScanner(QObject* parent = nullptr);

  struct RawFile {
    QString path;
    QString name;
    qint64 size;
    QDateTime modified;
  };

  Q_INVOKABLE QVariantList scanForRawFiles(const QString& folderPath) const;

 private:
  bool isRawFile(const QFileInfo& fileInfo) const;
  QStringList m_supportedExtensions;
};