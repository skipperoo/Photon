#pragma once

#include <QDir>
#include <QJSEngine>
#include <QObject>
#include <QQmlEngine>
#include <QSettings>
#include <QStandardPaths>
#include <QString>

/**
 * @brief Manages application state and persistent settings.
 *
 * This singleton class handles:
 * - Application view state (Welcome, Library, Develop)
 * - Last opened folder persistence
 * - Application preferences (GPU selection, cache size)
 * - Settings persistence using QSettings
 */
class AppStateManager : public QObject {
  Q_OBJECT
  Q_PROPERTY(ViewState currentView READ currentView WRITE setCurrentView NOTIFY
                 currentViewChanged)
  Q_PROPERTY(QString currentFolder READ currentFolder WRITE setCurrentFolder
                 NOTIFY currentFolderChanged)
  Q_PROPERTY(QString currentImage READ currentImage WRITE setCurrentImage NOTIFY
                 currentImageChanged)
  Q_PROPERTY(QString lastOpenedFolder READ lastOpenedFolder NOTIFY
                 lastOpenedFolderChanged)
  Q_PROPERTY(
      bool hasLastSession READ hasLastSession NOTIFY hasLastSessionChanged)
  Q_PROPERTY(QString preferredGpu READ preferredGpu WRITE setPreferredGpu NOTIFY
                 preferredGpuChanged)
  Q_PROPERTY(QStringList availableGpus READ availableGpus NOTIFY
                 availableGpusChanged)
  Q_PROPERTY(bool isDarkMode READ isDarkMode WRITE setIsDarkMode NOTIFY
                 isDarkModeChanged)
  Q_PROPERTY(QString accentColor READ accentColor WRITE setAccentColor NOTIFY
                 accentColorChanged)
  Q_PROPERTY(QString logLocation READ logLocation WRITE setLogLocation NOTIFY
                 logLocationChanged)
  Q_PROPERTY(int cacheSizeGB READ cacheSizeGB WRITE setCacheSizeGB NOTIFY
                 cacheSizeGBChanged)

 public:
  enum class ViewState { Welcome, Library, Develop, Settings };
  Q_ENUM(ViewState)

  explicit AppStateManager(const QString& appName = "Photon", QObject* parent = nullptr);
  ~AppStateManager() override;

  // Singleton accessor
  static AppStateManager* instance(const QString& appName = "Photon");

  // QML singleton factory
  static QObject* createQmlInstance(QQmlEngine* engine,
                                    QJSEngine* scriptEngine);

  // Getters
  ViewState currentView() const { return m_currentView; }
  QString currentFolder() const { return m_currentFolder; }
  QString currentImage() const { return m_currentImage; }
  QString lastOpenedFolder() const { return m_lastOpenedFolder; }
  bool hasLastSession() const { return !m_lastOpenedFolder.isEmpty(); }
  QString preferredGpu() const { return m_preferredGpu; }
  QStringList availableGpus() const { return m_availableGpus; }
  bool isDarkMode() const { return m_isDarkMode; }
  QString accentColor() const { return m_accentColor; }
  QString logLocation() const;
  int cacheSizeGB() const { return m_cacheSizeGB; }

  // Settings operations
  Q_INVOKABLE void loadSettings();
  Q_INVOKABLE void saveSettings();
  Q_INVOKABLE void clearLastSession();
  Q_INVOKABLE void continueSession();
  Q_INVOKABLE void clearThumbnailCache();

 public slots:
  void setCurrentView(ViewState view);
  void setCurrentFolder(const QString& folder);
  void setCurrentImage(const QString& image);
  void setPreferredGpu(const QString& gpu);
  void setIsDarkMode(bool dark);
  void setAccentColor(const QString& color);
  void setLogLocation(const QString& location);
  void setCacheSizeGB(int size);

 signals:
  void currentViewChanged();
  void currentFolderChanged();
  void currentImageChanged();
  void lastOpenedFolderChanged();
  void hasLastSessionChanged();
  void preferredGpuChanged();
  void availableGpusChanged();
  void isDarkModeChanged();
  void accentColorChanged();
  void logLocationChanged();
  void cacheSizeGBChanged();

 private:
  void detectGpus();

  static AppStateManager* s_instance;

  ViewState m_currentView = ViewState::Welcome;
  QString m_currentFolder;
  QString m_currentImage;
  QString m_lastOpenedFolder;
  QString m_preferredGpu = "Auto";
  QStringList m_availableGpus;
  bool m_isDarkMode = true;
  QString m_accentColor = "#3b82f6"; // Default Blue
  int m_cacheSizeGB = 10;

  QSettings m_settings;

  static constexpr const char* KEY_LAST_FOLDER = "workspace/lastOpenedFolder";
  static constexpr const char* KEY_PREFERRED_GPU = "performance/preferredGpu";
  static constexpr const char* KEY_CACHE_SIZE = "performance/cacheSizeGB";
  static constexpr const char* KEY_DARK_MODE = "ui/darkMode";
  static constexpr const char* KEY_ACCENT_COLOR = "ui/accentColor";
};
