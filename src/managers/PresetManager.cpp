#include "PresetManager.h"

#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>

#include "LogManager.h"

namespace {

const QString kGlobal = QStringLiteral("global");
const QString kProject = QStringLiteral("project");
const QString kStateFile = QStringLiteral(".presets-state");

void log(const QString& msg, photon::LogLevel level = photon::PHOTON_INFO) {
  photon::LogManager::instance()->log("[ PresetManager.cpp ] - " + msg, level);
}

// A name usable as a file / folder name: no path separators or characters
// Windows rejects, no leading dots (hidden files), at most 120 characters.
QString sanitize(const QString& name) {
  QString s = name.trimmed();
  for (const QChar c : QStringLiteral("/\\:*?\"<>|")) s.replace(c, QLatin1Char('_'));
  while (s.startsWith(QLatin1Char('.'))) s.remove(0, 1);
  s = s.trimmed().left(120);
  return s;
}

QString localPath(const QString& file) {
  return file.startsWith(QLatin1String("file:")) ? QUrl(file).toLocalFile() : file;
}

QStringList sortedNames(QStringList names) {
  QCollator collator;
  collator.setNumericMode(true);
  collator.setCaseSensitivity(Qt::CaseInsensitive);
  std::sort(names.begin(), names.end(),
            [&](const QString& a, const QString& b) { return collator.compare(a, b) < 0; });
  return names;
}

QStringList presetNamesIn(const QString& dirPath) {
  QStringList names;
  const QDir dir(dirPath);
  for (const QFileInfo& fi : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files))
    names << fi.completeBaseName();
  return sortedNames(names);
}

}  // namespace

PresetManager::PresetManager(QObject* parent)
    : PresetManager(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                        QStringLiteral("/presets"),
                    parent) {}

PresetManager::PresetManager(const QString& globalRoot, QObject* parent)
    : QObject(parent), m_globalRoot(QDir::cleanPath(globalRoot)) {
  QDir().mkpath(m_globalRoot);
  loadState();
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
          [this](const QString&) { refresh(); });
  refresh();
}

PresetManager::~PresetManager() = default;

// --- Paths ------------------------------------------------------------------

QString PresetManager::root(const QString& scope) const {
  return scope == kProject ? m_projectRoot : m_globalRoot;
}

QString PresetManager::folderPath(const QString& scope, const QString& folder) const {
  const QString r = root(scope);
  if (r.isEmpty()) return QString();
  const QString f = sanitize(folder);
  return f.isEmpty() ? r : r + QLatin1Char('/') + f;
}

QString PresetManager::presetPath(const QString& scope, const QString& folder,
                                  const QString& name) const {
  const QString dir = folderPath(scope, folder);
  const QString n = sanitize(name);
  if (dir.isEmpty() || n.isEmpty()) return QString();
  return dir + QLatin1Char('/') + n + QStringLiteral(".json");
}

QString PresetManager::freeName(const QString& scope, const QString& folder,
                                const QString& name) const {
  const QString base = sanitize(name);
  if (!presetExists(scope, folder, base)) return base;
  for (int i = 2;; ++i) {
    const QString candidate = QStringLiteral("%1 (%2)").arg(base).arg(i);
    if (!presetExists(scope, folder, candidate)) return candidate;
  }
}

QString PresetManager::favoriteKey(const QString& folder, const QString& name) {
  const QString f = sanitize(folder);
  const QString n = sanitize(name);
  return f.isEmpty() ? n : f + QLatin1Char('/') + n;
}

// --- Library -------------------------------------------------------------------

QVariantList PresetManager::library(const QString& scope) const {
  QVariantList list;
  const QString r = root(scope);
  if (r.isEmpty() || !QDir(r).exists()) return list;
  auto group = [&](const QString& folder) {
    QVariantList presets;
    for (const QString& name : presetNamesIn(folderPath(scope, folder))) {
      QVariantMap p;
      p["name"] = name;
      p["favorite"] = scope == kGlobal && m_favorites.contains(favoriteKey(folder, name));
      presets << p;
    }
    QVariantMap g;
    g["folder"] = folder;
    g["presets"] = presets;
    return g;
  };
  list << group(QString());
  for (const QString& folder : folders(scope)) list << group(folder);
  return list;
}

