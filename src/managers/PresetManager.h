#pragma once

#include <QtQmlIntegration/qqmlintegration.h>

#include <QFileSystemWatcher>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Develop presets, in two libraries:
// - global: <app data>/presets, available everywhere; presets can be starred
//   as favorites;
// - project: <open folder>/.PhotonData/presets, only while that folder is
//   open (they travel with the photos and do not clutter other projects).
// Each library holds unfiled presets and one level of folders
// (sub-directories); a preset is one JSON file of Develop settings.
// Presets are imported/exported as .json (one) or .zip (several, folders
// kept). Scopes are "global" and "project"; a folder of "" is unfiled.
class PresetManager : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  // [{ folder: string ("" = unfiled), presets: [{ name, favorite }] }],
  // unfiled first, then folders by name.
  Q_PROPERTY(QVariantList globalLibrary READ globalLibrary NOTIFY presetsChanged)
  Q_PROPERTY(QVariantList projectLibrary READ projectLibrary NOTIFY presetsChanged)
  // Starred global presets: [{ folder, name }] by name.
  Q_PROPERTY(QVariantList favorites READ favorites NOTIFY presetsChanged)
  Q_PROPERTY(bool projectAvailable READ projectAvailable NOTIFY projectChanged)
  // The open folder's name (for the "This folder" section).
  Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
  // The panel shows only favorites (persisted).
  Q_PROPERTY(bool favoritesOnly READ favoritesOnly WRITE setFavoritesOnly NOTIFY
                 favoritesOnlyChanged)

 public:
  explicit PresetManager(QObject* parent = nullptr);
  // Tests: an explicit global library folder.
  PresetManager(const QString& globalRoot, QObject* parent);
  ~PresetManager() override;

  QVariantList globalLibrary() const { return library(QStringLiteral("global")); }
  QVariantList projectLibrary() const { return library(QStringLiteral("project")); }
  QVariantList favorites() const;
  bool projectAvailable() const { return !m_projectRoot.isEmpty(); }
  QString projectName() const { return m_projectName; }
  bool favoritesOnly() const { return m_favoritesOnly; }
  void setFavoritesOnly(bool on);

  // The project library follows the open folder ("" = none).
  Q_INVOKABLE void setProjectFolder(const QString& folder);

  // Presets. Names become file names: characters that are not allowed in
  // file names are replaced; saving over an existing preset updates it.
  Q_INVOKABLE bool savePreset(const QString& scope, const QString& folder,
                              const QString& name, const QVariantMap& settings);
  Q_INVOKABLE QVariantMap loadPreset(const QString& scope, const QString& folder,
                                     const QString& name) const;
  Q_INVOKABLE bool presetExists(const QString& scope, const QString& folder,
                                const QString& name) const;
  Q_INVOKABLE bool deletePreset(const QString& scope, const QString& folder,
                                const QString& name);
  Q_INVOKABLE bool renamePreset(const QString& scope, const QString& folder,
                                const QString& name, const QString& newName);
  // Fails when the destination already has a preset with that name.
  Q_INVOKABLE bool movePreset(const QString& scope, const QString& folder,
                              const QString& name, const QString& toScope,
                              const QString& toFolder);
  // Copies under a free name ("Name (2)") when the name is taken.
  Q_INVOKABLE bool copyPreset(const QString& scope, const QString& folder,
                              const QString& name, const QString& toScope,
                              const QString& toFolder);

  // Folders (one level).
  Q_INVOKABLE QStringList folders(const QString& scope) const;
  Q_INVOKABLE bool createFolder(const QString& scope, const QString& name);
  Q_INVOKABLE bool renameFolder(const QString& scope, const QString& name,
                                const QString& newName);
  // Deletes the folder and its presets.
  Q_INVOKABLE bool deleteFolder(const QString& scope, const QString& name);
  Q_INVOKABLE int presetCount(const QString& scope, const QString& folder) const;

  // Favorites (global presets).
  Q_INVOKABLE bool isFavorite(const QString& folder, const QString& name) const;
  Q_INVOKABLE void setFavorite(const QString& folder, const QString& name, bool on);

  // Import / export. Files may be paths or file:// URLs.
  // One preset as .json.
  Q_INVOKABLE bool exportPreset(const QString& scope, const QString& folder,
                                const QString& name, const QString& file);
  // A folder ("" = the whole library) as a .zip ("<folder>/<name>.json").
  Q_INVOKABLE bool exportFolder(const QString& scope, const QString& folder,
                                const QString& file);
  // The favorites as a .zip (their folders kept).
  Q_INVOKABLE bool exportFavorites(const QString& file);
  // .json files go to `folder`; .zip entries keep their folder (unfiled ones
  // go to `folder`). Names already taken get " (2)". Returns
  // { imported, skipped, message }.
  Q_INVOKABLE QVariantMap importPresets(const QStringList& files,
                                        const QString& scope,
                                        const QString& folder);

  // Limits for imported data (a preset is a few KB).
  static constexpr qint64 kMaxPresetBytes = 1 << 20;
  static constexpr int kMaxZipEntries = 2000;

 signals:
  void presetsChanged();
  void projectChanged();
  void favoritesOnlyChanged();

 private:
  QVariantList library(const QString& scope) const;
  QString root(const QString& scope) const;
  QString folderPath(const QString& scope, const QString& folder) const;
  QString presetPath(const QString& scope, const QString& folder,
                     const QString& name) const;
  QString freeName(const QString& scope, const QString& folder,
                   const QString& name) const;
  bool writePreset(const QString& path, const QJsonObject& settings) const;
  bool importPresetData(const QByteArray& data, const QString& scope,
                        const QString& folder, const QString& name);
  static QString favoriteKey(const QString& folder, const QString& name);
  void loadState();
  void saveState() const;
  void refresh();
  void rewatch();

  QString m_globalRoot;
  QString m_projectRoot;  // "" when no folder is open
  QString m_projectName;
  QSet<QString> m_favorites;  // "folder/name" ("name" when unfiled)
  bool m_favoritesOnly = false;
  QFileSystemWatcher m_watcher;
};
