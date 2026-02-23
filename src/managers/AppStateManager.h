#pragma once

#include <QDir>
#include <QJSEngine>
#include <QMutex>
#include <QObject>
#include <QQmlEngine>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

/**
 * @brief Manages application state and persistent settings.
 */
class AppStateManager : public QObject {
  Q_OBJECT
  Q_PROPERTY(ViewState currentView READ currentView WRITE setCurrentView NOTIFY
                 currentViewChanged)
  Q_PROPERTY(QString currentFolder READ currentFolder WRITE setCurrentFolder
                 NOTIFY currentFolderChanged)
  Q_PROPERTY(QString currentImage READ currentImage WRITE setCurrentImage NOTIFY
                 currentImageChanged)
  Q_PROPERTY(QStringList selectedImages READ selectedImages NOTIFY
                 selectedImagesChanged)
  Q_PROPERTY(
      int selectionCount READ selectionCount NOTIFY selectedImagesChanged)
  Q_PROPERTY(QString lastOpenedFolder READ lastOpenedFolder NOTIFY
                 lastOpenedFolderChanged)
  Q_PROPERTY(
      bool hasLastSession READ hasLastSession NOTIFY hasLastSessionChanged)
  Q_PROPERTY(QString preferredGpu READ preferredGpu WRITE setPreferredGpu NOTIFY
                 preferredGpuChanged)
  Q_PROPERTY(
      QStringList availableGpus READ availableGpus NOTIFY availableGpusChanged)
  Q_PROPERTY(bool isDarkMode READ isDarkMode WRITE setIsDarkMode NOTIFY
                 isDarkModeChanged)
  Q_PROPERTY(QString accentColor READ accentColor WRITE setAccentColor NOTIFY
                 accentColorChanged)
  Q_PROPERTY(QString logLocation READ logLocation WRITE setLogLocation NOTIFY
                 logLocationChanged)
  Q_PROPERTY(bool previewDenoiseFull READ previewDenoiseFull WRITE
                 setPreviewDenoiseFull NOTIFY previewDenoiseFullChanged)
  Q_PROPERTY(
      QString logLevel READ logLevel WRITE setLogLevel NOTIFY logLevelChanged)
  Q_PROPERTY(int cacheSizeGB READ cacheSizeGB WRITE setCacheSizeGB NOTIFY
                 cacheSizeGBChanged)

 public:
  enum class ViewState { Welcome, Library, Develop, Settings };
  Q_ENUM(ViewState)

  explicit AppStateManager(const QString& appName = "Photon",
                           QObject* parent = nullptr);
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
  QStringList selectedImages() const { return m_selectedImages; }
  int selectionCount() const { return m_selectedImages.size(); }
  QString lastOpenedFolder() const { return m_lastOpenedFolder; }
  bool hasLastSession() const { return !m_lastOpenedFolder.isEmpty(); }
  QString preferredGpu() const { return m_preferredGpu; }
  QStringList availableGpus() const { return m_availableGpus; }
  bool isDarkMode() const { return m_isDarkMode; }
  QString accentColor() const { return m_accentColor; }
  bool previewDenoiseFull() const { return m_previewDenoiseFull; }
  QString logLocation() const;
  QString logLevel() const;
  int cacheSizeGB() const { return m_cacheSizeGB; }

  // Settings operations
  Q_INVOKABLE void loadSettings();
  Q_INVOKABLE void saveSettings();
  Q_INVOKABLE void clearLastSession();
  Q_INVOKABLE void continueSession();
  Q_INVOKABLE void clearThumbnailCache();

  // Selection operations
  Q_INVOKABLE void toggleSelection(const QString& path);
  Q_INVOKABLE void selectRange(const QString& path,
                               const QStringList& allPaths);
  Q_INVOKABLE void selectAll(const QStringList& allPaths);
  Q_INVOKABLE void clearSelection();
  Q_INVOKABLE bool isSelected(const QString& path) const;
  Q_INVOKABLE void setRatingForSelected(int rating);

 public slots:
  void setCurrentView(ViewState view);
  void setCurrentFolder(const QString& folder);
  void setCurrentImage(const QString& image);
  void setPreferredGpu(const QString& gpu);
  void setIsDarkMode(bool dark);
  void setAccentColor(const QString& color);
  void setPreviewDenoiseFull(bool full);
  void setLogLocation(const QString& location);
  void setLogLevel(const QString& level);
  void setCacheSizeGB(int size);

 signals:
  void currentViewChanged();
  void currentFolderChanged();
  void currentImageChanged();
  void selectedImagesChanged();
  void lastOpenedFolderChanged();
  void ratingUpdated();
  void hasLastSessionChanged();
  void preferredGpuChanged();
  void availableGpusChanged();
  void isDarkModeChanged();
  void accentColorChanged();
  void previewDenoiseFullChanged();
  void logLocationChanged();
  void logLevelChanged();
  void cacheSizeGBChanged();

 private:
  void detectGpus();

  static AppStateManager* s_instance;

  ViewState m_currentView = ViewState::Welcome;
  QString m_currentFolder;
  QString m_currentImage;
  QStringList m_selectedImages;
  QString m_lastOpenedFolder;
  QString m_preferredGpu = "Auto";
  QStringList m_availableGpus;
  bool m_isDarkMode = true;
  QString m_accentColor = "#3b82f6";  // Default Blue
  bool m_previewDenoiseFull = false;
  int m_cacheSizeGB = 10;

  QSettings m_settings;
  mutable QMutex m_mutex;

  static constexpr const char* KEY_LAST_FOLDER = "workspace/lastOpenedFolder";
  static constexpr const char* KEY_PREFERRED_GPU = "performance/preferredGpu";
  static constexpr const char* KEY_CACHE_SIZE = "performance/cacheSizeGB";
  static constexpr const char* KEY_DARK_MODE = "ui/darkMode";
  static constexpr const char* KEY_ACCENT_COLOR = "ui/accentColor";
  static constexpr const char* KEY_PREVIEW_DENOISE_FULL =
      "performance/previewDenoiseFull";
};