QVariantList PresetManager::favorites() const {
  QVariantList list;
  QStringList keys;
  for (const QString& key : m_favorites) keys << key;
  // By preset name, then folder.
  std::sort(keys.begin(), keys.end(), [](const QString& a, const QString& b) {
    const QString na = a.section(QLatin1Char('/'), -1), nb = b.section(QLatin1Char('/'), -1);
    const int c = QString::localeAwareCompare(na.toLower(), nb.toLower());
    return c != 0 ? c < 0 : a < b;
  });
  for (const QString& key : keys) {
    const QString folder = key.contains(QLatin1Char('/')) ? key.section(QLatin1Char('/'), 0, 0) : QString();
    const QString name = key.section(QLatin1Char('/'), -1);
    if (!presetExists(kGlobal, folder, name)) continue;
    QVariantMap p;
    p["folder"] = folder;
    p["name"] = name;
    list << p;
  }
  return list;
}

QStringList PresetManager::folders(const QString& scope) const {
  const QString r = root(scope);
  if (r.isEmpty()) return {};
  return sortedNames(QDir(r).entryList(QDir::Dirs | QDir::NoDotAndDotDot));
}

int PresetManager::presetCount(const QString& scope, const QString& folder) const {
  const QString dir = folderPath(scope, folder);
  return dir.isEmpty() ? 0 : int(presetNamesIn(dir).size());
}

void PresetManager::setProjectFolder(const QString& folder) {
  const QString projectRoot =
      folder.isEmpty() ? QString()
                       : QDir::cleanPath(localPath(folder)) + QStringLiteral("/.PhotonData/presets");
  if (projectRoot == m_projectRoot) return;
  m_projectRoot = projectRoot;
  m_projectName = folder.isEmpty() ? QString() : QFileInfo(QDir::cleanPath(localPath(folder))).fileName();
  emit projectChanged();
  refresh();
}

void PresetManager::refresh() {
  rewatch();
  emit presetsChanged();
}

void PresetManager::rewatch() {
  if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
  QStringList dirs;
  for (const QString& scope : {kGlobal, kProject}) {
    const QString r = root(scope);
    if (r.isEmpty() || !QDir(r).exists()) continue;
    dirs << r;
    for (const QString& f : folders(scope)) dirs << r + QLatin1Char('/') + f;
  }
  if (!dirs.isEmpty()) m_watcher.addPaths(dirs);
}

// --- State (favorites) ----------------------------------------------------------

void PresetManager::loadState() {
  QFile f(m_globalRoot + QLatin1Char('/') + kStateFile);
  if (!f.open(QIODevice::ReadOnly)) return;
  const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
  m_favorites.clear();
  for (const QJsonValue& v : o.value(QStringLiteral("favorites")).toArray())
    if (!v.toString().isEmpty()) m_favorites.insert(v.toString());
  m_favoritesOnly = o.value(QStringLiteral("favoritesOnly")).toBool(false);
}

void PresetManager::saveState() const {
  QJsonArray favs;
  QStringList keys(m_favorites.begin(), m_favorites.end());
  keys.sort();
  for (const QString& k : keys) favs.append(k);
  QJsonObject o;
  o["favorites"] = favs;
  o["favoritesOnly"] = m_favoritesOnly;
  QSaveFile f(m_globalRoot + QLatin1Char('/') + kStateFile);
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(o).toJson());
    f.commit();
  }
}

void PresetManager::setFavoritesOnly(bool on) {
  if (m_favoritesOnly == on) return;
  m_favoritesOnly = on;
  saveState();
  emit favoritesOnlyChanged();
}

bool PresetManager::isFavorite(const QString& folder, const QString& name) const {
  return m_favorites.contains(favoriteKey(folder, name));
}

void PresetManager::setFavorite(const QString& folder, const QString& name, bool on) {
  if (!presetExists(kGlobal, folder, name)) return;
  const QString key = favoriteKey(folder, name);
  if (on == m_favorites.contains(key)) return;
  if (on)
    m_favorites.insert(key);
  else
    m_favorites.remove(key);
  saveState();
  emit presetsChanged();
}

// --- Presets ------------------------------------------------------------------------

bool PresetManager::writePreset(const QString& path, const QJsonObject& settings) const {
  if (path.isEmpty()) return false;
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return false;
  f.write(QJsonDocument(settings).toJson());
  return f.commit();
}

bool PresetManager::savePreset(const QString& scope, const QString& folder,
                               const QString& name, const QVariantMap& settings) {
  if (settings.isEmpty()) return false;
  const QString path = presetPath(scope, folder, name);
  if (!writePreset(path, QJsonObject::fromVariantMap(settings))) {
    log(QString("could not save preset %1").arg(path), photon::PHOTON_WARNING);
    return false;
  }
  refresh();
  return true;
}

QVariantMap PresetManager::loadPreset(const QString& scope, const QString& folder,
                                      const QString& name) const {
  QFile f(presetPath(scope, folder, name));
  if (!f.open(QIODevice::ReadOnly)) return {};
  return QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
}

bool PresetManager::presetExists(const QString& scope, const QString& folder,
                                 const QString& name) const {
  const QString path = presetPath(scope, folder, name);
  return !path.isEmpty() && QFileInfo::exists(path);
}

