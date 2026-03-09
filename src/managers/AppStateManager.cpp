#include "AppStateManager.h"

#include <vulkan/vulkan.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QUrl>

#include "LogManager.h"
#include "PreviewManager.h"
#include "Version.h"

using namespace photon;

namespace {
QString editsPathForImage(const QString& imagePath, bool ensureDirectory) {
  if (imagePath.isEmpty()) return QString();

  QFileInfo fileInfo(imagePath);
  QString editsDir = QDir::toNativeSeparators(fileInfo.absolutePath() +
                                              "/.PhotonData/edits");
  if (ensureDirectory) QDir().mkpath(editsDir);
  return QDir::toNativeSeparators(editsDir + "/" + fileInfo.fileName() +
                                  ".json");
}

QJsonArray readEditStates(const QString& editsPath) {
  QJsonArray arr;
  if (!QFile::exists(editsPath)) return arr;

  QFile file(editsPath);
  if (!file.open(QIODevice::ReadOnly)) return arr;

  QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  if (doc.isArray()) arr = doc.array();
  return arr;
}

bool writeEditStates(const QString& editsPath, const QJsonArray& arr) {
  QFile file(editsPath);
  if (!file.open(QIODevice::WriteOnly)) return false;
  file.write(QJsonDocument(arr).toJson());
  return true;
}
}  // namespace

AppStateManager* AppStateManager::s_instance = nullptr;

AppStateManager::AppStateManager(const QString& appName, QObject* parent)
    : QObject(parent),
      m_settings(QSettings::IniFormat, QSettings::UserScope, appName, appName) {
  s_instance = this;
  detectGpus();
  loadSettings();
}

AppStateManager::~AppStateManager() { s_instance = nullptr; }

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

QString AppStateManager::version() const { return PHOTON_VERSION_STRING; }

void AppStateManager::loadSettings() {
  QMutexLocker locker(&m_mutex);
  m_lastOpenedFolder = m_settings.value(KEY_LAST_FOLDER, QString()).toString();
  m_preferredGpu = m_settings.value(KEY_PREFERRED_GPU, "Auto").toString();
  m_cacheSizeGB = m_settings.value(KEY_CACHE_SIZE, 10).toInt();
  m_scanIntervalSeconds = m_settings.value(KEY_SCAN_INTERVAL, 10).toInt();
  m_isDarkMode = m_settings.value(KEY_DARK_MODE, true).toBool();
  m_accentColor = m_settings.value(KEY_ACCENT_COLOR, "#3b82f6").toString();
  m_previewDenoiseFull =
      m_settings.value(KEY_PREVIEW_DENOISE_FULL, false).toBool();
  QString level = m_settings.value("diagnostics/logLevel", "INFO").toString();
  LogManager::instance()->setMinLogLevel(level);

  emit lastOpenedFolderChanged();
  emit hasLastSessionChanged();
  emit preferredGpuChanged();
  emit isDarkModeChanged();
  emit accentColorChanged();
  emit cacheSizeGBChanged();
  emit scanIntervalSecondsChanged();
}

void AppStateManager::saveSettings() {
  QMutexLocker locker(&m_mutex);
  m_settings.setValue(KEY_LAST_FOLDER, m_lastOpenedFolder);
  m_settings.setValue(KEY_PREFERRED_GPU, m_preferredGpu);
  m_settings.setValue(KEY_CACHE_SIZE, m_cacheSizeGB);
  m_settings.setValue(KEY_SCAN_INTERVAL, m_scanIntervalSeconds);
  m_settings.setValue(KEY_DARK_MODE, m_isDarkMode);
  m_settings.setValue(KEY_ACCENT_COLOR, m_accentColor);
  m_settings.setValue(KEY_PREVIEW_DENOISE_FULL, m_previewDenoiseFull);
  m_settings.sync();
}

void AppStateManager::clearLastSession() {
  QMutexLocker locker(&m_mutex);
  m_lastOpenedFolder.clear();
  m_settings.remove(KEY_LAST_FOLDER);
  emit lastOpenedFolderChanged();
  emit hasLastSessionChanged();
}

