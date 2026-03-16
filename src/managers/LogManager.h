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
  ERROR,
  FATAL
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
      return DEBUG;
    else if (level == "INFO")
      return INFO;
    else if (level == "WARNING")
      return WARNING;
    else if (level == "ERROR")
      return ERROR;
    else if (level == "FATAL")
      return FATAL;

    return DEBUG;
  }

  QString enumLevelToStr(int level) const {
    switch(level) {
      case DEBUG:
        return "DEBUG";

      case INFO:
        return "INFO";

      case WARNING:
        return "WARNING";

      case ERROR:
        return "ERROR";

      case FATAL:
        return "FATAL";
    }
    return "";
  }

  QString logLevel() const { 
    return LogManager::enumLevelToStr(m_logLevel);
  }

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