bool PresetManager::deletePreset(const QString& scope, const QString& folder,
                                 const QString& name) {
  if (!QFile::remove(presetPath(scope, folder, name))) return false;
  if (scope == kGlobal && m_favorites.remove(favoriteKey(folder, name))) saveState();
  refresh();
  return true;
}

bool PresetManager::renamePreset(const QString& scope, const QString& folder,
                                 const QString& name, const QString& newName) {
  const QString to = presetPath(scope, folder, newName);
  const QString from = presetPath(scope, folder, name);
  if (to.isEmpty() || from.isEmpty() || from == to || QFileInfo::exists(to)) return false;
  if (!QFile::rename(from, to)) return false;
  if (scope == kGlobal && m_favorites.remove(favoriteKey(folder, name))) {
    m_favorites.insert(favoriteKey(folder, newName));
    saveState();
  }
  refresh();
  return true;
}

bool PresetManager::movePreset(const QString& scope, const QString& folder,
                               const QString& name, const QString& toScope,
                               const QString& toFolder) {
  const QString from = presetPath(scope, folder, name);
  const QString to = presetPath(toScope, toFolder, name);
  if (from.isEmpty() || to.isEmpty() || from == to || QFileInfo::exists(to) ||
      !QFileInfo::exists(from))
    return false;
  QDir().mkpath(QFileInfo(to).absolutePath());
  if (!QFile::rename(from, to)) return false;  // copies across file systems
  const bool wasFavorite = scope == kGlobal && m_favorites.remove(favoriteKey(folder, name));
  if (wasFavorite && toScope == kGlobal) m_favorites.insert(favoriteKey(toFolder, name));
  if (wasFavorite) saveState();
  refresh();
  return true;
}

bool PresetManager::copyPreset(const QString& scope, const QString& folder,
                               const QString& name, const QString& toScope,
                               const QString& toFolder) {
  const QString from = presetPath(scope, folder, name);
  if (from.isEmpty() || !QFileInfo::exists(from) || root(toScope).isEmpty()) return false;
  const QString to = presetPath(toScope, toFolder, freeName(toScope, toFolder, name));
  QDir().mkpath(QFileInfo(to).absolutePath());
  if (!QFile::copy(from, to)) return false;
  refresh();
  return true;
}

// --- Folders -------------------------------------------------------------------------

bool PresetManager::createFolder(const QString& scope, const QString& name) {
  const QString n = sanitize(name);
  if (n.isEmpty() || root(scope).isEmpty()) return false;
  const QString path = folderPath(scope, n);
  if (QFileInfo::exists(path)) return false;
  if (!QDir().mkpath(path)) return false;
  refresh();
  return true;
}

bool PresetManager::renameFolder(const QString& scope, const QString& name,
                                 const QString& newName) {
  const QString n = sanitize(name), nn = sanitize(newName);
  if (n.isEmpty() || nn.isEmpty() || n == nn) return false;
  const QString from = folderPath(scope, n), to = folderPath(scope, nn);
  if (!QFileInfo(from).isDir() || QFileInfo::exists(to)) return false;
  if (!QDir().rename(from, to)) return false;
  if (scope == kGlobal) {
    bool changed = false;
    for (const QString& key : QStringList(m_favorites.begin(), m_favorites.end())) {
      if (key.startsWith(n + QLatin1Char('/'))) {
        m_favorites.remove(key);
        m_favorites.insert(nn + key.mid(n.size()));
        changed = true;
      }
    }
    if (changed) saveState();
  }
  refresh();
  return true;
}

bool PresetManager::deleteFolder(const QString& scope, const QString& name) {
  const QString n = sanitize(name);
  if (n.isEmpty()) return false;
  QDir dir(folderPath(scope, n));
  if (!dir.exists() || !dir.removeRecursively()) return false;
  if (scope == kGlobal) {
    bool changed = false;
    for (const QString& key : QStringList(m_favorites.begin(), m_favorites.end())) {
      if (key.startsWith(n + QLatin1Char('/'))) changed |= m_favorites.remove(key);
    }
    if (changed) saveState();
  }
  refresh();
  return true;
}

// --- Import / export ---------------------------------------------------------------

bool PresetManager::exportPreset(const QString& scope, const QString& folder,
                                 const QString& name, const QString& file) {
  QFile in(presetPath(scope, folder, name));
  if (!in.open(QIODevice::ReadOnly)) return false;
  QString path = localPath(file);
  if (!path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)) path += QStringLiteral(".json");
  QSaveFile out(path);
  if (!out.open(QIODevice::WriteOnly)) return false;
  out.write(in.readAll());
  return out.commit();
}

