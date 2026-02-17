#include "AppStateManager.h"

#include <QDebug>
#include <QDir>

AppStateManager* AppStateManager::s_instance = nullptr;

AppStateManager::AppStateManager(QObject* parent)
    : QObject(parent),
      m_settings(QSettings::IniFormat, QSettings::UserScope, "Photon",
                 "Photon") {
  loadSettings();
}

AppStateManager::~AppStateManager() = default;

AppStateManager* AppStateManager::instance() {
  if (!s_instance) {
    s_instance = new AppStateManager();
  }
  return s_instance;
}

QObject* AppStateManager::createQmlInstance(QQmlEngine* engine,
                                            QJSEngine* scriptEngine) {
  Q_UNUSED(engine)
  Q_UNUSED(scriptEngine)
  return instance();
}

void AppStateManager::loadSettings() {
  m_lastOpenedFolder = m_settings.value(KEY_LAST_FOLDER, QString()).toString();
  m_preferredGpu = m_settings.value(KEY_PREFERRED_GPU, "Auto").toString();
  m_cacheSizeGB = m_settings.value(KEY_CACHE_SIZE, 10).toInt();

  emit lastOpenedFolderChanged();
  emit hasLastSessionChanged();
  emit preferredGpuChanged();
  emit cacheSizeGBChanged();
}

void AppStateManager::saveSettings() {
  m_settings.setValue(KEY_LAST_FOLDER, m_lastOpenedFolder);
  m_settings.setValue(KEY_PREFERRED_GPU, m_preferredGpu);
  m_settings.setValue(KEY_CACHE_SIZE, m_cacheSizeGB);
  m_settings.sync();
}

void AppStateManager::clearLastSession() {
  m_lastOpenedFolder.clear();
  m_settings.remove(KEY_LAST_FOLDER);
  emit lastOpenedFolderChanged();
  emit hasLastSessionChanged();
}

void AppStateManager::setCurrentView(ViewState view) {
  if (m_currentView != view) {
    m_currentView = view;
    emit currentViewChanged();
  }
}

void AppStateManager::setCurrentFolder(const QString& folder) {
  if (m_currentFolder != folder) {
    m_currentFolder = folder;
    m_lastOpenedFolder = folder;

    // Create .PhotonData folder structure if it doesn't exist
    QDir folderDir(folder);
    if (folderDir.exists()) {
      QString photonDataPath = folder + "/.PhotonData";

      // Create .PhotonData directory
      QDir photonDir(photonDataPath);
      if (!photonDir.exists()) {
        folderDir.mkpath(".PhotonData");
      }

      // Create subdirectories
      photonDir.mkpath("edits");
      photonDir.mkpath("cache/thumbnails");
      photonDir.mkpath("cache/previews");
    }

    emit currentFolderChanged();
    emit lastOpenedFolderChanged();
    emit hasLastSessionChanged();

    saveSettings();
  }
}

void AppStateManager::setPreferredGpu(const QString& gpu) {
  if (m_preferredGpu != gpu) {
    m_preferredGpu = gpu;
    emit preferredGpuChanged();
    saveSettings();
  }
}

void AppStateManager::setCacheSizeGB(int size) {
  if (m_cacheSizeGB != size) {
    m_cacheSizeGB = size;
    emit cacheSizeGBChanged();
    saveSettings();
  }
}
