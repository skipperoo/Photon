#include "PresetManager.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

PresetManager::PresetManager(QObject* parent) : QObject(parent) {
  m_presetsPath = QDir::toNativeSeparators(
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
      "/presets");
  QDir().mkpath(m_presetsPath);
  refreshPresets();
}

PresetManager::~PresetManager() {}

void PresetManager::savePreset(const QString& name,
                               const QVariantMap& settings) {
  if (name.isEmpty()) return;

  QString safeName = name;
  safeName.replace("/", "_").replace("\\", "_");
  QString filePath =
      QDir::toNativeSeparators(m_presetsPath + "/" + safeName + ".json");

  QFile file(filePath);
  if (file.open(QIODevice::WriteOnly)) {
    QJsonDocument doc(QJsonObject::fromVariantMap(settings));
    file.write(doc.toJson());
    refreshPresets();
  }
}

void PresetManager::deletePreset(const QString& name) {
  QString filePath =
      QDir::toNativeSeparators(m_presetsPath + "/" + name + ".json");
  if (QFile::remove(filePath)) {
    refreshPresets();
  }
}

QVariantMap PresetManager::loadPreset(const QString& name) const {
  QString filePath =
      QDir::toNativeSeparators(m_presetsPath + "/" + name + ".json");
  QFile file(filePath);
  if (file.open(QIODevice::ReadOnly)) {
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.object().toVariantMap();
  }
  return QVariantMap();
}

void PresetManager::refreshPresets() {
  QDir dir(m_presetsPath);
  QStringList filters;
  filters << "*.json";
  QStringList files = dir.entryList(filters, QDir::Files);

  QStringList names;
  for (const QString& file : files) {
    names << QFileInfo(file).baseName();
  }

  if (m_presets != names) {
    m_presets = names;
    emit presetsChanged();
  }
}
