#include "LogManager.h"

#include <filesystem>
#include <QDebug>
#include <QDir>
#include <QMutexLocker>
#include <QGuiApplication>

namespace photon {

LogManager* LogManager::s_instance = nullptr;

LogManager::LogManager(QObject* parent) : QObject(parent) {
  // Default log location
  QString defaultDir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(defaultDir);
  m_logLocation = QDir::toNativeSeparators(defaultDir + "/photon.log");
  // If we get to 100MB log file we truncate it before the session starts
  // so that we do not lose the current session logs.
  // Would be better to rotate the logs, but this is quick and good for now
  if (std::filesystem::file_size(m_logLocation.toStdString()) / (1024 * 1024) > 100)
    clearLog();
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


void LogManager::setLogLevel(const QString& level) {
  setLogLevel(strLevelToEnum(level));
}

void LogManager::setLogLevel(int level) {
  if (m_logLevel != level) {
    m_logLevel = level;
    emit logLevelChanged();
  }
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
    log("Logging started at " + m_logLocation, PHOTON_INFO);
  }
}

void LogManager::log(const QString& message, int level) {
  QMutexLocker locker(&m_logMutex);
  if (level < m_logLevel) return;
  if (!m_logFile.isOpen()) return;

  QTextStream out(&m_logFile);
  QString timestamp =
      QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
  out << QString("[ %1 ] [ %2 ] %3\n").arg(timestamp, enumLevelToStr(level), message);
  out.flush();
  if (level == PHOTON_FATAL)
    QGuiApplication::quit();
}

void LogManager::clearLog() {
  // Otherwise the automatic cleanup does not work
  if (m_logFile.fileName().isEmpty() && !m_logLocation.isEmpty())
    m_logFile.setFileName(m_logLocation);

  if (m_logFile.isOpen()) {
    m_logFile.close();
  }
  if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    m_logFile.close();
  }
  openLogFile();
}

}  // namespace photon
