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

class LogManager : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString logLocation READ logLocation WRITE setLogLocation NOTIFY
                 logLocationChanged)
  Q_PROPERTY(QString minLogLevel READ minLogLevel WRITE setMinLogLevel NOTIFY
                 minLogLevelChanged)

 public:
  explicit LogManager(QObject* parent = nullptr);
  ~LogManager() override;

  static LogManager* instance();

  QString logLocation() const { return m_logLocation; }
  void setLogLocation(const QString& location);

  QString minLogLevel() const { return m_minLogLevel; }
  void setMinLogLevel(const QString& level);

  Q_INVOKABLE void log(const QString& message, const QString& level = "INFO");
  Q_INVOKABLE void clearLog();

 signals:
  void logLocationChanged();
  void minLogLevelChanged();

 private:
  static LogManager* s_instance;
  QString m_logLocation;
  QString m_minLogLevel = "INFO";
  QFile m_logFile;
  QMutex m_logMutex;

  int levelToInt(const QString& level) const;

  void openLogFile();
};

}  // namespace photon