namespace {
bool writeZip(const QString& file, const QList<QPair<QString, QString>>& entries) {
  QString path = localPath(file);
  if (!path.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)) path += QStringLiteral(".zip");
  QZipWriter zip(path);
  if (zip.status() != QZipWriter::NoError) return false;
  zip.setCompressionPolicy(QZipWriter::AutoCompress);
  for (const auto& [entry, source] : entries) {
    QFile f(source);
    if (!f.open(QIODevice::ReadOnly)) continue;
    zip.addFile(entry, f.readAll());
  }
  zip.close();
  return zip.status() == QZipWriter::NoError;
}
}  // namespace

bool PresetManager::exportFolder(const QString& scope, const QString& folder,
                                 const QString& file) {
  QList<QPair<QString, QString>> entries;
  const QStringList groups = folder.isEmpty() ? (QStringList{QString()} + folders(scope))
                                              : QStringList{sanitize(folder)};
  for (const QString& g : groups) {
    for (const QString& name : presetNamesIn(folderPath(scope, g))) {
      const QString entry = (g.isEmpty() ? QString() : g + QLatin1Char('/')) + name +
                            QStringLiteral(".json");
      entries.append({entry, presetPath(scope, g, name)});
    }
  }
  if (entries.isEmpty()) return false;
  return writeZip(file, entries);
}

bool PresetManager::exportFavorites(const QString& file) {
  QList<QPair<QString, QString>> entries;
  for (const QVariant& v : favorites()) {
    const QVariantMap p = v.toMap();
    const QString folder = p.value("folder").toString(), name = p.value("name").toString();
    entries.append({favoriteKey(folder, name) + QStringLiteral(".json"),
                    presetPath(kGlobal, folder, name)});
  }
  if (entries.isEmpty()) return false;
  return writeZip(file, entries);
}

bool PresetManager::importPresetData(const QByteArray& data, const QString& scope,
                                     const QString& folder, const QString& name) {
  if (data.size() > kMaxPresetBytes) return false;
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject() || doc.object().isEmpty())
    return false;
  const QString n = sanitize(name);
  if (n.isEmpty()) return false;
  return writePreset(presetPath(scope, folder, freeName(scope, folder, n)), doc.object());
}

QVariantMap PresetManager::importPresets(const QStringList& files, const QString& scope,
                                         const QString& folder) {
  int imported = 0, skipped = 0;
  if (root(scope).isEmpty()) {
    return {{"imported", 0}, {"skipped", int(files.size())},
            {"message", QStringLiteral("No folder is open for project presets")}};
  }
  for (const QString& file : files) {
    const QString path = localPath(file);
    const QFileInfo fi(path);
    if (fi.suffix().compare(QLatin1String("zip"), Qt::CaseInsensitive) == 0) {
      QZipReader zip(path);
      if (!zip.isReadable() || zip.status() != QZipReader::NoError) {
        ++skipped;
        continue;
      }
      const QList<QZipReader::FileInfo> entries = zip.fileInfoList();
      if (entries.size() > kMaxZipEntries) {
        ++skipped;
        continue;
      }
      for (const QZipReader::FileInfo& e : entries) {
        if (!e.isFile || !e.filePath.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
          continue;
        const QStringList parts = e.filePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        // Skip macOS metadata and hidden files.
        if (parts.isEmpty() || parts.first() == QLatin1String("__MACOSX") ||
            parts.last().startsWith(QLatin1Char('.'))) {
          continue;
        }
        if (e.size > kMaxPresetBytes) {
          ++skipped;
          continue;
        }
        // One level of folders: an entry's folder is its parent directory.
        const QString entryFolder = parts.size() > 1 ? sanitize(parts.at(parts.size() - 2)) : folder;
        const QString name = QFileInfo(parts.last()).completeBaseName();
        if (importPresetData(zip.fileData(e.filePath), scope, entryFolder, name))
          ++imported;
        else
          ++skipped;
      }
    } else {
      QFile f(path);
      if (fi.size() > kMaxPresetBytes || !f.open(QIODevice::ReadOnly)) {
        ++skipped;
        continue;
      }
      if (importPresetData(f.readAll(), scope, folder, fi.completeBaseName()))
        ++imported;
      else
        ++skipped;
    }
  }
  refresh();
  QString message = imported == 1 ? QStringLiteral("Imported 1 preset")
                                  : QStringLiteral("Imported %1 presets").arg(imported);
  if (skipped > 0) message += QStringLiteral(" (%1 skipped: not a valid preset)").arg(skipped);
  log(QString("import into %1/%2: %3").arg(scope, folder, message));
  return {{"imported", imported}, {"skipped", skipped}, {"message", message}};
}
