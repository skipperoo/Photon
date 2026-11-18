#include <QtCore/private/qzipwriter_p.h>

#include <QtTest>

#include "PresetManager.h"

class TestPresetManager : public QObject {
  Q_OBJECT

 private slots:
  void testSaveLoadAndLibrary();
  void testRenameMoveCopy();
  void testFavorites();
  void testFolders();
  void testExportImportRoundTrip();
  void testImportRejectsBadData();
  void testProjectScope();
};

namespace {
const QString G = QStringLiteral("global");
const QString P = QStringLiteral("project");

QVariantMap look(double exposure) {
  return {{"contrast", 1.1}, {"saturation", -10.0}, {"exposure", exposure}};
}

QStringList names(const QVariantList& library, const QString& folder) {
  for (const QVariant& g : library) {
    const QVariantMap m = g.toMap();
    if (m.value("folder").toString() != folder) continue;
    QStringList out;
    for (const QVariant& p : m.value("presets").toList()) out << p.toMap().value("name").toString();
    return out;
  }
  return {};
}
}  // namespace

void TestPresetManager::testSaveLoadAndLibrary() {
  QTemporaryDir dir;
  PresetManager pm(dir.path(), nullptr);
  QVERIFY(pm.savePreset(G, "", "Warm", look(0)));
  QVERIFY(pm.savePreset(G, "Film", "Portra 400", look(0)));
  QVERIFY(pm.savePreset(G, "Film", "Ektar 100", look(0)));
  QVERIFY(pm.savePreset(G, "", "a/b:c", look(0)));  // unsafe characters
  QVERIFY(QFileInfo::exists(dir.filePath("Film/Portra 400.json")));
  QVERIFY(QFileInfo::exists(dir.filePath("a_b_c.json")));
  // Unfiled first, then folders; names in natural order.
  const QVariantList lib = pm.globalLibrary();
  QCOMPARE(lib.size(), 2);
  QCOMPARE(lib.first().toMap().value("folder").toString(), QString());
  QCOMPARE(names(lib, ""), (QStringList{"a_b_c", "Warm"}));
  QCOMPARE(names(lib, "Film"), (QStringList{"Ektar 100", "Portra 400"}));
  QCOMPARE(pm.loadPreset(G, "Film", "Portra 400").value("saturation").toDouble(), -10.0);
  // Saving over an existing preset updates it.
  QVERIFY(pm.savePreset(G, "Film", "Portra 400", look(1)));
  QCOMPARE(pm.loadPreset(G, "Film", "Portra 400").value("exposure").toDouble(), 1.0);
  QVERIFY(!pm.savePreset(G, "", "Empty", {}));
  QVERIFY(!pm.savePreset(G, "", "   ", look(0)));
  QVERIFY(pm.deletePreset(G, "", "Warm"));
  QVERIFY(!pm.presetExists(G, "", "Warm"));
}

void TestPresetManager::testRenameMoveCopy() {
  QTemporaryDir dir;
  PresetManager pm(dir.path(), nullptr);
  pm.savePreset(G, "", "A", look(0));
  pm.savePreset(G, "", "B", look(0));
  QVERIFY(!pm.renamePreset(G, "", "A", "B"));  // taken
  QVERIFY(pm.renamePreset(G, "", "A", "C"));
  QVERIFY(pm.presetExists(G, "", "C") && !pm.presetExists(G, "", "A"));
  pm.createFolder(G, "Film");
  QVERIFY(pm.movePreset(G, "", "C", G, "Film"));
  QVERIFY(pm.presetExists(G, "Film", "C"));
  pm.savePreset(G, "", "C", look(0));
  QVERIFY(!pm.movePreset(G, "", "C", G, "Film"));  // name taken there
  QVERIFY(pm.copyPreset(G, "", "C", G, "Film"));    // copied under a free name
  QVERIFY(pm.presetExists(G, "Film", "C (2)"));
  QVERIFY(pm.copyPreset(G, "", "C", G, "Film"));
  QVERIFY(pm.presetExists(G, "Film", "C (3)"));
}

