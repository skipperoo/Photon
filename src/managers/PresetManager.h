#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QDir>
#include <QStandardPaths>
#include <QtQmlIntegration/qqmlintegration.h>

class PresetManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList presets READ presets NOTIFY presetsChanged)
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit PresetManager(QObject* parent = nullptr);

    QStringList presets() const { return m_presets; }

    Q_INVOKABLE void savePreset(const QString& name, const QVariantMap& settings);
    Q_INVOKABLE void deletePreset(const QString& name);
    Q_INVOKABLE QVariantMap loadPreset(const QString& name) const;

signals:
    void presetsChanged();

private:
    QStringList m_presets;
    QString m_presetsPath;
    void refreshPresets();
};
