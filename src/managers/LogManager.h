#pragma once

#include <QObject>
#include <QString>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>

namespace photon {

class LogManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString logLocation READ logLocation WRITE setLogLocation NOTIFY logLocationChanged)

public:
    explicit LogManager(QObject* parent = nullptr);
    ~LogManager() override;

    static LogManager* instance();

    QString logLocation() const { return m_logLocation; }
    void setLogLocation(const QString& location);

    Q_INVOKABLE void log(const QString& message, const QString& level = "INFO");
    Q_INVOKABLE void clearLog();

signals:
    void logLocationChanged();

private:
    static LogManager* s_instance;
    QString m_logLocation;
    QFile m_logFile;

    void openLogFile();
};

} // namespace photon
