#include "LogManager.h"
#include <QDebug>

namespace photon {

LogManager* LogManager::s_instance = nullptr;

LogManager::LogManager(QObject* parent) : QObject(parent) {
    // Default log location
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(defaultDir);
    m_logLocation = defaultDir + "/photon.log";
    openLogFile();
}

LogManager::~LogManager() {
    if (m_logFile.isOpen()) {
        m_logFile.close();
    }
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

void LogManager::openLogFile() {
    if (m_logFile.isOpen()) {
        m_logFile.close();
    }
    m_logFile.setFileName(m_logLocation);
    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "Failed to open log file at" << m_logLocation;
    } else {
        log("Logging started at " + m_logLocation);
    }
}

void LogManager::log(const QString& message, const QString& level) {
    if (!m_logFile.isOpen()) return;

    QTextStream out(&m_logFile);
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    out << QString("[%1] [%2] %3\n").arg(timestamp, level.leftJustified(5), message);
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

} // namespace photon
