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
  PHOTON_DEBUG,
  PHOTON_INFO,
  PHOTON_WARNING,
  PHOTON_ERROR,
  PHOTON_FATAL
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


  LogLevel strLevelToEnum(QString level) const {
    if (level == "DEBUG")
      return PHOTON_DEBUG;
    else if (level == "INFO")
      return PHOTON_INFO;
    else if (level == "WARNING")
      return PHOTON_WARNING;
    else if (level == "ERROR")
      return PHOTON_ERROR;
    else if (level == "FATAL")
      return PHOTON_FATAL;

    return PHOTON_DEBUG;
  }

  QString enumLevelToStr(int level) const {
    switch(level) {
      case PHOTON_DEBUG:
        return "DEBUG";

      case PHOTON_INFO:
        return "INFO";

      case PHOTON_WARNING:
        return "WARNING";

      case PHOTON_ERROR:
        return "ERROR";

      case PHOTON_FATAL:
        return "FATAL";
    }
    return "";
  }

  QString logLevel() const {
    return LogManager::enumLevelToStr(m_logLevel);
  }

  void setLogLevel(int level);
  void setLogLevel(const QString& level);

  Q_INVOKABLE void log(const QString& message, int level = PHOTON_INFO);
  Q_INVOKABLE void clearLog();

 signals:
  void logLocationChanged();
  void logLevelChanged();

 private:
  static LogManager* s_instance;
  QString m_logLocation;
  int m_logLevel = PHOTON_INFO;
  QFile m_logFile;
  QMutex m_logMutex;


  void openLogFile();
};

}  // namespace photon