QVariantMap AppStateManager::continueSession() {
  QVariantMap result;
  result["isSet"] = false;
  result["exists"] = false;
  if (hasLastSession()) {
    result["isSet"] = true;
    if (!QFile::exists(m_lastOpenedFolder)){
      clearLastSession();
      return result;
    }
    setCurrentFolder(m_lastOpenedFolder);
    setCurrentView(ViewState::Library);
    result["exists"] = true;
  }
  return result;
}

void AppStateManager::clearThumbnailCache() {
  if (m_currentFolder.isEmpty()) return;

  QString thumbCachePath = QDir::toNativeSeparators(
      m_currentFolder + "/.PhotonData/cache/thumbnails");
  QDir thumbDir(thumbCachePath);
  if (thumbDir.exists()) {
    thumbDir.removeRecursively();
    thumbDir.mkpath(".");
    LogManager::instance()->log("Thumbnail cache cleared for " +
                                m_currentFolder);
  }
}

void AppStateManager::setCurrentView(ViewState view) {
  if (m_currentView != view) {
    m_currentView = view;
    emit currentViewChanged();
  }
}

void AppStateManager::setCurrentFolder(const QString& folder) {
  // Convert file:// URLs to local paths (handles both QUrl objects and string
  // URLs)
  QString localPath = folder;
  if (folder.startsWith("file://")) {
    QUrl url(folder);
    if (url.isValid()) {
      localPath = url.toLocalFile();
    }
  }

  // Ensure native path separators for cross-platform compatibility
  localPath = QDir::toNativeSeparators(localPath);

  if (m_currentFolder != localPath) {
    m_currentFolder = localPath;

    if (!localPath.isEmpty()) {
      // Create .PhotonData folder structure if it doesn't exist
      QDir folderDir(localPath);
      if (folderDir.exists()) {
        m_lastOpenedFolder = localPath;
        QString photonDataPath =
            QDir::toNativeSeparators(localPath + "/.PhotonData");

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

        // Start background preview generation
        if (PreviewManager::instance()) {
          PreviewManager::instance()->startFolderScan(m_currentFolder);
        }
      } else {
        qWarning() << "Folder does not exist:" << localPath;
      }
    }

    emit currentFolderChanged();
  }
}

void AppStateManager::setCurrentImage(const QString& image) {
  LogManager::instance()->log(
      QString("[ AppStateManager ] - setCurrentImage START: %1").arg(image),
      "DEBUG");

  if (m_currentImage != image) {
    m_currentImage = image;
    emit currentImageChanged();

    // Auto-select if nothing is selected or if current selection was just the
    // old current image
    if (m_selectedImages.isEmpty() || (m_selectedImages.size() <= 1)) {
      m_selectedImages.clear();
      m_selectedImages.append(image);
      emit selectedImagesChanged();
    }
  }

  LogManager::instance()->log("[ AppStateManager ] - setCurrentImage END",
                              "DEBUG");
}

void AppStateManager::toggleSelection(const QString& path) {
  if (m_selectedImages.contains(path)) {
    m_selectedImages.removeAll(path);
  } else {
    m_selectedImages.append(path);
  }
  emit selectedImagesChanged();
}

void AppStateManager::selectRange(const QString& path,
                                  const QStringList& allPaths) {
  if (allPaths.isEmpty()) return;

  int firstIdx = -1;
  if (!m_selectedImages.isEmpty()) {
    firstIdx = allPaths.indexOf(m_selectedImages.first());
  } else if (!m_currentImage.isEmpty()) {
    firstIdx = allPaths.indexOf(m_currentImage);
  }

  int lastIdx = allPaths.indexOf(path);

  if (firstIdx == -1 || lastIdx == -1) {
    toggleSelection(path);
    return;
  }

  int start = std::min(firstIdx, lastIdx);
  int end = std::max(firstIdx, lastIdx);

  m_selectedImages.clear();
  for (int i = start; i <= end; ++i) {
    m_selectedImages.append(allPaths[i]);
  }
  emit selectedImagesChanged();
}