void TestPresetManager::testFavorites() {
  QTemporaryDir dir;
  {
    PresetManager pm(dir.path(), nullptr);
    pm.savePreset(G, "Film", "Portra", look(0));
    pm.savePreset(G, "", "Warm", look(0));
    pm.setFavorite("Film", "Portra", true);
    pm.setFavorite("", "Warm", true);
    pm.setFavorite("", "Missing", true);  // ignored
    QCOMPARE(pm.favorites().size(), 2);
    QVERIFY(pm.isFavorite("Film", "Portra"));
    pm.setFavoritesOnly(true);
    // Favorites follow renames and moves.
    QVERIFY(pm.renamePreset(G, "Film", "Portra", "Portra 400"));
    QVERIFY(pm.isFavorite("Film", "Portra 400") && !pm.isFavorite("Film", "Portra"));
    QVERIFY(pm.renameFolder(G, "Film", "Kodak"));
    QVERIFY(pm.isFavorite("Kodak", "Portra 400"));
    pm.createFolder(G, "Warm tones");
    QVERIFY(pm.movePreset(G, "", "Warm", G, "Warm tones"));
    QVERIFY(pm.isFavorite("Warm tones", "Warm"));
  }
  // ...and persist with the presets folder.
  PresetManager again(dir.path(), nullptr);
  QVERIFY(again.favoritesOnly());
  QCOMPARE(again.favorites().size(), 2);
  QVERIFY(again.isFavorite("Kodak", "Portra 400"));
  // Deleting a preset or its folder drops the star.
  QVERIFY(again.deletePreset(G, "Warm tones", "Warm"));
  QVERIFY(again.deleteFolder(G, "Kodak"));
  QCOMPARE(again.favorites().size(), 0);
}

void TestPresetManager::testFolders() {
  QTemporaryDir dir;
  PresetManager pm(dir.path(), nullptr);
  QVERIFY(pm.createFolder(G, "B&W"));
  QVERIFY(!pm.createFolder(G, "B&W"));
  QVERIFY(pm.createFolder(G, "Colour"));
  QCOMPARE(pm.folders(G), (QStringList{"B&W", "Colour"}));
  pm.savePreset(G, "B&W", "Tri-X", look(0));
  QCOMPARE(pm.presetCount(G, "B&W"), 1);
  QVERIFY(!pm.renameFolder(G, "B&W", "Colour"));  // taken
  QVERIFY(pm.renameFolder(G, "B&W", "Mono"));
  QVERIFY(pm.presetExists(G, "Mono", "Tri-X"));
  QVERIFY(pm.deleteFolder(G, "Mono"));
  QCOMPARE(pm.folders(G), QStringList{"Colour"});
  // The state file never shows up as a preset.
  pm.setFavoritesOnly(true);
  QCOMPARE(names(pm.globalLibrary(), "").size(), 0);
}

void TestPresetManager::testExportImportRoundTrip() {
  QTemporaryDir src, dst, out;
  PresetManager a(src.path(), nullptr);
  a.savePreset(G, "", "Warm", look(0));
  a.savePreset(G, "Film", "Portra", look(0));
  a.savePreset(G, "Film", "Ektar", look(0));
  a.setFavorite("Film", "Portra", true);
  QVERIFY(a.exportPreset(G, "", "Warm", QUrl::fromLocalFile(out.filePath("warm")).toString()));
  QVERIFY(QFileInfo::exists(out.filePath("warm.json")));  // extension added
  QVERIFY(a.exportFolder(G, "Film", out.filePath("film.zip")));
  QVERIFY(a.exportFolder(G, "", out.filePath("all.zip")));
  QVERIFY(a.exportFavorites(out.filePath("favs.zip")));

  PresetManager b(dst.path(), nullptr);
  QVariantMap r = b.importPresets({out.filePath("all.zip")}, G, "");
  QCOMPARE(r.value("imported").toInt(), 3);
  QCOMPARE(names(b.globalLibrary(), ""), QStringList{"Warm"});
  QCOMPARE(names(b.globalLibrary(), "Film"), (QStringList{"Ektar", "Portra"}));
  QCOMPARE(b.loadPreset(G, "Film", "Portra"), a.loadPreset(G, "Film", "Portra"));
  // Importing again keeps both (free names), json into the chosen folder.
  r = b.importPresets({out.filePath("film.zip"), out.filePath("warm.json")}, G, "Imported");
  QCOMPARE(r.value("imported").toInt(), 3);
  QVERIFY(b.presetExists(G, "Film", "Portra (2)"));
  QVERIFY(b.presetExists(G, "Imported", "warm"));
  r = b.importPresets({out.filePath("favs.zip")}, G, "");
  QVERIFY(b.presetExists(G, "Film", "Portra (3)"));
}

