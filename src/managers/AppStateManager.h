#pragma once

#include <QJSEngine>
#include <QObject>
#include <QQmlEngine>
#include <QSettings>
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
  Q_PROPERTY(QString lastOpenedFolder READ lastOpenedFolder NOTIFY
                 lastOpenedFolderChanged)
  Q_PROPERTY(
      bool hasLastSession READ hasLastSession NOTIFY hasLastSessionChanged)
  Q_PROPERTY(QString preferredGpu READ preferredGpu WRITE setPreferredGpu NOTIFY
                 preferredGpuChanged)
  Q_PROPERTY(int cacheSizeGB READ cacheSizeGB WRITE setCacheSizeGB NOTIFY
                 cacheSizeGBChanged)

 public:
  enum class ViewState { Welcome, Library, Develop };
  Q_ENUM(ViewState)

  explicit AppStateManager(QObject* parent = nullptr);
  ~AppStateManager() override;

  // Singleton accessor
  static AppStateManager* instance();

  // QML singleton factory
  static QObject* createQmlInstance(QQmlEngine* engine,
                                    QJSEngine* scriptEngine);

  // Getters
  ViewState currentView() const { return m_currentView; }
  QString currentFolder() const { return m_currentFolder; }
  QString lastOpenedFolder() const { return m_lastOpenedFolder; }
  bool hasLastSession() const { return !m_lastOpenedFolder.isEmpty(); }
  QString preferredGpu() const { return m_preferredGpu; }
  int cacheSizeGB() const { return m_cacheSizeGB; }

  // Settings operations
  Q_INVOKABLE void loadSettings();
  Q_INVOKABLE void saveSettings();
  Q_INVOKABLE void clearLastSession();

 public slots:
  void setCurrentView(ViewState view);
  void setCurrentFolder(const QString& folder);
  void setPreferredGpu(const QString& gpu);
  void setCacheSizeGB(int size);

 signals:
  void currentViewChanged();
  void currentFolderChanged();
  void lastOpenedFolderChanged();
  void hasLastSessionChanged();
  void preferredGpuChanged();
  void cacheSizeGBChanged();

 private:
  static AppStateManager* s_instance;

  ViewState m_currentView = ViewState::Welcome;
  QString m_currentFolder;
  QString m_lastOpenedFolder;
  QString m_preferredGpu = "Auto";
  int m_cacheSizeGB = 10;

  QSettings m_settings;

  static constexpr const char* KEY_LAST_FOLDER = "workspace/lastOpenedFolder";
  static constexpr const char* KEY_PREFERRED_GPU = "performance/preferredGpu";
  static constexpr const char* KEY_CACHE_SIZE = "performance/cacheSizeGB";
};