void AppStateManager::selectAll(const QStringList& allPaths) {
  m_selectedImages = allPaths;
  emit selectedImagesChanged();
}

void AppStateManager::clearSelection() {
  if (!m_selectedImages.isEmpty()) {
    m_selectedImages.clear();
    emit selectedImagesChanged();
  }
}

bool AppStateManager::isSelected(const QString& path) const {
  return m_selectedImages.contains(path);
}

void AppStateManager::setRatingForSelected(int rating) {
  if (m_selectedImages.isEmpty()) {
    return;
  }

  for (const QString& path : m_selectedImages) {
    // Update sidecar file
    QFileInfo fileInfo(path);
    QString editsDir = QDir::toNativeSeparators(fileInfo.absolutePath() +
                                                "/.PhotonData/edits");
    QDir().mkpath(editsDir);
    QString editsPath = QDir::toNativeSeparators(editsDir + "/" +
                                                 fileInfo.fileName() + ".json");

    QJsonArray arr;
    if (QFile::exists(editsPath)) {
      QFile file(editsPath);
      if (file.open(QIODevice::ReadOnly)) {
        arr = QJsonDocument::fromJson(file.readAll()).array();
      }
    }

    QJsonObject lastState;
    if (!arr.isEmpty()) {
      lastState = arr.last().toObject();
    }

    // Only update if rating changed
    if (lastState.contains("rating") && lastState["rating"].toInt() == rating) {
      continue;
    }

    lastState["rating"] = rating;

    if (arr.isEmpty()) {
      arr.append(lastState);
    } else {
      arr.replace(arr.size() - 1, lastState);
    }

    QFile file(editsPath);
    if (file.open(QIODevice::WriteOnly)) {
      file.write(QJsonDocument(arr).toJson());
    }
  }

  emit ratingUpdated();
}

QVariantMap AppStateManager::loadSettingsForImage(const QString& path) const {
  QString editsPath = editsPathForImage(path, false);
  if (editsPath.isEmpty()) return {};

  QJsonArray arr = readEditStates(editsPath);
  if (arr.isEmpty()) return {};

  return arr.last().toObject().toVariantMap();
}

void AppStateManager::applySettingsForSelected(const QVariantMap& settings,
                                               const QString& excludePath) {
  if (m_selectedImages.isEmpty() || settings.isEmpty()) return;

  bool changedAny = false;
  for (const QString& path : m_selectedImages) {
    if (path.isEmpty() || path == excludePath) continue;

    QString editsPath = editsPathForImage(path, true);
    if (editsPath.isEmpty()) continue;

    QJsonArray arr = readEditStates(editsPath);
    QJsonObject lastState = arr.isEmpty() ? QJsonObject() : arr.last().toObject();

    bool changed = false;
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
      QJsonValue value = QJsonValue::fromVariant(it.value());
      if (!lastState.contains(it.key()) || lastState[it.key()] != value) {
        lastState[it.key()] = value;
        changed = true;
      }
    }

    if (!changed) continue;

    if (arr.isEmpty()) {
      arr.append(lastState);
    } else {
      arr.replace(arr.size() - 1, lastState);
    }

    if (writeEditStates(editsPath, arr)) {
      changedAny = true;
    }
  }

  if (changedAny) emit editsUpdated();
}

void AppStateManager::rotateSelectedRight(const QString& excludePath) {
  if (m_selectedImages.isEmpty()) return;

  bool changedAny = false;
  for (const QString& path : m_selectedImages) {
    if (path.isEmpty() || path == excludePath) continue;

    QString editsPath = editsPathForImage(path, true);
    if (editsPath.isEmpty()) continue;

    QJsonArray arr = readEditStates(editsPath);
    QJsonObject lastState = arr.isEmpty() ? QJsonObject() : arr.last().toObject();
    int steps = lastState.value("orientationSteps").toInt(0);
    lastState["orientationSteps"] = ((steps % 4) + 1) % 4;

    if (arr.isEmpty()) {
      arr.append(lastState);
    } else {
      arr.replace(arr.size() - 1, lastState);
    }

    if (writeEditStates(editsPath, arr)) {
      changedAny = true;
    }
  }

  if (changedAny) emit editsUpdated();
}