void TestPresetManager::testImportRejectsBadData() {
  QTemporaryDir dir, out;
  PresetManager pm(dir.path(), nullptr);
  auto write = [&](const QString& name, const QByteArray& data) {
    QFile f(out.filePath(name));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
  };
  write("broken.json", "{not json");
  write("array.json", "[1, 2]");
  write("empty.json", "{}");
  write("big.json", "{\"a\": \"" + QByteArray(PresetManager::kMaxPresetBytes, 'x') + "\"}");
  write("good.json", "{\"contrast\": 1.2}");
  QVariantMap r = pm.importPresets({out.filePath("broken.json"), out.filePath("array.json"),
                                    out.filePath("empty.json"), out.filePath("big.json"),
                                    out.filePath("good.json"), out.filePath("missing.json")},
                                   G, "");
  QCOMPARE(r.value("imported").toInt(), 1);
  QCOMPARE(r.value("skipped").toInt(), 5);
  // A zip: hidden and macOS metadata entries ignored, deep paths use the
  // nearest folder, non-json ignored.
  {
    QZipWriter zip(out.filePath("mixed.zip"));
    zip.addFile("A/one.json", "{\"contrast\": 1.1}");
    zip.addFile("x/y/B/two.json", "{\"contrast\": 1.1}");
    zip.addFile("__MACOSX/A/._one.json", "junk");
    zip.addFile("A/.hidden.json", "{\"contrast\": 1.1}");
    zip.addFile("readme.txt", "hi");
    zip.addFile("bad.json", "nope");
    zip.close();
  }
  r = pm.importPresets({out.filePath("mixed.zip")}, G, "");
  QCOMPARE(r.value("imported").toInt(), 2);
  QCOMPARE(r.value("skipped").toInt(), 1);
  QVERIFY(pm.presetExists(G, "A", "one"));
  QVERIFY(pm.presetExists(G, "B", "two"));
}

void TestPresetManager::testProjectScope() {
  QTemporaryDir globalDir, photos;
  PresetManager pm(globalDir.path(), nullptr);
  QVERIFY(!pm.projectAvailable());
  QVERIFY(!pm.savePreset(P, "", "Here", look(0)));  // no folder open
  QCOMPARE(pm.importPresets({"x.json"}, P, "").value("imported").toInt(), 0);
  QSignalSpy changed(&pm, &PresetManager::projectChanged);
  pm.setProjectFolder(QUrl::fromLocalFile(photos.path()).toString());
  QCOMPARE(changed.count(), 1);
  QVERIFY(pm.projectAvailable());
  QCOMPARE(pm.projectName(), QFileInfo(photos.path()).fileName());
  QVERIFY(pm.savePreset(P, "", "Here", look(0)));
  QVERIFY(QFileInfo::exists(photos.filePath(".PhotonData/presets/Here.json")));
  QCOMPARE(names(pm.projectLibrary(), ""), QStringList{"Here"});
  QVERIFY(names(pm.globalLibrary(), "").isEmpty());  // kept apart
  // Promote to global (copy), and move back.
  QVERIFY(pm.copyPreset(P, "", "Here", G, ""));
  QVERIFY(pm.presetExists(G, "", "Here"));
  pm.setFavorite("", "Here", true);
  QVERIFY(pm.movePreset(G, "", "Here", P, "Kept"));
  QVERIFY(!pm.isFavorite("", "Here"));  // only global presets are starred
  QVERIFY(pm.presetExists(P, "Kept", "Here"));
  pm.setProjectFolder(QString());
  QVERIFY(!pm.projectAvailable());
  QVERIFY(pm.projectLibrary().isEmpty());
}

QTEST_MAIN(TestPresetManager)
#include "tst_PresetManager.moc"
