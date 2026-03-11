#pragma once

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QObject>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>

namespace photon {

enum LogLevel {
  DEBUG,
  INFO,
  WARNING,
  ERROR
};

class LogManager : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString logLocation READ logLocation WRITE setLogLocation NOTIFY
                 logLocationChanged)
  Q_PROPERTY(QString logLevel READ logLevel WRITE setLogLevel NOTIFY
                 logLevelChanged)

 public:
  explicit LogManager(QObject* parent = nullptr);
  ~LogManager() override;

  static LogManager* instance();

  QString logLocation() const { return m_logLocation; }
  void setLogLocation(const QString& location);

  int logLevel() const { return m_logLevel; }
  void setLogLevel(int level);
  void setLogLevel(const QString& level);

  Q_INVOKABLE void log(const QString& message, int level = INFO);
  Q_INVOKABLE void clearLog();

 signals:
  void logLocationChanged();
  void logLevelChanged();

 private:
  static LogManager* s_instance;
  QString m_logLocation;
  int m_logLevel = INFO;
  QFile m_logFile;
  QMutex m_logMutex;


  void openLogFile();
};

}  // namespace photon