void AppStateManager::rotateSelectedLeft(const QString& excludePath) {
  if (m_selectedImages.isEmpty()) return;

  bool changedAny = false;
  for (const QString& path : m_selectedImages) {
    if (path.isEmpty() || path == excludePath) continue;

    QString editsPath = editsPathForImage(path, true);
    if (editsPath.isEmpty()) continue;

    QJsonArray arr = readEditStates(editsPath);
    QJsonObject lastState = arr.isEmpty() ? QJsonObject() : arr.last().toObject();
    int steps = lastState.value("orientationSteps").toInt(0);
    lastState["orientationSteps"] = ((steps % 4) + 3) % 4;

    if (arr.isEmpty()) {
      arr.append(lastState);
    } else {
      arr.replace(arr.size() - 1, lastState);
    }

    if (writeEditStates(editsPath, arr)) {
      changedAny = true;
    }
  }

  if (changedAny) emit editsUpdated();
}

void AppStateManager::flipSelectedHorizontal(const QString& excludePath) {
  if (m_selectedImages.isEmpty()) return;

  bool changedAny = false;
  for (const QString& path : m_selectedImages) {
    if (path.isEmpty() || path == excludePath) continue;

    QString editsPath = editsPathForImage(path, true);
    if (editsPath.isEmpty()) continue;

    QJsonArray arr = readEditStates(editsPath);
    QJsonObject lastState = arr.isEmpty() ? QJsonObject() : arr.last().toObject();
    bool flip = lastState.value("flipHorizontal").toBool(false);
    lastState["flipHorizontal"] = !flip;

    if (arr.isEmpty()) {
      arr.append(lastState);
    } else {
      arr.replace(arr.size() - 1, lastState);
    }

    if (writeEditStates(editsPath, arr)) {
      changedAny = true;
    }
  }

  if (changedAny) emit editsUpdated();
}

void AppStateManager::flipSelectedVertical(const QString& excludePath) {
  if (m_selectedImages.isEmpty()) return;

  bool changedAny = false;
  for (const QString& path : m_selectedImages) {
    if (path.isEmpty() || path == excludePath) continue;

    QString editsPath = editsPathForImage(path, true);
    if (editsPath.isEmpty()) continue;

    QJsonArray arr = readEditStates(editsPath);
    QJsonObject lastState = arr.isEmpty() ? QJsonObject() : arr.last().toObject();
    bool flip = lastState.value("flipVertical").toBool(false);
    lastState["flipVertical"] = !flip;

    if (arr.isEmpty()) {
      arr.append(lastState);
    } else {
      arr.replace(arr.size() - 1, lastState);
    }

    if (writeEditStates(editsPath, arr)) {
      changedAny = true;
    }
  }

  if (changedAny) emit editsUpdated();
}

void AppStateManager::setPreferredGpu(const QString& gpu) {
  if (m_preferredGpu != gpu) {
    m_preferredGpu = gpu;
    emit preferredGpuChanged();
    saveSettings();
    LogManager::instance()->log("Preferred GPU changed to " + gpu +
                                ". Restart may be required.");
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
  // Convert file:// URLs to local paths
  QString localPath = location;
  if (location.startsWith("file://")) {
    QUrl url(location);
    if (url.isValid()) {
      localPath = url.toLocalFile();
    }
  }

  // Ensure native path separators for cross-platform compatibility
  localPath = QDir::toNativeSeparators(localPath);

  LogManager::instance()->setLogLocation(localPath);
  emit logLocationChanged();
  m_settings.setValue("diagnostics/logLocation", localPath);
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

void AppStateManager::setScanIntervalSeconds(int seconds) {
  if (m_scanIntervalSeconds != seconds) {
    m_scanIntervalSeconds = qBound(1, seconds, 300);
    emit scanIntervalSecondsChanged();
    saveSettings();
  }
}
