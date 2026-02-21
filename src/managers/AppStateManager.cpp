#include "AppStateManager.h"
#include "LogManager.h"

#include <QDebug>
#include <QDir>
#include <vulkan/vulkan.h>

using namespace photon;

AppStateManager* AppStateManager::s_instance = nullptr;

AppStateManager::AppStateManager(const QString& appName, QObject* parent)
    : QObject(parent),
      m_settings(QSettings::IniFormat, QSettings::UserScope, "Photon",
                 appName) {
  detectGpus();
  loadSettings();
}

AppStateManager::~AppStateManager() = default;

AppStateManager* AppStateManager::instance(const QString& appName) {
  if (!s_instance) {
    s_instance = new AppStateManager(appName);
  }
  return s_instance;
}

QObject* AppStateManager::createQmlInstance(QQmlEngine* engine,
                                            QJSEngine* scriptEngine) {
  Q_UNUSED(engine)
  Q_UNUSED(scriptEngine)
  return instance();
}

void AppStateManager::detectGpus() {
  m_availableGpus.clear();
  m_availableGpus.append("Auto");

  VkInstance instance = VK_NULL_HANDLE;
  VkInstanceCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

  if (vkCreateInstance(&createInfo, nullptr, &instance) == VK_SUCCESS) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount > 0) {
      std::vector<VkPhysicalDevice> devices(deviceCount);
      vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
      for (auto device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        m_availableGpus.append(QString::fromUtf8(props.deviceName));
      }
    }
    vkDestroyInstance(instance, nullptr);
  } else {
    qWarning() << "Failed to create Vulkan instance for GPU detection";
  }
  emit availableGpusChanged();
}

void AppStateManager::loadSettings() {
  m_lastOpenedFolder = m_settings.value(KEY_LAST_FOLDER, QString()).toString();
  m_preferredGpu = m_settings.value(KEY_PREFERRED_GPU, "Auto").toString();
  m_cacheSizeGB = m_settings.value(KEY_CACHE_SIZE, 10).toInt();
  m_isDarkMode = m_settings.value(KEY_DARK_MODE, true).toBool();
  m_accentColor = m_settings.value(KEY_ACCENT_COLOR, "#3b82f6").toString();
  m_previewDenoiseFull = m_settings.value(KEY_PREVIEW_DENOISE_FULL, false).toBool();
  QString level = m_settings.value("diagnostics/logLevel", "INFO").toString();
  LogManager::instance()->setMinLogLevel(level);

  emit lastOpenedFolderChanged();
  emit hasLastSessionChanged();
  emit preferredGpuChanged();
  emit isDarkModeChanged();
  emit accentColorChanged();
  emit cacheSizeGBChanged();
}

void AppStateManager::saveSettings() {
  m_settings.setValue(KEY_LAST_FOLDER, m_lastOpenedFolder);
  m_settings.setValue(KEY_PREFERRED_GPU, m_preferredGpu);
  m_settings.setValue(KEY_CACHE_SIZE, m_cacheSizeGB);
  m_settings.setValue(KEY_DARK_MODE, m_isDarkMode);
  m_settings.setValue(KEY_ACCENT_COLOR, m_accentColor);
  m_settings.setValue(KEY_PREVIEW_DENOISE_FULL, m_previewDenoiseFull);
  m_settings.sync();
}

void AppStateManager::clearLastSession() {
  m_lastOpenedFolder.clear();
  m_settings.remove(KEY_LAST_FOLDER);
  emit lastOpenedFolderChanged();
  emit hasLastSessionChanged();
}

void AppStateManager::continueSession() {
  if (hasLastSession()) {
    setCurrentFolder(m_lastOpenedFolder);
    setCurrentView(ViewState::Library);
  }
}

void AppStateManager::clearThumbnailCache() {
  if (m_currentFolder.isEmpty()) return;

  QString thumbCachePath = m_currentFolder + "/.PhotonData/cache/thumbnails";
  QDir thumbDir(thumbCachePath);
  if (thumbDir.exists()) {
    thumbDir.removeRecursively();
    thumbDir.mkpath(".");
    LogManager::instance()->log("Thumbnail cache cleared for " + m_currentFolder);
  }
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

    if (!folder.isEmpty()) {
      // Create .PhotonData folder structure if it doesn't exist
      QDir folderDir(folder);
      if (folderDir.exists()) {
        m_lastOpenedFolder = folder;
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

        emit lastOpenedFolderChanged();
        emit hasLastSessionChanged();
        saveSettings();
      } else {
        qWarning() << "Folder does not exist:" << folder;
      }
    }

    emit currentFolderChanged();
  }
}

void AppStateManager::setCurrentImage(const QString& image) {
  if (m_currentImage != image) {
    m_currentImage = image;
    emit currentImageChanged();
  }
}

void AppStateManager::setPreferredGpu(const QString& gpu) {
  if (m_preferredGpu != gpu) {
    m_preferredGpu = gpu;
    emit preferredGpuChanged();
    saveSettings();
    LogManager::instance()->log("Preferred GPU changed to " + gpu + ". Restart may be required.");
  }
}

void AppStateManager::setIsDarkMode(bool dark) {
  if (m_isDarkMode != dark) {
    m_isDarkMode = dark;
    emit isDarkModeChanged();
    saveSettings();
  }
}

void AppStateManager::setAccentColor(const QString& color) {
  if (m_accentColor != color) {
    m_accentColor = color;
    emit accentColorChanged();
    saveSettings();
  }
}

void AppStateManager::setPreviewDenoiseFull(bool full) {
  if (m_previewDenoiseFull != full) {
    m_previewDenoiseFull = full;
    emit previewDenoiseFullChanged();
    saveSettings();
  }
}

QString AppStateManager::logLocation() const {
  return LogManager::instance()->logLocation();
}

void AppStateManager::setLogLocation(const QString& location) {
  LogManager::instance()->setLogLocation(location);
  emit logLocationChanged();
  // We don't save log location in m_settings here as LogManager handles its own persistence if needed, 
  // but let's be consistent and save it if we want it to persist across sessions via Photon settings.
  m_settings.setValue("diagnostics/logLocation", location);
  m_settings.sync();
}

QString AppStateManager::logLevel() const {
  return LogManager::instance()->minLogLevel();
}

void AppStateManager::setLogLevel(const QString& level) {
  LogManager::instance()->setMinLogLevel(level);
  emit logLevelChanged();
  m_settings.setValue("diagnostics/logLevel", level);
  m_settings.sync();
}

void AppStateManager::setCacheSizeGB(int size) {
  if (m_cacheSizeGB != size) {
    m_cacheSizeGB = size;
    emit cacheSizeGBChanged();
    saveSettings();
  }
}
