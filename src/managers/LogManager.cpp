#include "LogManager.h"

#include <QDebug>
#include <QDir>
#include <QMutexLocker>

namespace photon {

LogManager* LogManager::s_instance = nullptr;

LogManager::LogManager(QObject* parent) : QObject(parent) {
  // Default log location
  QString defaultDir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(defaultDir);
  m_logLocation = QDir::toNativeSeparators(defaultDir + "/photon.log");
  openLogFile();
}

LogManager::~LogManager() {
  if (m_logFile.isOpen()) {
    m_logFile.close();
  }
  s_instance = nullptr;
}

LogManager* LogManager::instance() {
  if (!s_instance) {
    s_instance = new LogManager();
  }
  return s_instance;
}

void LogManager::setLogLocation(const QString& location) {
  if (m_logLocation != location) {
    m_logLocation = location;
    openLogFile();
    emit logLocationChanged();
  }
}

void LogManager::setMinLogLevel(const QString& level) {
  if (m_minLogLevel != level) {
    m_minLogLevel = level;
    emit minLogLevelChanged();
  }
}

int LogManager::levelToInt(const QString& level) const {
  if (level == "DEBUG") return 0;
  if (level == "INFO") return 1;
  if (level == "WARNING") return 2;
  if (level == "ERROR") return 3;
  return 1;
}

void LogManager::openLogFile() {
  if (m_logFile.isOpen()) {
    m_logFile.close();
  }
  m_logFile.setFileName(m_logLocation);
  if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append |
                      QIODevice::Text)) {
    qWarning() << "Failed to open log file at" << m_logLocation;
  } else {
    log("Logging started at " + m_logLocation, "INFO");
  }
}

void LogManager::log(const QString& message, const QString& level) {
  QMutexLocker locker(&m_logMutex);
  if (levelToInt(level) < levelToInt(m_minLogLevel)) return;
  if (!m_logFile.isOpen()) return;

  QTextStream out(&m_logFile);
  QString timestamp =
      QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
  out << QString("[%1] [%2] %3\n").arg(timestamp, level, message);
  out.flush();
}

void LogManager::clearLog() {
  if (m_logFile.isOpen()) {
    m_logFile.close();
  }
  if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    m_logFile.close();
  }
  openLogFile();
}

}  // namespace photon
