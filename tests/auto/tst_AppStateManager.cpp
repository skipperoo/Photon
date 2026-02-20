#include <QtTest>
#include <QTemporaryDir>

#include "managers/AppStateManager.h"

class TestAppStateManager : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanupTestCase();
  void testSingleton();
  void testViewState();
  void testCurrentFolder();
  void testHasLastSession();
  void testContinueSession();
  void testSettingsPersistence();

 private:
  AppStateManager* m_manager;
};

void TestAppStateManager::initTestCase() {
  m_manager = AppStateManager::instance("PhotonTest");
  QVERIFY(m_manager != nullptr);
}

void TestAppStateManager::cleanupTestCase() {
  // Cleanup is handled by singleton
}

void TestAppStateManager::testSingleton() {
  AppStateManager* instance1 = AppStateManager::instance();
  AppStateManager* instance2 = AppStateManager::instance();
  QCOMPARE(instance1, instance2);
}

void TestAppStateManager::testViewState() {
  // Test initial state
  QCOMPARE(m_manager->currentView(), AppStateManager::ViewState::Welcome);

  // Test setting view state
  m_manager->setCurrentView(AppStateManager::ViewState::Library);
  QCOMPARE(m_manager->currentView(), AppStateManager::ViewState::Library);

  // Test signal emission
  QSignalSpy spy(m_manager, &AppStateManager::currentViewChanged);
  m_manager->setCurrentView(AppStateManager::ViewState::Develop);
  QCOMPARE(spy.count(), 1);

  // Test no signal on same value
  m_manager->setCurrentView(AppStateManager::ViewState::Develop);
  QCOMPARE(spy.count(), 1);
}

void TestAppStateManager::testCurrentFolder() {
  QSignalSpy folderSpy(m_manager, &AppStateManager::currentFolderChanged);
  QSignalSpy lastFolderSpy(m_manager,
                           &AppStateManager::lastOpenedFolderChanged);
  QSignalSpy sessionSpy(m_manager, &AppStateManager::hasLastSessionChanged);

  QTemporaryDir tempDir;
  QString testPath = tempDir.path();
  m_manager->setCurrentFolder(testPath);

  QCOMPARE(m_manager->currentFolder(), testPath);
  QCOMPARE(m_manager->lastOpenedFolder(), testPath);
  QCOMPARE(folderSpy.count(), 1);
  QCOMPARE(lastFolderSpy.count(), 1);
  QCOMPARE(sessionSpy.count(), 1);
}

void TestAppStateManager::testHasLastSession() {
  // Clear any existing session
  m_manager->clearLastSession();
  QVERIFY(!m_manager->hasLastSession());

  // Set a folder
  QTemporaryDir tempDir;
  QString testPath = tempDir.path();
  m_manager->setCurrentFolder(testPath);
  QVERIFY(m_manager->hasLastSession());

  // Clear again
  m_manager->clearLastSession();
  QVERIFY(!m_manager->hasLastSession());
}

void TestAppStateManager::testContinueSession() {
  m_manager->setCurrentView(AppStateManager::ViewState::Welcome);
  m_manager->setCurrentFolder("");
  
  QTemporaryDir tempDir;
  QString testPath = tempDir.path();
  m_manager->setCurrentFolder(testPath);
  
  // Go back to welcome and clear current folder (but lastOpened remains)
  m_manager->setCurrentView(AppStateManager::ViewState::Welcome);
  m_manager->setCurrentFolder("");
  
  QCOMPARE(m_manager->currentFolder(), QString(""));
  QCOMPARE(m_manager->lastOpenedFolder(), testPath);
  QVERIFY(m_manager->hasLastSession());
  
  m_manager->continueSession();
  
  QCOMPARE(m_manager->currentFolder(), testPath);
  QCOMPARE(m_manager->currentView(), AppStateManager::ViewState::Library);
}

void TestAppStateManager::testSettingsPersistence() {
  // Set some values
  QTemporaryDir tempDir;
  QString testPath = tempDir.path();
  m_manager->setCurrentFolder(testPath);
  m_manager->setPreferredGpu("Vulkan");
  m_manager->setCacheSizeGB(20);

  // Force save
  m_manager->saveSettings();

  // Values should persist
  QCOMPARE(m_manager->lastOpenedFolder(), testPath);
  QCOMPARE(m_manager->preferredGpu(), QString("Vulkan"));
  QCOMPARE(m_manager->cacheSizeGB(), 20);
}

QTEST_MAIN(TestAppStateManager)
#include "tst_AppStateManager.moc"
